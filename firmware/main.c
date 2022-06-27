// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "config.h"

#include <stdbool.h>
#include <string.h>

#include "cmd_protocol.h"
#include "common.h"
#include "diagnostics.h"
#include "hardware.h"
#include "pipe.h"
#include "usb.h"

void __interrupt(high_priority) isr_high(void) {}

void __interrupt(low_priority) isr_low(void) { usb_isr(); }

volatile __bank(1) struct Diagnostics diagnostics;

struct CartMode {
  unsigned vcart : 1;
  unsigned reset : 1;
};

struct CommandHeader {
  uint8_t len;
  uint8_t bytes[16];
};

struct State {
  union {
    struct {
      unsigned unlocked : 1;
      unsigned : 7;
    };
    uint8_t global_flags;
  };
  union {
    struct {
      unsigned cmd_valid : 1;
      unsigned header_valid : 1;
      unsigned : 6;
    };
    uint8_t cmd_flags;
  };
  enum Command cmd;
  struct CommandHeader header;
  struct CartMode cart_mode;
  struct ReadSlice tx_read_slice;
  struct WriteSlice tx_write_slice;
  union {
    struct {
      struct {
        unsigned use_cs : 1;
      };
      uint16_t addr;
    } read;
    struct {
      struct {
        unsigned use_cs : 1;
        unsigned force_ce : 1;
      };
      uint16_t addr;
      uint16_t len;
    } read_burst;
    struct {
      struct {
        unsigned use_cs : 1;
        unsigned use_vin : 1;
      };
      uint16_t addr;
      uint8_t data;
    } write;
    struct {
      struct {
        unsigned use_cs : 1;
        unsigned use_vin : 1;
      };
      uint16_t addr;
      uint16_t len;
    } write_burst;
    struct {
      uint8_t magic;
    } reset;
    struct {
      uint8_t bytes[5];
    } identify;
    union {
      struct CartMode new_mode;
      uint8_t value;
    } set_mode;
    union {
      struct {
        struct CartMode mode;
        struct {
          unsigned reset_pin : 1;
        } pins;
      };
      uint8_t bytes[2];
    } get_mode;
    struct {
      uint16_t addr;
      union {
        struct {
          unsigned : 7;
          unsigned expected_d7 : 1;
        };
        uint8_t flags;
      };
      uint16_t counter;
    } poll_flash_data;
    struct {
      struct {
        unsigned : 6;
        unsigned use_vin : 1;
        unsigned : 1;
      };
      uint16_t addr;
      uint16_t len;
    } flash_burst;
  };
};

static struct State state;

struct Write {
  uint16_t addr;
  uint8_t data;
  union {
    struct {
      unsigned : 6;
      unsigned use_vin : 1;
      unsigned : 1;
    };
    uint8_t byte;
  };
};

struct WriteSequence {
  uint8_t len;
  struct Write write[16];
};

static struct WriteSequence flash_sequence = {0};

static void apply_cart_mode() {
  if (state.cart_mode.vcart) {
    VCART_EN_OUT(1);
    ADDR_BUS_OUT(0x0000);
    TRISA = 0x00;
    TRISC = 0x00;
    TRISD = 0x00;
    ANSELB = 0x00;
    ANSELEbits.ANSE0 = 0;
    ENABLE_DATA_BUS_PULLUPS();
    RD_PIN_OUT(0);
  } else {
    VCART_EN_OUT(0);
    ADDR_BUS_OUT(0x0000);
    TRISA = 0xff;
    TRISC = 0xff;
    TRISD = 0xff;
    ANSELB = 0xff;
    ANSELEbits.ANSE0 = 1;
    DISABLE_DATA_BUS_PULLUPS();
    RD_PIN_OUT(1);
  }
  if (state.cart_mode.reset) {
    RES_PIN_OUT(0);
  } else {
    RES_PIN_OUT(1);
  }
}

