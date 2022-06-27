// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef USB_PROTOCOL_H
#define USB_PROTOCOL_H

#include <stdint.h>

// Table 8-1: PID Types
enum UsbPid {
  PID_OUT = 0b0001,
  PID_IN = 0b1001,
  PID_SOF = 0b0101,
  PID_SETUP = 0b1101,
  PID_DATA0 = 0b0011,
  PID_DATA1 = 0b1011,
  PID_DATA2 = 0b0111,
  PID_MDATA = 0b1111,
  PID_ACK = 0b0010,
  PID_NAK = 0b1010,
  PID_STALL = 0b1110,
  PID_NYET = 0b0110,
  PID_PRE = 0b1100,
  PID_ERR = 0b1100,
  PID_SPLIT = 0b1000,
  PID_PING = 0b0100
};

// Table 9-2: Format of Setup Data
struct UsbSetupPacket {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
};

// Table 9-2: Format of Setup Data
enum UsbRequestType {
  USB_REQUEST_STANDARD = 0,
  USB_REQUEST_CLASS = 1,
  USB_REQUEST_VENDOR = 2
};

// Table 9-2: Format of Setup Data
enum UsbRequestRecipient {
  USB_RECIPIENT_DEVICE = 0,
  USB_RECIPIENT_INTERFACE = 1,
  USB_RECIPIENT_ENDPOINT = 2,
  USB_RECIPIENT_OTHER = 3
};

// Table 9-4: Standard Request Codes
enum UsbStdRequest {
  GET_STATUS = 0,
  CLEAR_FEATURE = 1,
  SET_FEATURE = 3,
  SET_ADDRESS = 5,
  GET_DESCRIPTOR = 6,
  SET_DESCRIPTOR = 7,
  GET_CONFIGURATION = 8,
  SET_CONFIGURATION = 9,
  GET_INTERFACE = 10,
  SET_INTERFACE = 11,
  SYNCH_FRAME = 12
};

enum VendorRequest { VENDOR_RESET = 0x40, VENDOR_IDENTIFY = 0x41 };

enum CdcClassRequest {
  CDC_SEND_ENCAPSULATED_COMMAND = 0x00,
  CDC_GET_ENCAPSULATED_RESPONSE = 0x01,
  CDC_SET_LINE_CODING = 0x20,
  CDC_GET_LINE_CODING = 0x21,
  CDC_SET_CONTROL_LINE_STATE = 0x22,
};

// Table 9-5: Descriptor Types
enum UsbDescriptor {
  USB_DESCRIPTOR_DEVICE = 1,
  USB_DESCRIPTOR_CONFIGURATION = 2,
  USB_DESCRIPTOR_STRING = 3,
  USB_DESCRIPTOR_INTERFACE = 4,
  USB_DESCRIPTOR_ENDPOINT = 5,
  USB_DESCRIPTOR_DEVICE_QUALIFIER = 6,
  USB_DESCRIPTOR_OTHER_SPEED_CONFIGURATION = 7,
  USB_DESCRIPTOR_INTERFACE_POWER = 8,
  // USB ECN additions
  USB_DESCRIPTOR_OTG = 9,
  USB_DESCRIPTOR_DEBUG = 10,
  USB_DESCRIPTOR_INTERFACE_ASSOCIATION = 11
};

enum MsRequest { GET_MS_DESCRIPTOR = 0xa0 };

#define ENDPOINT_IN_ADDR(num) (num) | 0x80
#define ENDPOINT_OUT_ADDR(num) (num)

enum EndpointType {
  ENDPOINT_TYPE_CONTROL = 0b00,
  ENDPOINT_TYPE_ISOCHRONOUS = 0b01,
  ENDPOINT_TYPE_BULK = 0b10,
  ENDPOINT_TYPE_INTERRUPT = 0b11,
};

// CDC PSTN Subclass 1.2
// Table 17: Line Coding Structure
struct LineCoding {
  uint32_t dwDTERate;
  uint8_t bCharFormat;
  uint8_t bParityType;
  uint8_t bDataBits;
};

#endif /* USB_PROTOCOL_H */
