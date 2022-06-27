// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "config.h"

#include <assert.h>
#include <pic18f45k50.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common.h"
#include "diagnostics.h"
#include "hardware.h"
#include "pipe.h"
#include "usb.h"
#include "usb_descriptors.h"
#include "usb_protocol.h"

extern const struct DeviceDescriptor DEVICE_DESCRIPTOR;
extern const struct FullConfigDescriptor CONFIG_DESCRIPTOR;
extern const struct StringDescriptor0 STRING_DESCRIPTOR0;
extern const struct StringDescriptorVendor STRING_DESCRIPTOR1;
extern const struct StringDescriptorProduct STRING_DESCRIPTOR2;

#define _STAT_BSTALL_MASK (1 << 2)
#define _STAT_DTSEN_MASK (1 << 3)
#define _STAT_DTS_MASK (1 << 6)

#define ADDR_BDS 0x400
#define ADDR_EP0_OUT_BUFFER (ADDR_BDS + sizeof(struct BufferDescriptors))
#define ADDR_EP0_IN_BUFFER (ADDR_EP0_OUT_BUFFER + EP0_PACKET_SIZE)
#define ADDR_EP1_IN_EVEN_BUFFER (ADDR_EP0_IN_BUFFER + EP0_PACKET_SIZE)
#define ADDR_EP1_IN_ODD_BUFFER (ADDR_EP1_IN_EVEN_BUFFER + EP1_PACKET_SIZE)
#define ADDR_EP2_OUT_EVEN_BUFFER (ADDR_EP1_IN_ODD_BUFFER + EP1_PACKET_SIZE)
#define ADDR_EP2_OUT_ODD_BUFFER (ADDR_EP2_OUT_EVEN_BUFFER + EP2_PACKET_SIZE)
#define ADDR_EP2_IN_EVEN_BUFFER (ADDR_EP2_OUT_ODD_BUFFER + EP2_PACKET_SIZE)
#define ADDR_EP2_IN_ODD_BUFFER (ADDR_EP2_IN_EVEN_BUFFER + EP2_PACKET_SIZE)

volatile struct BufferDescriptors bds __at(ADDR_BDS);
volatile uint8_t ep0_out_buffer[EP0_PACKET_SIZE] __at(ADDR_EP0_OUT_BUFFER);
volatile uint8_t ep0_in_buffer[EP0_PACKET_SIZE] __at(ADDR_EP0_IN_BUFFER);
volatile uint8_t
    ep1_in_even_buffer[EP1_PACKET_SIZE] __at(ADDR_EP1_IN_EVEN_BUFFER);
volatile uint8_t
    ep1_in_odd_buffer[EP1_PACKET_SIZE] __at(ADDR_EP1_IN_ODD_BUFFER);
volatile uint8_t
    ep2_out_even_buffer[EP2_PACKET_SIZE] __at(ADDR_EP2_OUT_EVEN_BUFFER);
volatile uint8_t
    ep2_out_odd_buffer[EP2_PACKET_SIZE] __at(ADDR_EP2_OUT_ODD_BUFFER);
volatile uint8_t
    ep2_in_even_buffer[EP2_PACKET_SIZE] __at(ADDR_EP2_IN_EVEN_BUFFER);
volatile uint8_t
    ep2_in_odd_buffer[EP2_PACKET_SIZE] __at(ADDR_EP2_IN_ODD_BUFFER);

extern volatile struct UsbSetupPacket ep0_setup __at(ADDR_EP0_OUT_BUFFER);

volatile struct UsbState usb_state = {0};
volatile struct PipeState pipe_state = {0};

static struct LineCoding line_coding = {
    .dwDTERate = 115200, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8};

enum TxStatf {
  TX_STATF_NONE = 0,
  TX_STATF_DIR_OUT = (1 << 0),
  TX_STATF_DIR_IN = (1 << 1),
  TX_STATF_DATA_STAGE = (1 << 2),
  TX_STATF_DATA_ROM = (1 << 3),
  TX_STATF_DATA_RAM = (1 << 4),
  TX_STATF_SET_ADDR = (1 << 7),
};

