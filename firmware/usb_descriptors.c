// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "config.h"

#include "usb.h"
#include "usb_descriptors.h"
#include "usb_protocol.h"

enum DeviceClass {
  DEVICE_CDC = 0x02,
};

enum CdcDescriptorType { CS_INTERFACE = 0x24, CS_ENDPOINT = 0x25 };

enum CdcDescriptorSubtype {
  CS_HEADER = 0x00,
  CS_CALL_MANAGEMENT = 0x01,
  CS_ACM = 0x02,
  CS_UNION = 0x06
};

enum CdcInterfaceClass {
  IF_COMM = 0x02,
  IF_DATA = 0x0a,
};

enum CdcInterfaceSubClass { IF_ACM = 0x02 };

const struct DeviceDescriptor DEVICE_DESCRIPTOR = {
    .bLength = sizeof(struct DeviceDescriptor),
    .bDescriptorType = USB_DESCRIPTOR_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = DEVICE_CDC,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = EP0_PACKET_SIZE,
    .idVendor = 0x16c0,
    .idProduct = 0x05e1,
    .bcdDevice = (FW_MAJOR_VERSION << 8) | FW_MINOR_VERSION,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 0,
    .bNumConfigurations = 1};

const struct FullConfigDescriptor CONFIG_DESCRIPTOR = {
    .config =
        {
            .bLength = sizeof(struct ConfigDescriptor),
            .bDescriptorType = USB_DESCRIPTOR_CONFIGURATION,
            .wTotalLength = sizeof(struct FullConfigDescriptor),
            .bNumInterfaces = 2,
            .bConfigurationValue = 1,
            .iConfiguration = 0,
            .bmAttributes = 0x80,
            .bMaxPower = 0x64, // 200 mA
        },
    .iad = {.bLength = sizeof(struct InterfaceAssociationDescriptor),
            .bDescriptorType = USB_DESCRIPTOR_INTERFACE_ASSOCIATION,
            .bFirstInterface = 0,
            .bInterfaceCount = 2,
            .bFunctionClass = 0x02,
            .bFunctionSubClass = 0x02,
            .bFunctionProtocol = 0x00,
            .iFunction = 0x00},
    .comm_if = {.bLength = sizeof(struct InterfaceDescriptor),
                .bDescriptorType = USB_DESCRIPTOR_INTERFACE,
                .bInterfaceNumber = 0,
                .bAlternateSetting = 0,
                .bNumEndpoints = 1,
                .bInterfaceClass = IF_COMM,
                .bInterfaceSubClass = IF_ACM,
                .bInterfaceProtocol = 0x00,
                .iInterface = 0x00},
    .cdc_header = {.bFunctionLength =
                       sizeof(struct CdcHeaderFunctionDescriptor),
                   .bDescriptorType = CS_INTERFACE,
                   .bDescriptorSubtype = CS_HEADER,
                   .bcdCDC = 0x0110},
    .cdc_acm =
        {
            .bFunctionLength = sizeof(struct CdcAcmFunctionDescriptor),
            .bDescriptorType = CS_INTERFACE,
            .bDescriptorSubtype = CS_ACM,
            .bmCapabilities = 0x00,
        },
    .cdc_union = {.bFunctionLength = sizeof(struct CdcUnionFunctionDescriptor),
                  .bDescriptorType = CS_INTERFACE,
                  .bDescriptorSubtype = CS_UNION,
                  .bControlInterface = 0,
                  .bSubordinateInterface = 1},
    .cdc_call_management = {.bFunctionLength = sizeof(
                                struct CdcCallManagementFunctionDescriptor),
                            .bDescriptorType = CS_INTERFACE,
                            .bDescriptorSubtype = CS_CALL_MANAGEMENT,
                            .bmCapabilities = 0x00,
                            .bDataInterface = 1},
    .ep1 = {.bLength = sizeof(struct EndpointDescriptor),
            .bDescriptorType = USB_DESCRIPTOR_ENDPOINT,
            .bEndpointAddress = ENDPOINT_IN_ADDR(1),
            .bmAttributes = ENDPOINT_TYPE_INTERRUPT,
            .wMaxPacketSize = EP1_PACKET_SIZE,
            .bInterval = 2},
    .data_if = {.bLength = sizeof(struct InterfaceDescriptor),
                .bDescriptorType = USB_DESCRIPTOR_INTERFACE,
                .bInterfaceNumber = 1,
                .bAlternateSetting = 0,
                .bNumEndpoints = 2,
                .bInterfaceClass = IF_DATA,
                .bInterfaceSubClass = 0x00,
                .bInterfaceProtocol = 0x00,
                .iInterface = 0x00},
    .ep2_in = {.bLength = sizeof(struct EndpointDescriptor),
               .bDescriptorType = USB_DESCRIPTOR_ENDPOINT,
               .bEndpointAddress = ENDPOINT_IN_ADDR(2),
               .bmAttributes = ENDPOINT_TYPE_BULK,
               .wMaxPacketSize = EP2_PACKET_SIZE,
               .bInterval = 0},
    .ep2_out = {.bLength = sizeof(struct EndpointDescriptor),
                .bDescriptorType = USB_DESCRIPTOR_ENDPOINT,
                .bEndpointAddress = ENDPOINT_OUT_ADDR(2),
                .bmAttributes = ENDPOINT_TYPE_BULK,
                .wMaxPacketSize = EP2_PACKET_SIZE,
                .bInterval = 0}};

const struct StringDescriptor0 STRING_DESCRIPTOR0 = {
    .header =
        {
            .bLength = sizeof(struct StringDescriptor0),
            .bDescriptorType = USB_DESCRIPTOR_STRING,
        },
    .wLangId = {0x0409}};

const struct StringDescriptorVendor STRING_DESCRIPTOR1 = {
    .header =
        {
            .bLength = sizeof(struct StringDescriptorVendor),
            .bDescriptorType = USB_DESCRIPTOR_STRING,
        },
    .wString = {'g', 'e', 'k', 'k', 'i', 'o', '.', 'f', 'i'}};

const struct StringDescriptorProduct STRING_DESCRIPTOR2 = {
    .header =
        {
            .bLength = sizeof(struct StringDescriptorProduct),
            .bDescriptorType = USB_DESCRIPTOR_STRING,
        },
    .wString = {'G', 'B', '-', 'C', 'A', 'R', 'T', 'P', 'P', '-', 'X', 'C'}};
