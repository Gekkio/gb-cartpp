; SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; SPDX-License-Identifier: MIT OR Apache-2.0

; USB 2.0 standard descriptors

#include <xc.inc>
#include "protocol.inc"
#include "usb.inc"

  global DEVICE_DESCRIPTOR, CONFIG_DESCRIPTOR
  global STRING_DESCRIPTOR0, STRING_DESCRIPTOR1, STRING_DESCRIPTOR2

  psect data

DEVICE_DESCRIPTOR:
  db __end_DEVICE_DESCRIPTOR - DEVICE_DESCRIPTOR ; bLength
  db USB_DESCRIPTOR_DEVICE ; bDescriptorType
  dw 0x0200 ; bcdUSB
  db 0xff ; bDeviceClass
  db 0xff ; bDeviceSubClass
  db 0xff ; bDeviceProtocol
  db EP0_BUFFER_SIZE ; bMaxPacketSize0
  dw 0x16c0 ; idVendor
  dw 0x05dc ; idProduct
  db BOOTLOADER_VERSION_MINOR ; bcdDevice
  db BOOTLOADER_VERSION_MAJOR ; bcdDevice
  db 0x01 ; iManufacturer
  db 0x02 ; iProduct
  db 0x00 ; iSerialNumber
  db 0x01 ; bNumConfigurations
__end_DEVICE_DESCRIPTOR:

CONFIG_DESCRIPTOR:
  db __end_CONFIG_DESCRIPTOR - CONFIG_DESCRIPTOR ; bLength
  db USB_DESCRIPTOR_CONFIGURATION ; bDescriptorType
  dw __end_INTERFACE_DESCRIPTOR - CONFIG_DESCRIPTOR ; wTotalLength
  db 0x01 ; bNumInterfaces
  db 0x01 ; bConfigurationValue
  db 0x00 ; iConfiguration
  db 0x80 ; bmAttributes
  db 0x64 ; bMaxPower
__end_CONFIG_DESCRIPTOR:

INTERFACE_DESCRIPTOR:
  db __end_INTERFACE_DESCRIPTOR - INTERFACE_DESCRIPTOR ; bLength
  db USB_DESCRIPTOR_INTERFACE ; bDescriptorType
  db 0x00 ; bInterfaceNumber
  db 0x00 ; bAlternateSetting
  db 0x00 ; bNumEndpoints
  db 0xff ; bInterfaceClass
  db 0xff ; bInterfaceSubClass
  db 0xff ; bInterfaceProtocol
  db 0x00 ; iInterface
__end_INTERFACE_DESCRIPTOR:

STRING_DESCRIPTOR0:
  db __end_STRING_DESCRIPTOR0 - STRING_DESCRIPTOR0 ; bLength
  db USB_DESCRIPTOR_STRING ; bDescriptorType
  dw 0x0409 ; wLANGID[0]
__end_STRING_DESCRIPTOR0:

STRING_DESCRIPTOR1:
  db __end_STRING_DESCRIPTOR1 - STRING_DESCRIPTOR1 ; bLength
  db USB_DESCRIPTOR_STRING ; bDescriptorType
  dw 'g', 'e', 'k', 'k', 'i', 'o', '.', 'f', 'i'
__end_STRING_DESCRIPTOR1:

STRING_DESCRIPTOR2:
  db __end_STRING_DESCRIPTOR2 - STRING_DESCRIPTOR2 ; bLength
  db USB_DESCRIPTOR_STRING ; bDescriptorType
  #ifdef GB_CARTPP_DIY
    dw 'G', 'B', '-', 'C', 'A', 'R', 'T', 'P', 'P', '-', 'D', 'I', 'Y'
  #else
    dw 'G', 'B', '-', 'C', 'A', 'R', 'T', 'P', 'P', '-', 'X', 'C'
  #endif
__end_STRING_DESCRIPTOR2:

  end