struct Ep0State {
  union {
    struct {
      unsigned dir_out : 1;
      unsigned dir_in : 1;
      unsigned data_stage : 1;
      unsigned data_rom : 1;
      unsigned data_ram : 1;
      unsigned : 2;
      unsigned set_addr : 1;
    };
    uint8_t tx_statf;
  };
  uint8_t addr;
  const __rom uint8_t *rom_in_ptr;
  const __ram uint8_t *ram_in_ptr;
  __ram uint8_t *ram_out_ptr;
  uint16_t data_len;
};

volatile static struct Ep0State ep0_state;

union BulkFlags {
  struct {
    unsigned cpu_dts : 1;
    unsigned cpu_odd : 1;
    unsigned sie_dts : 1;
    unsigned sie_odd : 1;
    unsigned even_busy : 1;
    unsigned odd_busy : 1;
  };
  uint8_t flags;
};

volatile static union BulkFlags bulk_in;
volatile static union BulkFlags bulk_out;

#define BD_ARM_DATA(bd, dts, len)                                              \
  do {                                                                         \
    bd.STAT = (dts ? _STAT_DTS_MASK : 0) | _STAT_DTSEN_MASK;                   \
    bd.BC_L = len;                                                             \
    bd.UOWN = 1;                                                               \
  } while (0);

#define BD_ARM_CTRL_STATUS(bd, len)                                            \
  do {                                                                         \
    bd.STAT = _STAT_DTS_MASK | _STAT_DTSEN_MASK;                               \
    bd.BC_L = len;                                                             \
    bd.UOWN = 1;                                                               \
  } while (0);

#define BD_ARM_STALL(bd, len)                                                  \
  do {                                                                         \
    bd.STAT = _STAT_DTSEN_MASK | _STAT_BSTALL_MASK;                            \
    bd.BC_L = len;                                                             \
    bd.UOWN = 1;                                                               \
  } while (0);

#define USB_SYNC_ACTIVE_FLAG()                                                 \
  do {                                                                         \
    usb_state.active = usb_state.configured && !usb_state.suspended &&         \
                       usb_state.line_state && !pipe_state.device_reset;       \
  } while (0);

void pipe_reset(void) {
  bds.ep1_in_even.UOWN = 0;
  bds.ep1_in_odd.UOWN = 0;
  bds.ep2_out_even.UOWN = 0;
  bds.ep2_out_odd.UOWN = 0;
  bds.ep2_in_even.UOWN = 0;
  bds.ep2_in_odd.UOWN = 0;
  bulk_in.cpu_dts = bulk_in.sie_dts;
  bulk_out.cpu_dts = bulk_out.sie_dts;
  pipe_state.flags = 0x00;
  bulk_in.cpu_odd = bulk_in.sie_odd;
  bulk_out.cpu_odd = bulk_out.sie_odd;
  USB_SYNC_ACTIVE_FLAG();
}

static void pipe_arm_rx() {
  if (bulk_out.cpu_odd) {
#ifdef DEBUG
    if (bulk_out.odd_busy) {
      panic("pipe_arm_rx: OUT odd already busy");
    }
#endif
    BD_ARM_DATA(bds.ep2_out_odd, bulk_out.cpu_dts, sizeof(ep2_out_odd_buffer));
    bulk_out.odd_busy = true;
  } else {
#ifdef DEBUG
    if (bulk_out.even_busy) {
      panic("pipe_arm_rx: OUT even already busy");
    }
#endif
    BD_ARM_DATA(bds.ep2_out_even, bulk_out.cpu_dts,
                sizeof(ep2_out_even_buffer));
    bulk_out.even_busy = true;
  }
  FLIP_BIT(bulk_out.cpu_odd);
  FLIP_BIT(bulk_out.cpu_dts);
}

