; Copyright (C) 2019-2020 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.

; USB 2.0 standard endpoint 0 request handling

#include <xc.inc>
#include "macros.inc"
#include "usb.inc"

  global usb_ep0_std_setup
  extrn TX_ADDR, EP0_OUT_BUFFER, DATA_CNT
  extrn usb_assert_request_type, usb_assert_no_data
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
  sublw SET_CONFIGURATION
  bz _set_configuration

  retlw TX_STATF_REJECT

_assert_out_request:
  movlw 0x00
  ljmp usb_assert_request_type

_assert_in_request:
  movlw 0x80
  ljmp usb_assert_request_type

_get_status:
  rcall _assert_in_request

  mov_tblptr STATUS_RESPONSE
  movlw 0x02
  bra _static_rom_response

_set_address:
  rcall _assert_out_request
  fcall usb_assert_no_data

  movff EP0_OUT_BUFFER + wValueLsb, TX_ADDR
  retlw (1 << TX_STATF_ADDR) | (1 << TX_STATF_DIR_OUT)

_set_configuration:
  rcall _assert_out_request
  fcall usb_assert_no_data

  movf BANKMASK(EP0_OUT_BUFFER + wValueLsb), w, b
  decf WREG, a
  bz _set_configuration_0_or_1
  decf WREG, a
  bz _set_configuration_0_or_1
  retlw TX_STATF_REJECT

_set_configuration_0_or_1:
  retlw (1 << TX_STATF_DIR_OUT) ; 0x00

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
STATUS_RESPONSE:
  db 0x00, 0x00

  end
