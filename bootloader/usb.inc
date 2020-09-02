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

; USB 2.0 standard constants

; Table 8-1: PID Types
PID_OUT   equ 0b0001
PID_IN    equ 0b1001
PID_SOF   equ 0b0101
PID_SETUP equ 0b1101
PID_DATA0 equ 0b0011
PID_DATA1 equ 0b1011
PID_DATA2 equ 0b0111
PID_MDATA equ 0b1111
PID_ACK   equ 0b0010
PID_NAK   equ 0b1010
PID_STALL equ 0b1110
PID_NYET  equ 0b0110
PID_PRE   equ 0b1100
PID_ERR   equ 0b1100
PID_SPLIT equ 0b1000
PID_PING  equ 0b0100

; Table 9-4: Standard Request Codes
GET_STATUS        equ 0
CLEAR_FEATURE     equ 1
SET_FEATURE       equ 3
SET_ADDRESS       equ 5
GET_DESCRIPTOR    equ 6
SET_DESCRIPTOR    equ 7
GET_CONFIGURATION equ 8
SET_CONFIGURATION equ 9
GET_INTERFACE     equ 10
SET_INTERFACE     equ 11
SYNCH_FRAME       equ 12

; Table 9-2: Format of Setup Data
bmRequestType equ 0
bRequest      equ 1
wValueLsb     equ 2
wValueMsb     equ 3
wIndexLsb     equ 4
wIndexMsb     equ 5
wLengthLsb    equ 6
wLengthMsb    equ 7

; Table 9-5: Descriptor Types
USB_DESCRIPTOR_DEVICE           equ 1
USB_DESCRIPTOR_CONFIGURATION    equ 2
USB_DESCRIPTOR_STRING           equ 3
USB_DESCRIPTOR_INTERFACE        equ 4
USB_DESCRIPTOR_DEVICE_QUALIFIER equ 6

EP0_BUFFER_SIZE equ 0x40

TX_STATF_DATA_STAGE   equ 7
TX_STATF_ADDR         equ 6
TX_STATF_DIR_IN       equ 5
TX_STATF_DIR_OUT      equ 4
TX_STATF_RAM_DATA     equ 3
TX_STATF_WRITE_CFG    equ 2
TX_STATF_WRITE_FLASH  equ 1
TX_STATF_WRITE_ID     equ 0

TX_STATF_REJECT equ 0x00

TX_DATAF_DTS   equ 7
TX_DATAF_EXACT equ 6