void pipe_rx_acquire(void) {
  if (!pipe_state.rx_slice_valid) {
    BEGIN_CRITICAL_SECTION();
    if (bulk_out.cpu_odd && !bulk_out.odd_busy) {
      pipe_state.rx_slice.len = bds.ep2_out_odd.BC_L;
      pipe_state.rx_slice.ptr = ep2_out_odd_buffer;
      pipe_state.rx_slice_valid = true;
    } else if (!bulk_out.even_busy) {
      pipe_state.rx_slice.len = bds.ep2_out_even.BC_L;
      pipe_state.rx_slice.ptr = ep2_out_even_buffer;
      pipe_state.rx_slice_valid = true;
    }
    END_CRITICAL_SECTION();
  } else if (pipe_state.rx_slice.len == 0) {
    BEGIN_CRITICAL_SECTION();
    pipe_arm_rx();
    if (bulk_out.cpu_odd && !bulk_out.odd_busy) {
      pipe_state.rx_slice.len = bds.ep2_out_odd.BC_L;
      pipe_state.rx_slice.ptr = ep2_out_odd_buffer;
      pipe_state.rx_slice_valid = true;
    } else if (!bulk_out.even_busy) {
      pipe_state.rx_slice.len = bds.ep2_out_even.BC_L;
      pipe_state.rx_slice.ptr = ep2_out_even_buffer;
      pipe_state.rx_slice_valid = true;
    } else {
      pipe_state.rx_slice_valid = false;
    }
    END_CRITICAL_SECTION();
  }
}

void pipe_tx_acquire() {
  if (!pipe_state.tx_slice_valid) {
    BEGIN_CRITICAL_SECTION();
    if (bulk_in.cpu_odd && !bulk_in.odd_busy) {
      pipe_state.tx_slice.len = sizeof(ep2_in_odd_buffer);
      pipe_state.tx_slice.ptr = ep2_in_odd_buffer;
      pipe_state.tx_slice_valid = true;
    } else if (!bulk_in.even_busy) {
      pipe_state.tx_slice.len = sizeof(ep2_in_even_buffer);
      pipe_state.tx_slice.ptr = ep2_in_even_buffer;
      pipe_state.tx_slice_valid = true;
    }
    END_CRITICAL_SECTION();
  } else if (pipe_state.tx_slice.len == 0) {
    pipe_tx_flush();
  }
}

void pipe_tx_flush() {
  BEGIN_CRITICAL_SECTION();
#ifdef DEBUG
  if (!pipe_state.tx_slice_valid) {
    panic("pipe_tx_flush: no valid slice");
  }
#endif
  uint8_t len = EP2_PACKET_SIZE - pipe_state.tx_slice.len;
  if (bulk_in.cpu_odd) {
#ifdef DEBUG
    if (bulk_in.odd_busy) {
      panic("pipe_tx_flush: IN odd already busy");
    }
#endif
    BD_ARM_DATA(bds.ep2_in_odd, bulk_in.cpu_dts, len);
    bulk_in.odd_busy = true;
  } else {
#ifdef DEBUG
    if (bulk_in.even_busy) {
      panic("pipe_tx_flush: IN even already busy");
    }
#endif
    BD_ARM_DATA(bds.ep2_in_even, bulk_in.cpu_dts, len);
    bulk_in.even_busy = true;
  }
  pipe_state.tx_need_zlp = (len == EP2_PACKET_SIZE);
  FLIP_BIT(bulk_in.cpu_odd);
  FLIP_BIT(bulk_in.cpu_dts);

  if (bulk_in.cpu_odd && !bulk_in.odd_busy) {
    pipe_state.tx_slice.len = sizeof(ep2_in_odd_buffer);
    pipe_state.tx_slice.ptr = ep2_in_odd_buffer;
    pipe_state.tx_slice_valid = true;
  } else if (!bulk_in.even_busy) {
    pipe_state.tx_slice.len = sizeof(ep2_in_even_buffer);
    pipe_state.tx_slice.ptr = ep2_in_even_buffer;
    pipe_state.tx_slice_valid = true;
  } else {
    pipe_state.tx_slice_valid = false;
  }
  END_CRITICAL_SECTION();
}

static void ep0_stall(void) {
  ep0_state.tx_statf = TX_STATF_NONE;
  BD_ARM_STALL(bds.ep0_out, sizeof(ep0_out_buffer));
  BD_ARM_STALL(bds.ep0_in, 0x00);
}

static uint8_t ep0_in_fill_buffer(void) {
  uint8_t len = (uint8_t)MIN(ep0_state.data_len, sizeof(ep0_in_buffer));
  if (ep0_state.data_rom) {
    for (uint8_t i = 0; i < len; i++) {
      ep0_in_buffer[i] = *ep0_state.rom_in_ptr++;
    }
  } else if (ep0_state.data_ram) {
    for (uint8_t i = 0; i < len; i++) {
      ep0_in_buffer[i] = *ep0_state.ram_in_ptr++;
    }
  }
  return len;
}

