// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include <stdint.h>

struct DeviceDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t bcdUSB;
  uint8_t bDeviceClass;
  uint8_t bDeviceSubClass;
  uint8_t bDeviceProtocol;
  uint8_t bMaxPacketSize0;
  uint16_t idVendor;
  uint16_t idProduct;
  uint16_t bcdDevice;
  uint8_t iManufacturer;
  uint8_t iProduct;
  uint8_t iSerialNumber;
  uint8_t bNumConfigurations;
};

struct InterfaceDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
};

struct InterfaceAssociationDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bFirstInterface;
  uint8_t bInterfaceCount;
  uint8_t bFunctionClass;
  uint8_t bFunctionSubClass;
  uint8_t bFunctionProtocol;
  uint8_t iFunction;
};

struct ConfigDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint16_t wTotalLength;
  uint8_t bNumInterfaces;
  uint8_t bConfigurationValue;
  uint8_t iConfiguration;
  uint8_t bmAttributes;
  uint8_t bMaxPower;
};

struct EndpointDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
};

// USB-CDC120
// Table 15: Class-Specific Descriptor Header Format
struct CdcHeaderFunctionDescriptor {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint16_t bcdCDC;
};

// USB-PSTN120
// Table 4: Abstract Control Management Functional Descriptor
struct CdcAcmFunctionDescriptor {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bmCapabilities;
};

// USB-CDC120
// Table 16: Union Interface Functional Descriptor
struct CdcUnionFunctionDescriptor {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bControlInterface;
  uint8_t bSubordinateInterface;
};

// USB-PSTN120
// Table 3: Call Management Functional Descriptor
struct CdcCallManagementFunctionDescriptor {
  uint8_t bFunctionLength;
  uint8_t bDescriptorType;
  uint8_t bDescriptorSubtype;
  uint8_t bmCapabilities;
  uint8_t bDataInterface;
};

struct FullConfigDescriptor {
  struct ConfigDescriptor config;
  struct InterfaceAssociationDescriptor iad;
  struct InterfaceDescriptor comm_if;
  struct CdcHeaderFunctionDescriptor cdc_header;
  struct CdcAcmFunctionDescriptor cdc_acm;
  struct CdcUnionFunctionDescriptor cdc_union;
  struct CdcCallManagementFunctionDescriptor cdc_call_management;
  struct EndpointDescriptor ep1;
  struct InterfaceDescriptor data_if;
  struct EndpointDescriptor ep2_in;
  struct EndpointDescriptor ep2_out;
};

struct StringDescriptor {
  uint8_t bLength;
  uint8_t bDescriptorType;
};

struct StringDescriptor0 {
  struct StringDescriptor header;
  uint16_t wLangId[1];
};

struct StringDescriptorVendor {
  struct StringDescriptor header;
  uint16_t wString[9];
};

struct StringDescriptorProduct {
  struct StringDescriptor header;
  uint16_t wString[12];
};

#endif /* USB_DESCRIPTORS_H */