static void reset_state(void) {
  state.global_flags = 0;
  state.cmd_flags = 0;
  state.cart_mode.vcart = 0;
  state.cart_mode.reset = 1;
  apply_cart_mode();
  pipe_state.rx_slice_valid = false;
  pipe_state.rx_slice.ptr = NULL;
  pipe_state.rx_slice.len = 0;
  pipe_state.tx_slice_valid = false;
  pipe_state.tx_slice.ptr = NULL;
  pipe_state.tx_slice.len = 0;
}

static const uint8_t UNLOCK_MAGIC[16] = {0x0d, 0x68, 0xb7, 0xa3, 0x12, 0x1b,
                                         0x44, 0x13, 0xc2, 0x8a, 0xd0, 0xa4,
                                         0xd3, 0x95, 0xaf, 0x86};

static void fetch_command(void) {
  pipe_rx_acquire();
  if (!pipe_state.rx_slice_valid) {
    return;
  }
  if (!state.cmd_valid && pipe_state.rx_slice.len > 0) {
    state.cmd = *(pipe_state.rx_slice.ptr++);
    state.cmd_valid = true;
    state.header_valid = false;
    state.header.len = 0;
    pipe_state.rx_slice.len -= 1;
  }

  int8_t total_header_len = -1;
  switch (state.cmd & CMD_MASK) {
  case CMD_PING:
    total_header_len = 16;
    break;
  case CMD_UNLOCK:
    total_header_len = 16;
    break;
  case CMD_SET_MODE:
    if (state.unlocked) {
      total_header_len = 1;
    }
    break;
  case CMD_GET_MODE:
    if (state.unlocked) {
      total_header_len = 0;
    }
    break;
  case CMD_READ:
    if (state.unlocked) {
      total_header_len = 2;
    }
    break;
  case CMD_READ_BURST:
    if (state.unlocked) {
      total_header_len = 4;
    }
    break;
  case CMD_WRITE:
    if (state.unlocked) {
      total_header_len = 3;
    }
    break;
  case CMD_WRITE_BURST:
    if (state.unlocked) {
      total_header_len = 4;
    }
    break;
  case CMD_POLL_FLASH_DATA:
    if (state.unlocked) {
      total_header_len = 3;
    }
    break;
  case CMD_SET_FLASH_WRITE_SEQUENCE:
    if (state.unlocked) {
      total_header_len = 1;
    }
    break;
  case CMD_FLASH_BURST:
    if (state.unlocked) {
      total_header_len = 4;
    }
    break;
  case CMD_DIAGNOSTICS:
    total_header_len = 0;
    break;
  case CMD_RESET:
    if (state.unlocked) {
      total_header_len = 1;
    }
    break;
  case CMD_IDENTIFY:
    total_header_len = 0;
    break;
  default:
    break;
  }

  if (total_header_len < 0) {
    state.cmd_valid = false;
    return;
  }

  uint8_t needed_bytes = (uint8_t)total_header_len - state.header.len;
  uint8_t copied_bytes = MIN(pipe_state.rx_slice.len, needed_bytes);

  __ram uint8_t *dst = &state.header.bytes[state.header.len];

  for (uint8_t i = copied_bytes; i > 0; i--) {
    *(dst++) = *(pipe_state.rx_slice.ptr++);
  }

  state.header.len += copied_bytes;
  pipe_state.rx_slice.len -= copied_bytes;

  if (copied_bytes == needed_bytes) {
    state.header_valid = true;

    switch (state.cmd & CMD_MASK) {
    case CMD_PING:
      state.tx_read_slice.len = sizeof(state.header.bytes);
      state.tx_read_slice.ptr = state.header.bytes;
      break;
    case CMD_UNLOCK:
      if (memcmp(state.header.bytes, UNLOCK_MAGIC,
                 sizeof(state.header.bytes)) != 0) {
        state.cmd_valid = false;
        state.header_valid = false;
        return;
      }

      state.tx_read_slice.len = sizeof(state.header.bytes);
      state.tx_read_slice.ptr = state.header.bytes;
      state.unlocked = true;
      break;
    case CMD_SET_MODE:
      state.set_mode.value = state.header.bytes[0];
      break;
    case CMD_GET_MODE:
      state.get_mode.mode = state.cart_mode;
      state.get_mode.pins.reset_pin = RES_PIN_IN();
      state.tx_read_slice.len = sizeof(state.get_mode.bytes);
      state.tx_read_slice.ptr = state.get_mode.bytes;
      break;
    case CMD_READ:
      state.read.addr =
          U16_FROM_LE_BYTES(state.header.bytes[0], state.header.bytes[1]);
      state.read.use_cs = state.cmd & (1 << 7) ? true : false;
      break;
    case CMD_READ_BURST:
      state.read_burst.addr =
          U16_FROM_LE_BYTES(state.header.bytes[0], state.header.bytes[1]);
      state.read_burst.len =
          U16_FROM_LE_BYTES(state.header.bytes[2], state.header.bytes[3]);
      state.read_burst.use_cs = state.cmd & (1 << 7) ? true : false;
      state.read_burst.force_ce = state.cmd & (1 << 6) ? true : false;
      break;
    case CMD_WRITE:
      state.write.addr =
          U16_FROM_LE_BYTES(state.header.bytes[0], state.header.bytes[1]);
      state.write.data = state.header.bytes[2];
      state.write.use_cs = state.cmd & (1 << 7) ? true : false;
      state.write.use_vin = state.cmd & (1 << 6) ? true : false;
      break;
    case CMD_WRITE_BURST:
      state.write_burst.addr =
          U16_FROM_LE_BYTES(state.header.bytes[0], state.header.bytes[1]);
      state.write_burst.len =
          U16_FROM_LE_BYTES(state.header.bytes[2], state.header.bytes[3]);
      state.write_burst.use_cs = state.cmd & (1 << 7) ? true : false;
      state.write_burst.use_vin = state.cmd & (1 << 6) ? true : false;
      break;
    case CMD_POLL_FLASH_DATA:
      state.poll_flash_data.addr =
          U16_FROM_LE_BYTES(state.header.bytes[0], state.header.bytes[1]);
      state.poll_flash_data.flags = state.header.bytes[2];
      break;
    case CMD_SET_FLASH_WRITE_SEQUENCE: {
      uint8_t write_count = state.header.bytes[0];
      if (write_count > sizeof(flash_sequence.write)) {
        state.cmd_valid = false;
        state.header_valid = false;
        return;
      }
      flash_sequence.len = write_count;
      state.tx_write_slice.ptr = (uint8_t *)&flash_sequence.write;
      state.tx_write_slice.len = write_count * sizeof(struct Write);
      break;
    }
    case CMD_FLASH_BURST: {
      state.flash_burst.addr =
          U16_FROM_LE_BYTES(state.header.bytes[0], state.header.bytes[1]);
      state.flash_burst.len =
          U16_FROM_LE_BYTES(state.header.bytes[2], state.header.bytes[3]);
      state.flash_burst.use_vin = state.cmd & (1 << 6) ? true : false;
      state.tx_read_slice.len = sizeof(state.flash_burst.len);
      state.tx_read_slice.ptr = (const uint8_t *)&state.flash_burst.len;
      break;
    }
    case CMD_DIAGNOSTICS:
      state.tx_read_slice.ptr = (const __ram uint8_t *)&diagnostics;
      state.tx_read_slice.len = sizeof(diagnostics);
      break;
    case CMD_RESET:
      state.reset.magic = state.header.bytes[0];
      break;
    case CMD_IDENTIFY:
      state.identify.bytes[0] = 0x99;
      state.identify.bytes[1] = (uint8_t)bootloader_version;
      state.identify.bytes[2] = bootloader_version >> 8;
      state.identify.bytes[3] = FW_MINOR_VERSION;
      state.identify.bytes[4] = FW_MAJOR_VERSION;
      state.tx_read_slice.ptr = state.identify.bytes;
      state.tx_read_slice.len = sizeof(state.identify.bytes);
      break;
    default:
      break;
    }
    state.cmd &= CMD_MASK;
  }
}