static void ep0_out_process_buffer(uint8_t len) {
  if (ep0_state.data_ram) {
    for (uint8_t i = 0; i < len; i++) {
      *ep0_state.ram_out_ptr++ = ep0_out_buffer[i];
    }
  }
}

static void usb_reset_uie(void) {
  switch (usb_state.kind) {
  case USB_STATE_DETACHED:
    UIE = 0x00;
    break;
  default:
    UIE = _UIE_IDLEIE_MASK | _UIE_TRNIE_MASK | _UIE_UERRIE_MASK |
          _UIE_URSTIE_MASK;
    break;
  }
}

void usb_reset(void) {
  usb_state.flags = 0x00;
  memset((void *)&ep0_state, 0, sizeof(struct Ep0State));
  bulk_in.flags = 0x00;
  bulk_out.flags = 0x00;
  pipe_reset();

  // clear USB interrupts
  UEIR = 0x00;
  UIR = 0x00;
  UEIE = 0x00;
  UIE = 0x00;
  PIE3bits.USBIE = 0;
  // use full speed mode with pullups
  UCFG = _UCFG_UPUEN_MASK | _UCFG_FSEN_MASK | _UCFG_PPB1_MASK | _UCFG_PPB0_MASK;

  // clear buffer descriptors
  memset((void *)&bds, 0, sizeof(struct BufferDescriptors));

  // reset ping pong pointers
  UCONbits.PPBRST = 1;
  // clear USB address
  UADDR = 0x00;
  // enable packet processing
  UCONbits.PKTDIS = 0;
  // stop resetting ping pong pointers
  UCONbits.PPBRST = 0;

  // flush transactions
  while (UIRbits.TRNIF) {
    UIRbits.TRNIF = 0;
    // > 24.2.3 USB status register
    // > If the next data in the FIFO holding register is valid, the SIE will reassert the interrupt within
    // > 6 Tcy of clearing TRNIF
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
  }

  // initialize EP0
  bds.ep0_out.ADR = (uint16_t)&ep0_out_buffer;
  bds.ep0_in.ADR = (uint16_t)&ep0_in_buffer;
  UEP0 = _UEP0_EPHSHK_MASK | _UEP0_EPOUTEN_MASK | _UEP0_EPINEN_MASK;

  // initialize EP1 (CDC comm interface, interrupt endpoint)
  bds.ep1_in_even.ADR = (uint16_t)&ep1_in_even_buffer;
  bds.ep1_in_odd.ADR = (uint16_t)&ep1_in_odd_buffer;
  UEP1 = _UEP1_EPHSHK_MASK | _UEP1_EPCONDIS_MASK | _UEP1_EPINEN_MASK;

  // initialize EP2 (CDC data interface, bidirectional bulk endpoint)
  bds.ep2_out_even.ADR = (uint16_t)&ep2_out_even_buffer;
  bds.ep2_out_odd.ADR = (uint16_t)&ep2_out_odd_buffer;
  bds.ep2_in_even.ADR = (uint16_t)&ep2_in_even_buffer;
  bds.ep2_in_odd.ADR = (uint16_t)&ep2_in_odd_buffer;
  UEP2 = _UEP2_EPHSHK_MASK | _UEP2_EPCONDIS_MASK | _UEP2_EPOUTEN_MASK |
         _UEP2_EPINEN_MASK;

  BD_ARM_STALL(bds.ep0_out, sizeof(ep0_out_buffer));
  BD_ARM_STALL(bds.ep0_in, 0x00);
  BD_ARM_STALL(bds.ep1_in_even, 0);
  BD_ARM_STALL(bds.ep1_in_odd, 0);
  BD_ARM_STALL(bds.ep2_out_even, 0);
  BD_ARM_STALL(bds.ep2_out_odd, 0);
  BD_ARM_STALL(bds.ep2_in_even, 0);
  BD_ARM_STALL(bds.ep2_in_odd, 0);

  UEIE = 0xff;
  usb_reset_uie();

  PIR3bits.USBIF = 0;
  PIE3bits.USBIE = 1;
}

static void usb_switch_state(enum UsbStateKind target) {
  switch (target) {
  case USB_STATE_DETACHED:
  case USB_STATE_POWERED:
    UIR = 0x00;
    break;
  default:
    break;
  }
  usb_state.kind = target;
  usb_reset_uie();
}

