// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef USB_H
#define USB_H

#include <stdbool.h>

#define EP0_PACKET_SIZE 64
#define EP1_PACKET_SIZE 8
#define EP2_PACKET_SIZE 64

struct BufferDescriptor {
  union {
    struct {
      unsigned : 7;
      unsigned UOWN : 1;
    };
    struct {
      unsigned BC_H : 2;
      unsigned BSTALL : 1;
      unsigned DTSEN : 1;
      unsigned : 2;
      unsigned DTS : 1;
      unsigned UOWN : 1;
    } cpu;
    struct {
      unsigned BC_H : 2;
      unsigned PID : 4;
      unsigned /* reserved */ : 1;
      unsigned UOWN : 1;
    } sie;
    uint8_t STAT;
  };
  uint8_t BC_L;
  uint16_t ADR;
};

struct BufferDescriptors {
  struct BufferDescriptor ep0_out;
  struct BufferDescriptor ep0_in;
  struct BufferDescriptor _ep1_out_even;
  struct BufferDescriptor _ep1_out_odd;
  struct BufferDescriptor ep1_in_even;
  struct BufferDescriptor ep1_in_odd;
  struct BufferDescriptor ep2_out_even;
  struct BufferDescriptor ep2_out_odd;
  struct BufferDescriptor ep2_in_even;
  struct BufferDescriptor ep2_in_odd;
};

extern volatile struct BufferDescriptors bds;
extern volatile uint8_t ep0_out_buffer[EP0_PACKET_SIZE];
extern volatile uint8_t ep0_in_buffer[EP0_PACKET_SIZE];
extern volatile uint8_t ep1_in_even_buffer[EP1_PACKET_SIZE];
extern volatile uint8_t ep1_in_odd_buffer[EP1_PACKET_SIZE];
extern volatile uint8_t ep2_out_even_buffer[EP2_PACKET_SIZE];
extern volatile uint8_t ep2_out_odd_buffer[EP2_PACKET_SIZE];
extern volatile uint8_t ep2_in_even_buffer[EP2_PACKET_SIZE];
extern volatile uint8_t ep2_in_odd_buffer[EP2_PACKET_SIZE];

enum UsbStateKind {
  USB_STATE_DETACHED,
  USB_STATE_ATTACHED,
  USB_STATE_POWERED,
  USB_STATE_DEFAULT
};

struct UsbState {
  enum UsbStateKind kind;
  union {
    struct {
      unsigned suspended : 1;
      unsigned configured : 1;
      unsigned active : 1;
      unsigned line_state : 1;
      unsigned : 4;
    };
    uint8_t flags;
  };
  uint8_t sof;
};

extern volatile struct UsbState usb_state;

void usb_init(void);
void usb_detach(void);
void usb_attach(void);
void usb_isr(void);

#endif /* USB_H */