static void reset_device(uint8_t magic) {
  reset_magic = magic;
  __delaywdt_ms(200);
  UCONbits.SUSPND = 0;
  UCONbits.USBEN = 0;
  __delaywdt_ms(200);
  RESET();
}

static inline uint8_t poll_flash_data(uint16_t addr, bool expected_d7) {
  uint8_t old_data;
  uint8_t new_data;
  PHI_PIN_OUT(1);
  ADDR_BUS_OUT(addr);
  A15_PIN_OUT(0);
  PHI_PIN_OUT(0);
  old_data = DATA_BUS_IN();
  A15_PIN_OUT(1);
  if (expected_d7) {
    while (true) {
      PHI_PIN_OUT(1);
      A15_PIN_OUT(0);
      PHI_PIN_OUT(0);
      new_data = DATA_BUS_IN();
      A15_PIN_OUT(1);
      if (new_data & 0x80) {
        if (old_data == new_data) {
          return new_data;
        } else {
          old_data = new_data;
        }
      }
      CLRWDT();
    }
  } else {
    while (true) {
      PHI_PIN_OUT(1);
      A15_PIN_OUT(0);
      PHI_PIN_OUT(0);
      new_data = DATA_BUS_IN();
      A15_PIN_OUT(1);
      if (!(new_data & 0x80)) {
        if (old_data == new_data) {
          return new_data;
        } else {
          old_data = new_data;
        }
      }
      CLRWDT();
    }    
  }
}