void usb_detach(void) {
  usb_switch_state(USB_STATE_DETACHED);
  usb_state.flags = 0x00;

  // > 24.2 USB Status and Control
  // > when disabling the USB module, make sure the SUSPND bit is clear prior to clearing the USBEN bit"
  UCONbits.SUSPND = 0;
  UCON = 0x00;
  UCFG = 0x00;

  // reset all endpoints
  UEP0 = 0x00;
  UEP1 = 0x00;
  UEP2 = 0x00;
  UEP3 = 0x00;
  UEP4 = 0x00;
  UEP5 = 0x00;
  UEP6 = 0x00;
  UEP7 = 0x00;
  UEP8 = 0x00;
  UEP9 = 0x00;
  UEP10 = 0x00;
  UEP11 = 0x00;
  UEP12 = 0x00;
  UEP13 = 0x00;
  UEP14 = 0x00;
  UEP15 = 0x00;
}

void usb_init(void) {
  memset((void *)&usb_state, 0, sizeof(struct UsbState));
  usb_reset();
  usb_detach();
}

void usb_attach(void) {
  // > 24.2 USB Status and Control
  // > when disabling the USB module, make sure the SUSPND bit is clear prior to clearing the USBEN bit"
  UCONbits.SUSPND = 0;
  UCON = 0x00;
  do {
    UCONbits.USBEN = 1;
  } while (!UCONbits.USBEN);
  usb_switch_state(USB_STATE_ATTACHED);
}

static UIEbits_t uie_save;

static void usb_suspend(void) {
  usb_state.suspended = true;
  USB_SYNC_ACTIVE_FLAG();
  uie_save = UIEbits;
  UIE = _UIE_ACTVIE_MASK;
  UIRbits.IDLEIF = 0;
  UCONbits.SUSPND = 1;
  osc_switch_slow();
}

static void usb_resume(void) {
  osc_switch_fast();
  UCONbits.SUSPND = 0;
  do {
    UIRbits.ACTVIF = 0;
  } while (UIRbits.ACTVIF);
  UIEbits = uie_save;
  usb_state.suspended = false;
  USB_SYNC_ACTIVE_FLAG();
}

static enum TxStatf usb_service_ep0_get_descriptor() {
  uint8_t descriptorType = ep0_setup.wValue >> 8;
  uint8_t descriptorIndex = (uint8_t)ep0_setup.wValue;
  switch (descriptorType) {
  case USB_DESCRIPTOR_DEVICE:
    if (descriptorIndex != 0)
      return TX_STATF_NONE;
    ep0_state.data_len = DEVICE_DESCRIPTOR.bLength;
    ep0_state.rom_in_ptr = (const uint8_t *)&DEVICE_DESCRIPTOR;
    break;
  case USB_DESCRIPTOR_CONFIGURATION:
    if (descriptorIndex != 0)
      return TX_STATF_NONE;
    ep0_state.data_len = CONFIG_DESCRIPTOR.config.wTotalLength;
    ep0_state.rom_in_ptr = (const uint8_t *)&CONFIG_DESCRIPTOR;
    break;
  case USB_DESCRIPTOR_STRING:
    switch (descriptorIndex) {
    case 0:
      ep0_state.data_len = STRING_DESCRIPTOR0.header.bLength;
      ep0_state.rom_in_ptr = (const uint8_t *)&STRING_DESCRIPTOR0;
      break;
    case 1:
      ep0_state.data_len = STRING_DESCRIPTOR1.header.bLength;
      ep0_state.rom_in_ptr = (const uint8_t *)&STRING_DESCRIPTOR1;
      break;
    case 2:
      ep0_state.data_len = STRING_DESCRIPTOR2.header.bLength;
      ep0_state.rom_in_ptr = (const uint8_t *)&STRING_DESCRIPTOR2;
      break;
    default:
      return TX_STATF_NONE;
    }
    break;
  default:
    return TX_STATF_NONE;
  }
  return TX_STATF_DIR_IN | TX_STATF_DATA_STAGE | TX_STATF_DATA_ROM;
}

static void usb_config_deactivate(void) {
  pipe_reset();
  usb_state.configured = false;
  USB_SYNC_ACTIVE_FLAG();
}

