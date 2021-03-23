; SPDX-FileCopyrightText: 2019-2020 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; SPDX-License-Identifier: MIT OR Apache-2.0

; USB 2.0 standard endpoint 0 request handling

#include <xc.inc>
#include "macros.inc"
#include "usb.inc"

  global usb_ep0_std_setup
  extrn DEV_CFG, TX_ADDR, EP0_OUT_BUFFER, DATA_CNT, DATA_ADDR
  extrn usb_assert_request_type, usb_assert_value, usb_assert_index, usb_assert_no_data
  extrn usb_save_rom_data_addr
  extrn DEVICE_DESCRIPTOR, CONFIG_DESCRIPTOR
  extrn STRING_DESCRIPTOR0, STRING_DESCRIPTOR1, STRING_DESCRIPTOR2

  psect code

usb_ep0_std_setup:
  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  bz _get_status

  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw GET_DESCRIPTOR
  bz _get_descriptor

  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw SET_ADDRESS
  bz _set_address

  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw GET_CONFIGURATION
  bz _get_configuration

  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw SET_CONFIGURATION
  bz _set_configuration

  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw GET_INTERFACE
  bz _get_interface

  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw SET_INTERFACE
  bz _set_interface

  retlw TX_STATF_REJECT

_assert_out_request:
  movlw 0x00
  ljmp usb_assert_request_type

_assert_in_request:
  movlw 0x80
  ljmp usb_assert_request_type

_get_status:
  rcall _assert_in_request

  mov_tblptr ZEROES
  movlw 0x02
  bra _static_rom_response

_set_address:
  rcall _assert_out_request
  fcall usb_assert_no_data

  movff EP0_OUT_BUFFER + wValueLsb, TX_ADDR
  retlw (1 << TX_STATF_ADDR) | (1 << TX_STATF_DIR_OUT)

_get_configuration:
  rcall _assert_in_request

  movlw 0x01
  movwf DATA_CNT, a
  movlw low(DEV_CFG)
  movwf DATA_ADDR, a
  movlw high(DEV_CFG)
  movwf DATA_ADDR+1, a

  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN) | (1 << TX_STATF_RAM_DATA)

_set_configuration:
  rcall _assert_out_request
  fcall usb_assert_no_data

  movf BANKMASK(EP0_OUT_BUFFER + wValueLsb), w, b
  iorwf WREG, a
  bz _set_configuration_0_or_1
  decf WREG, a
  bz _set_configuration_0_or_1
  retlw TX_STATF_REJECT

_set_configuration_0_or_1:
  movf BANKMASK(EP0_OUT_BUFFER + wValueLsb), w, b
  movwf DEV_CFG, a
  retlw (1 << TX_STATF_DIR_OUT)

_get_interface:
  movlw 0x81
  fcall usb_assert_request_type
  ; interface must be 0
  movlw 0x00
  fcall usb_assert_value

  mov_tblptr ZEROES
  movlw 0x01
  bra _static_rom_response

_set_interface:
  movlw 0x01
  fcall usb_assert_request_type
  movlw 0x00
  ; alternate setting must be 0
  fcall usb_assert_value
  movlw 0x00
  ; interface must be 0
  fcall usb_assert_index
  fcall usb_assert_no_data

  retlw (1 << TX_STATF_DIR_OUT)

_get_descriptor:
  rcall _assert_in_request

  movf BANKMASK(EP0_OUT_BUFFER + wValueMsb), w, b
  dcfsnz WREG, a
  bra _get_device_descriptor ; 0x01
  dcfsnz WREG, a
  bra _get_configuration_descriptor ; 0x02
  dcfsnz WREG, a
  bra _get_string_descriptor ; 0x03

  retlw TX_STATF_REJECT

_get_device_descriptor:
  ; descriptor index must be 0
  tstfsz BANKMASK(EP0_OUT_BUFFER + wValueLsb), b ; skip next if wValueLsb=0
  retlw TX_STATF_REJECT

  mov_tblptr DEVICE_DESCRIPTOR
  bra _descriptor_response

_get_configuration_descriptor:
  ; descriptor index must be 0
  tstfsz BANKMASK(EP0_OUT_BUFFER + wValueLsb), b ; skip next if wValueLsb=0
  retlw TX_STATF_REJECT

  mov_tblptr CONFIG_DESCRIPTOR
  tblrd+* ; bDescriptorType
  tblrd+* ; wTotalLengthLsb
  movf TABLAT, w, a
  tblrd*-
  tblrd*-
  bra _static_rom_response

_get_string_descriptor:
  movf BANKMASK(EP0_OUT_BUFFER + wValueLsb), w, b
  bz _string0_response ; 0x00
  dcfsnz WREG, a
  bra _string1_response ; 0x01
  dcfsnz WREG, a
  bra _string2_response ; 0x02

  retlw TX_STATF_REJECT

_string0_response:
  mov_tblptr STRING_DESCRIPTOR0
  bra _descriptor_response

_string1_response:
  mov_tblptr STRING_DESCRIPTOR1
  bra _descriptor_response

_string2_response:
  mov_tblptr STRING_DESCRIPTOR2

; Inputs:
;   TBLPTR: ROM source address
_descriptor_response:
  tblrd*
  movf TABLAT, w, a

; Inputs:
;   TBLPTR: ROM source address
;   W: byte count
_static_rom_response:
  movwf DATA_CNT, a
  fcall usb_save_rom_data_addr
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN)

  psect data
ZEROES:
  db 0x00, 0x00

  end