static bool execute_cmd_tx_read(void) {
  while (state.tx_read_slice.len > 0) {
    pipe_tx_acquire();
    if (!pipe_state.tx_slice_valid) {
      return false;
    }
    uint8_t len =
        (uint8_t)MIN(pipe_state.tx_slice.len, state.tx_read_slice.len);
    pipe_state.tx_slice.len -= len;
    state.tx_read_slice.len -= len;
    memcpy((void *)pipe_state.tx_slice.ptr,
           (const void *)state.tx_read_slice.ptr, len);
    pipe_state.tx_slice.ptr += len;
    state.tx_read_slice.ptr += len;
  }
  return true;
}

static bool execute_cmd_read(void) {
  pipe_tx_acquire();
  if (!pipe_state.tx_slice_valid) {
    return false;
  }
  if (state.read.use_cs) {
    READ_BYTE(state.read.addr, *pipe_state.tx_slice.ptr, CS_PIN_OUT,
              CS_PIN_OUT);
  } else {
    READ_BYTE(state.read.addr, *pipe_state.tx_slice.ptr, (void), A15_PIN_OUT);
  }
  pipe_state.tx_slice.ptr += 1;
  pipe_state.tx_slice.len -= 1;
  return true;
}

static bool execute_cmd_read_burst(void) {
  while (state.read_burst.len > 0) {
    pipe_tx_acquire();
    if (!pipe_state.tx_slice_valid) {
      return false;
    }
    uint8_t len = (uint8_t)MIN(pipe_state.tx_slice.len, state.read_burst.len);
    pipe_state.tx_slice.len -= len;
    state.read_burst.len -= len;
    uint8_t addr_l = (uint8_t)state.read_burst.addr;
    bool fast_path = (addr_l + len - 1) >= addr_l;
    if (fast_path && !state.read_burst.force_ce) {
      uint8_t addr_h = state.read_burst.addr >> 8;
      if (state.read_burst.use_cs) {
        CS_PIN_OUT(0);
      }
      ADDRH_BUS_OUT(addr_h);
      READ_BURST_FAST(addr_l, pipe_state.tx_slice.ptr, len);
      state.read_burst.addr += len;
      if (state.read_burst.use_cs) {
        CS_PIN_OUT(1);
      } else {
        A15_PIN_OUT(1);
      }
    } else {
      if (state.read_burst.use_cs) {
        READ_BURST(state.read_burst.addr, pipe_state.tx_slice.ptr, len,
                   CS_PIN_OUT, CS_PIN_OUT);
      } else {
        READ_BURST(state.read_burst.addr, pipe_state.tx_slice.ptr, len, (void),
                   A15_PIN_OUT);
      }
    }
    CLRWDT();
  }
  return true;
}