static void usb_config_activate(void) {
  usb_state.configured = true;
  USB_SYNC_ACTIVE_FLAG();
  pipe_reset();
  bulk_out.cpu_dts = false;
  bulk_out.sie_dts = false;
  bulk_in.cpu_dts = false;
  bulk_in.sie_dts = false;
  pipe_arm_rx();
  pipe_arm_rx();
}

static void ep0_handle_setup() {
  bds.ep0_out.UOWN = 0;
  bds.ep0_out.cpu.DTS = 1;
  bds.ep0_in.UOWN = 0;
  bds.ep0_in.cpu.DTS = 1;
  ep0_state.tx_statf = TX_STATF_NONE;
  ep0_state.data_len = 0;
  uint8_t request = ep0_setup.bRequest;
  uint8_t bmRequestType = ep0_setup.bmRequestType;
  if ((bmRequestType & 0b01100000) == 0b00000000) {
    // standard request
    switch (request) {
    case GET_STATUS:
      if (bmRequestType == 0x80) {
        ep0_state.data_len = 2;
        ep0_in_buffer[0] = 0x00;
        ep0_in_buffer[1] = 0x00;
        ep0_state.tx_statf = TX_STATF_DATA_STAGE | TX_STATF_DIR_IN;
      }
      break;
    case GET_DESCRIPTOR:
      if (bmRequestType == 0x80) {
        ep0_state.tx_statf = usb_service_ep0_get_descriptor();
      }
      break;
    case SET_ADDRESS:
      if (bmRequestType == 0x00) {
        ep0_state.addr = (uint8_t)ep0_setup.wValue;
        ep0_state.tx_statf = TX_STATF_SET_ADDR | TX_STATF_DIR_OUT;
      }
      break;
    case GET_CONFIGURATION:
      if (bmRequestType == 0x80) {
        ep0_state.data_len = 1;
        ep0_in_buffer[0] = usb_state.configured ? 1 : 0;
        ep0_state.tx_statf = TX_STATF_DATA_STAGE | TX_STATF_DIR_IN;
      }
      break;
    case SET_CONFIGURATION:
      if (bmRequestType == 0x00) {
        switch (ep0_setup.wValue) {
        case 0:
          usb_config_deactivate();
          ep0_state.tx_statf = TX_STATF_DIR_OUT;
          break;
        case 1:
          usb_config_activate();
          ep0_state.tx_statf = TX_STATF_DIR_OUT;
          break;
        default:
          break;
        }
      }
      break;
    case GET_INTERFACE:
      if (bmRequestType == 0x81 && usb_state.configured) {
        switch (ep0_setup.wIndex) {
        case 0:
        case 1:
          ep0_state.data_len = 1;
          ep0_in_buffer[0] = 0x00;
          ep0_state.tx_statf = TX_STATF_DATA_STAGE | TX_STATF_DIR_IN;
          break;
        default:
          break;
        }
      }
      break;
    case SET_INTERFACE:
      if (bmRequestType == 0x01 && usb_state.configured) {
        switch (ep0_setup.wIndex) {
        case 0:
        case 1:
          if (ep0_setup.wValue == 0) {
            ep0_state.tx_statf = TX_STATF_DIR_OUT;
          }
          break;
        default:
          break;
        }
      }
      break;
    default:
      break;
    }
  } else if ((bmRequestType & 0b01100000) == 0b00100000 &&
             usb_state.configured) {
    // class request
    switch (request) {
    case CDC_SEND_ENCAPSULATED_COMMAND:
      if (bmRequestType == 0x21 && ep0_setup.wIndex == 0) {
        ep0_state.tx_statf = TX_STATF_DIR_OUT;
      }
      break;
    case CDC_GET_ENCAPSULATED_RESPONSE:
      if (bmRequestType == 0xa1 && ep0_setup.wIndex == 0) {
        ep0_state.data_len = 0;
        ep0_state.tx_statf = TX_STATF_DIR_IN | TX_STATF_DATA_STAGE;
      }
      break;
    case CDC_SET_LINE_CODING:
      if (bmRequestType == 0x21 && ep0_setup.wIndex == 0) {
        ep0_state.data_len = sizeof(struct LineCoding);
        ep0_state.ram_out_ptr = (uint8_t *)&line_coding;
        ep0_state.tx_statf =
            TX_STATF_DIR_OUT | TX_STATF_DATA_STAGE | TX_STATF_DATA_RAM;
      }
      break;
    case CDC_GET_LINE_CODING:
      if (bmRequestType == 0xa1 && ep0_setup.wIndex == 0) {
        ep0_state.data_len = sizeof(struct LineCoding);
        ep0_state.ram_in_ptr = (const uint8_t *)&line_coding;
        ep0_state.tx_statf =
            TX_STATF_DIR_IN | TX_STATF_DATA_STAGE | TX_STATF_DATA_RAM;
      }
      break;
    case CDC_SET_CONTROL_LINE_STATE:
      if (bmRequestType == 0x21 && ep0_setup.wIndex == 0) {
        usb_state.line_state = (ep0_setup.wValue & (1 << 0)) ? true : false;
        USB_SYNC_ACTIVE_FLAG();
        ep0_state.tx_statf = TX_STATF_DIR_OUT;
      }
      break;
    }
  } else if ((bmRequestType & 0b01100000) == 0b01000000) {
    // vendor request
    switch (request) {
    case VENDOR_RESET:
      if (bmRequestType == 0x40) {
        pipe_state.reset_magic = (uint8_t)ep0_setup.wValue;
        pipe_state.device_reset = true;
        USB_SYNC_ACTIVE_FLAG();
        ep0_state.tx_statf = TX_STATF_DIR_OUT;
      }
      break;
    case VENDOR_IDENTIFY:
      if (bmRequestType == 0xc0) {
        ep0_state.data_len = 5;
        ep0_in_buffer[0] = 0x99;
        ep0_in_buffer[1] = (uint8_t)bootloader_version;
        ep0_in_buffer[2] = bootloader_version >> 8;
        ep0_in_buffer[3] = FW_MINOR_VERSION;
        ep0_in_buffer[4] = FW_MAJOR_VERSION;
        ep0_state.tx_statf = TX_STATF_DIR_IN | TX_STATF_DATA_STAGE;
      }
      break;
    }
  }
  if (ep0_state.dir_in && ep0_state.data_stage) {
    ep0_state.data_len = MIN(ep0_state.data_len, ep0_setup.wLength);
    uint8_t len = ep0_in_fill_buffer();
    BD_ARM_DATA(bds.ep0_in, bds.ep0_in.cpu.DTS, len);
  } else if (ep0_state.dir_out) {
    if (ep0_state.data_stage) {
      BD_ARM_DATA(bds.ep0_out, bds.ep0_out.cpu.DTS, sizeof(ep0_out_buffer));
    } else {
      BD_ARM_STALL(bds.ep0_out, sizeof(ep0_out_buffer));
      BD_ARM_CTRL_STATUS(bds.ep0_in, 0x00);
    }
  } else {
    ep0_stall();
  }
}