static void execute_cmd_write(void) {
  RD_PIN_OUT(1);
  if (state.write.use_cs) {
    if (state.write.use_vin) {
      WRITE_BYTE(state.write.addr, state.write.data, CS_PIN_OUT, CS_PIN_OUT,
                 VIN_PIN_OUT, VIN_PIN_OUT);
    } else {
      WRITE_BYTE(state.write.addr, state.write.data, CS_PIN_OUT, CS_PIN_OUT,
                 WR_PIN_OUT, WR_PIN_OUT);
    }
  } else {
    if (state.write.use_vin) {
      WRITE_BYTE(state.write.addr, state.write.data, (void), A15_PIN_OUT,
                 VIN_PIN_OUT, VIN_PIN_OUT);
    } else {
      WRITE_BYTE(state.write.addr, state.write.data, (void), A15_PIN_OUT,
                 WR_PIN_OUT, WR_PIN_OUT);
    }
  }
  RD_PIN_OUT(0);
}

static bool execute_cmd_write_burst(void) {
  while (state.write_burst.len > 0) {
    pipe_rx_acquire();
    if (!pipe_state.rx_slice_valid) {
      return false;
    }
    uint8_t len = (uint8_t)MIN(pipe_state.rx_slice.len, state.write_burst.len);
    pipe_state.rx_slice.len -= len;
    state.write_burst.len -= len;
    RD_PIN_OUT(1);
    if (state.write_burst.use_cs) {
      if (state.write_burst.use_vin) {
        for (uint8_t i = len; i > 0; i--) {
          WRITE_BYTE(state.write_burst.addr, *pipe_state.rx_slice.ptr,
                     CS_PIN_OUT, CS_PIN_OUT, VIN_PIN_OUT, VIN_PIN_OUT);
          pipe_state.rx_slice.ptr += 1;
          state.write_burst.addr += 1;
        }
      } else {
        for (uint8_t i = len; i > 0; i--) {
          WRITE_BYTE(state.write_burst.addr, *pipe_state.rx_slice.ptr,
                     CS_PIN_OUT, CS_PIN_OUT, WR_PIN_OUT, WR_PIN_OUT);
          pipe_state.rx_slice.ptr += 1;
          state.write_burst.addr += 1;
        }
      }
    } else {
      if (state.write_burst.use_vin) {
        for (uint8_t i = len; i > 0; i--) {
          WRITE_BYTE(state.write_burst.addr, *pipe_state.rx_slice.ptr, (void),
                     A15_PIN_OUT, VIN_PIN_OUT, VIN_PIN_OUT);
          pipe_state.rx_slice.ptr += 1;
          state.write_burst.addr += 1;
        }
      } else {
        for (uint8_t i = len; i > 0; i--) {
          WRITE_BYTE(state.write_burst.addr, *pipe_state.rx_slice.ptr, (void),
                     A15_PIN_OUT, WR_PIN_OUT, WR_PIN_OUT);
          pipe_state.rx_slice.ptr += 1;
          state.write_burst.addr += 1;
        }
      }
    }
    RD_PIN_OUT(0);
    CLRWDT();
  }
  return true;
}

static bool execute_cmd_poll_flash_data(void) {
  pipe_tx_acquire();
  if (!pipe_state.tx_slice_valid || pipe_state.tx_slice.len == 0) {
    return false;
  }
  uint8_t data = poll_flash_data(state.poll_flash_data.addr,
                                 state.poll_flash_data.expected_d7);
  *(pipe_state.tx_slice.ptr++) = data;
  pipe_state.tx_slice.len -= 1;
  return true;
}

static bool execute_cmd_set_flash_write_sequence(void) {
  while (state.tx_write_slice.len > 0) {
    pipe_rx_acquire();
    if (!pipe_state.rx_slice_valid) {
      return false;
    }
    uint8_t len =
        (uint8_t)MIN(pipe_state.rx_slice.len, state.tx_write_slice.len);
    pipe_state.rx_slice.len -= len;
    state.tx_write_slice.len -= len;
    memcpy((void *)state.tx_write_slice.ptr,
           (const void *)pipe_state.rx_slice.ptr, len);
    pipe_state.rx_slice.ptr += len;
    state.tx_write_slice.ptr += len;
    CLRWDT();
  }
  return true;
}

static bool execute_cmd_flash_burst(void) {
  while (state.flash_burst.len > 0) {
    pipe_rx_acquire();
    if (!pipe_state.rx_slice_valid) {
      return false;
    }
    uint8_t len = (uint8_t)MIN(pipe_state.rx_slice.len, state.flash_burst.len);
    pipe_state.rx_slice.len -= len;
    state.flash_burst.len -= len;
    for (uint8_t i = len; i > 0; i--) {
      uint8_t data = *(pipe_state.rx_slice.ptr++);
      if (data != 0xff) {
        RD_PIN_OUT(1);
        for (uint8_t j = 0; j < flash_sequence.len; j++) {
          if (flash_sequence.write[j].use_vin) {
            WRITE_BYTE(flash_sequence.write[j].addr,
                       flash_sequence.write[j].data, A15_PIN_OUT, A15_PIN_OUT,
                       VIN_PIN_OUT, VIN_PIN_OUT);
          } else {
            WRITE_BYTE(flash_sequence.write[j].addr,
                       flash_sequence.write[j].data, A15_PIN_OUT, A15_PIN_OUT,
                       WR_PIN_OUT, WR_PIN_OUT);
          }
        }
        uint16_t addr = state.flash_burst.addr++;
        if (state.flash_burst.use_vin) {
          WRITE_BYTE(addr, data, A15_PIN_OUT, A15_PIN_OUT, VIN_PIN_OUT,
                     VIN_PIN_OUT);
        } else {
          WRITE_BYTE(addr, data, A15_PIN_OUT, A15_PIN_OUT, WR_PIN_OUT,
                     WR_PIN_OUT);
        }
        RD_PIN_OUT(0);
        poll_flash_data(addr, data & 0x80);
      } else {
        state.flash_burst.addr += 1;
      }
    }
    CLRWDT();
  }
  return execute_cmd_tx_read();
}

static void execute_commands(void) {
  do {
    if (state.header_valid) {
      switch (state.cmd) {
      case CMD_PING:
      case CMD_UNLOCK:
      case CMD_GET_MODE:
      case CMD_DIAGNOSTICS:
      case CMD_IDENTIFY:
        if (!execute_cmd_tx_read()) {
          return;
        }
        break;
      case CMD_SET_MODE:
        state.cart_mode = state.set_mode.new_mode;
        apply_cart_mode();
        break;
      case CMD_READ:
        if (!execute_cmd_read()) {
          return;
        }
        break;
      case CMD_READ_BURST:
        if (!execute_cmd_read_burst()) {
          return;
        }
        break;
      case CMD_WRITE: {
        execute_cmd_write();
        break;
      }
      case CMD_WRITE_BURST: {
        if (!execute_cmd_write_burst()) {
          return;
        }
        break;
      }
      case CMD_POLL_FLASH_DATA: {
        if (!execute_cmd_poll_flash_data()) {
          return;
        }
        break;
      }
      case CMD_SET_FLASH_WRITE_SEQUENCE: {
        if (!execute_cmd_set_flash_write_sequence()) {
          return;
        }
        break;
      }
      case CMD_FLASH_BURST: {
        if (!execute_cmd_flash_burst()) {
          return;
        }
        break;
      }
      case CMD_RESET:
        reset_device(state.reset.magic);
        break;
      default:
        break;
      }
      state.cmd_flags = 0;
    }
    fetch_command();
    CLRWDT();
  } while (state.header_valid);
}

static uint16_t capture_res_pin_voltage(void) {
  PMD1bits.ADCMD = 0;

  ADCON2bits.ADFM = 1;
  ADCON2bits.ACQT = 0b111;
  ADCON2bits.ADCS = 0b110;
  ADCON1 = 0x00;
  ADCON0bits.ADON = 1;
  ADCON0bits.CHS = 5;
  ADCON0bits.GO = 1;

  while (ADCON0bits.GO) {
    NOP();
  }

  uint16_t value = (uint16_t)((ADRESH & 0b11) << 8) | ADRESL;
  ADCON0 = 0x00;
  PMD1bits.ADCMD = 1;
  return value;
}