static void ep0_in_transfer(void) {
  FLIP_BIT(bds.ep0_in.cpu.DTS);
  if (ep0_state.set_addr) {
    UADDR = ep0_state.addr;
    ep0_state.set_addr = false;
  }
  if (ep0_state.data_stage && ep0_state.dir_in) {
    ep0_state.data_len -= bds.ep0_in.BC_L;
    if (bds.ep0_in.BC_L < sizeof(ep0_in_buffer)) {
      ep0_state.data_stage = false;
      BD_ARM_CTRL_STATUS(bds.ep0_out, sizeof(ep0_out_buffer));
      BD_ARM_STALL(bds.ep0_in, 0x00);
    } else if (ep0_state.data_len > 0) {
      uint8_t len = ep0_in_fill_buffer();
      BD_ARM_DATA(bds.ep0_in, bds.ep0_in.cpu.DTS, len);
    } else {
      BD_ARM_DATA(bds.ep0_in, bds.ep0_in.cpu.DTS, 0x00);
    }
  } else {
    ep0_state.dir_in = false;
    BD_ARM_STALL(bds.ep0_in, 0x00);
  }
}

static void ep0_out_transfer(void) {
  FLIP_BIT(bds.ep0_out.cpu.DTS);
  if (bds.ep0_out.sie.PID == PID_SETUP) {
    ep0_handle_setup();
    UCONbits.PKTDIS = 0;
  } else if (ep0_state.data_stage && ep0_state.dir_out) {
    uint8_t len = (uint8_t)MIN(ep0_state.data_len, bds.ep0_out.BC_L);
    ep0_out_process_buffer(len);
    ep0_state.data_len -= len;
    if (bds.ep0_out.BC_L < sizeof(ep0_out_buffer)) {
      ep0_state.data_stage = false;
      BD_ARM_STALL(bds.ep0_out, sizeof(ep0_out_buffer));
      BD_ARM_CTRL_STATUS(bds.ep0_in, 0x00);
    } else {
      BD_ARM_DATA(bds.ep0_out, bds.ep0_out.cpu.DTS, sizeof(ep0_out_buffer));
    }
  } else {
    ep0_state.dir_out = false;
    BD_ARM_STALL(bds.ep0_out, sizeof(ep0_out_buffer));
  }
}