static void init(void) {
  uint8_t rcon_save = RCON;
  uint8_t stkptr_save = STKPTR;

  reset_magic = 0x00;
  RCON = _RCON_IPEN_MASK | _RCON_RI_MASK | _RCON_TO_MASK | _RCON_PD_MASK |
         _RCON_POR_MASK | _RCON_BOR_MASK;
  STKPTRbits.STKFUL = 0;
  STKPTRbits.STKUNF = 0;

  LATE = 0x00;
  TRISA = 0xff;
  TRISB = 0xff;
  TRISC = 0xff;
  TRISD = 0xff;
  TRISE = (uint8_t) ~(_TRISE_RE2_MASK); // enable VCART EN output
  ANSELA = 0xff;
  ANSELB = 0xff;
  ANSELC = 0xff;
  ANSELD = 0xff;
  ANSELE = 0xff;

  DISABLE_DATA_BUS_PULLUPS();
  PHI_PIN_OUT(0);
  WR_PIN_OUT(1);
  RD_PIN_OUT(1);
  CS_PIN_OUT(1);
  ADDR_BUS_OUT(0x8000);
  DATA_BUS_OUT(0);
  RES_PIN_OUT(0);
  VIN_PIN_OUT(1);

  VREGCON = 0x00;
  VREGCONbits.VREGPM = 0b11;
  ACTCON = _ACTCON_ACTSRC_MASK;
  OSCCON = _OSCCON_IDLEN_MASK;
  OSCCON2 = 0x00;
  OSCTUNEbits.SPLLMULT = 1;
  osc_init();

  PMD0 = (uint8_t) ~(_PMD0_USBMD_MASK | _PMD0_ACTMD_MASK);
  PMD1 = 0xFF;

  INTCON2 = 0xff;
  INTCON3 = _INTCON3_INT2IP_MASK | _INTCON3_INT1IP_MASK;
  PIR1 = 0x00;
  PIR2 = 0x00;
  PIR3 = 0x00;
  PIE1 = 0x00;
  PIE2 = 0x00;
  PIE3 = 0x00;
  IPR1 = 0xff;
  IPR2 = 0xff;
  IPR3 = 0xff;

  memset((void *)&diagnostics, 0, sizeof(struct Diagnostics));
  diagnostics.initial_rcon = rcon_save;
  diagnostics.initial_stkptr = stkptr_save;
  diagnostics.initial_res_voltage = capture_res_pin_voltage();
}

const uint16_t BOOTLOADER_THRESHOLD_VOLTAGE = 600; // ~2.9V

void main(void) {
  INTCON = 0x00;
  init();

  if (diagnostics.initial_res_voltage > BOOTLOADER_THRESHOLD_VOLTAGE) {
    if (capture_res_pin_voltage() > BOOTLOADER_THRESHOLD_VOLTAGE) {
      reset_device(0x42);
    }
  }

  usb_init();
  memset((void *)&state, 0, sizeof(struct State));
  reset_state();

  IPR3bits.USBIP = 0;
  INTCONbits.GIE = 1;
  INTCONbits.PEIE = 1;

  usb_attach();

  while (true) {
    while (usb_state.suspended) {
      SLEEP();
    }
    while (!usb_state.active) {
      CLRWDT();
      if (pipe_state.device_reset) {
        reset_device(pipe_state.reset_magic);
      }
    }
    while (usb_state.active) {
      CLRWDT();
      execute_commands();
      if (pipe_state.tx_slice_valid) {
        if (pipe_state.tx_slice.len < EP2_PACKET_SIZE ||
            pipe_state.tx_need_zlp) {
          pipe_tx_flush();
        }
      } else if (pipe_state.tx_need_zlp) {
        pipe_tx_acquire();
      }
    }
    reset_state();
  }
}

#define MAX_PANIC_MESSAGE_LEN 128
char panic_message[MAX_PANIC_MESSAGE_LEN] __at(0x700);

#ifdef DEBUG
void __attribute__((noreturn)) panic(const char *msg) {
  strncpy(panic_message, msg, MAX_PANIC_MESSAGE_LEN);
  NOP();
  while (true) {
    NOP();
  }
}
#endif