void usb_isr(void) {
  if (usb_state.suspended) {
    if (UIEbits.ACTVIE && UIRbits.ACTVIF) {
      usb_resume();
    } else {
      PIR3bits.USBIF = 0;
      return;
    }
  }
  if (UIEbits.URSTIE && UIRbits.URSTIF) {
    usb_switch_state(USB_STATE_DEFAULT);
    usb_reset();
  }
  switch (usb_state.kind) {
  case USB_STATE_DETACHED:
    break;
  case USB_STATE_POWERED:
    break;
  case USB_STATE_ATTACHED:
    if (!UCONbits.SE0) {
      usb_switch_state(USB_STATE_POWERED);
    }
    break;
  case USB_STATE_DEFAULT:
    while (UIEbits.TRNIE && UIRbits.TRNIF) {
      USTATbits_t stat = USTATbits;
      UIRbits.TRNIF = 0;
      switch (stat.ENDP) {
      case 0:
        if (stat.DIR) {
          ep0_in_transfer();
        } else {
          ep0_out_transfer();
        }
        break;
      case 2:
        if (stat.DIR) {
          // EP2 IN
          FLIP_BIT(bulk_in.sie_dts);
          FLIP_BIT(bulk_in.sie_odd);
          if (stat.PPBI) {
            bulk_in.odd_busy = false;
          } else {
            bulk_in.even_busy = false;
          }
        } else {
          // EP2 OUT
          FLIP_BIT(bulk_out.sie_dts);
          FLIP_BIT(bulk_out.sie_odd);
          if (stat.PPBI) {
            bulk_out.odd_busy = false;
          } else {
            bulk_out.even_busy = false;
          }
        }
        break;
      default:
#ifdef DEBUG
        panic("usb_isr: unexpected transfer for unsupported endpoint");
#endif
        // > 24.2.3 USB status register
        // > If the next data in the FIFO holding register is valid, the SIE will reassert the interrupt within
        // > 6 Tcy of clearing TRNIF
        NOP();
        NOP();
        NOP();
        NOP();
        NOP();
        NOP();
        break;
      }
    }
    break;
  }
  if (UIEbits.UERRIE && UIRbits.UERRIF) {
    if (UEIRbits.PIDEF) {
      diagnostics.usb_errors.pid_flag = 1;
      diagnostics.usb_errors.pid_cnt += 1;
    }
    if (UEIRbits.CRC5EF) {
      diagnostics.usb_errors.crc5_flag = 1;
      diagnostics.usb_errors.crc5_cnt += 1;
    }
    if (UEIRbits.CRC16EF) {
      diagnostics.usb_errors.crc16_flag = 1;
      diagnostics.usb_errors.crc16_cnt += 1;
    }
    if (UEIRbits.DFN8EF) {
      diagnostics.usb_errors.dfn8_flag = 1;
      diagnostics.usb_errors.dfn8_cnt += 1;
    }
    if (UEIRbits.BTOEF) {
      diagnostics.usb_errors.bto_flag = 1;
      diagnostics.usb_errors.bto_cnt += 1;
    }
    if (UEIRbits.BTSEF) {
      diagnostics.usb_errors.bts_flag = 1;
      diagnostics.usb_errors.bts_cnt += 1;
    }
    UEIR = 0;
  }
  if (UIEbits.IDLEIE && UIRbits.IDLEIF) {
    UIRbits.IDLEIF = 0;
    usb_suspend();
  }
  PIR3bits.USBIF = 0;
}
