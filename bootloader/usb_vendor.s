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

; USB vendor request handling

#include <xc.inc>
#include "macros.inc"
#include "protocol.inc"
#include "usb.inc"

  global usb_vendor_setup, usb_vendor_out_data
  extrn EP0_OUT_BUFFER, EP0_OUT_CNT, TX_STATF, DATA_ADDR, DATA_CNT
  extrn usb_assert_request_type, usb_assert_no_data
  extrn usb_save_rom_data_addr, usb_load_rom_data_addr, usb_save_ram_data_addr
  extrn erase_flash_block, write_cfg, write_id, write_flash, memcpy_tblptr_from_fsr0, load_cfg_tblptr
  extrn sys_prepare_reset

SYS_STATUS_UNLOCK equ 7

  psect udata_acs
SYS_STATUS: ds 1
IDENTIFY_RESPONSE: ds 5

  psect code

usb_vendor_setup:
  movf BANKMASK(EP0_OUT_BUFFER + bRequest), w, b
  sublw VENDOR_REQUEST_RESET
  bz _reset ; 0x40

  infsnz WREG, a
  bra _identify ; 0x41

  infsnz WREG, a
  bra _unlock ; 0x42

  btfss SYS_STATUS, SYS_STATUS_UNLOCK, a ; skip next if SYS_STATUS_UNLOCK=1
  retlw TX_STATF_REJECT

  infsnz WREG, a
  bra _lock ; 0x43
  infsnz WREG, a
  bra _read ; 0x44
  infsnz WREG, a
  bra _erase_flash ; 0x45
  infsnz WREG, a
  bra _write_flash ; 0x46
  infsnz WREG, a
  bra _write_cfg ; 0x47
  infsnz WREG, a
  bra _write_id ; 0x48

  retlw TX_STATF_REJECT

_assert_out_request:
  movlw 0x40
  ljmp usb_assert_request_type

_assert_in_request:
  movlw 0xc0
  ljmp usb_assert_request_type

_reset:
  rcall _assert_out_request
  fcall usb_assert_no_data

  movf BANKMASK(EP0_OUT_BUFFER + wValueLsb), w, b
  fcall sys_prepare_reset
  retlw (1 << TX_STATF_DIR_OUT)

_identify:
  rcall _assert_in_request

  movlw 0x42
  movwf IDENTIFY_RESPONSE, a
  movlw BOOTLOADER_VERSION_MINOR
  movwf IDENTIFY_RESPONSE+1, a
  movlw BOOTLOADER_VERSION_MAJOR
  movwf IDENTIFY_RESPONSE+2, a
  movlw 0x02
  fcall load_cfg_tblptr
  tblrd*+
  movff TABLAT, IDENTIFY_RESPONSE+3
  tblrd*+
  movff TABLAT, IDENTIFY_RESPONSE+4
  movlw 0x05
  movwf DATA_CNT, a
  lfsr 1, IDENTIFY_RESPONSE
  fcall usb_save_ram_data_addr
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN) | (1 << TX_STATF_RAM_DATA)

_unlock:
  rcall _assert_out_request
  fcall usb_assert_no_data

  movlw 0xf2
  cpfseq BANKMASK(EP0_OUT_BUFFER + wValueLsb), b
  retlw TX_STATF_REJECT
  movlw 0xc2
  cpfseq BANKMASK(EP0_OUT_BUFFER + wValueMsb), b
  retlw TX_STATF_REJECT
  movlw 0x9a
  cpfseq BANKMASK(EP0_OUT_BUFFER + wIndexLsb), b
  retlw TX_STATF_REJECT
  movlw 0xf0
  cpfseq BANKMASK(EP0_OUT_BUFFER + wIndexMsb), b
  retlw TX_STATF_REJECT

  bsf SYS_STATUS, SYS_STATUS_UNLOCK, a
  retlw (1 << TX_STATF_DIR_OUT)

_lock:
  rcall _assert_out_request
  fcall usb_assert_no_data

  bcf SYS_STATUS, SYS_STATUS_UNLOCK, a
  retlw (1 << TX_STATF_DIR_OUT)

_read:
  rcall _assert_in_request
  rcall _set_data_addr
  movff EP0_OUT_BUFFER + wLengthMsb, DATA_CNT+1
  movff EP0_OUT_BUFFER + wLengthLsb, DATA_CNT

  btfss BANKMASK(EP0_OUT_BUFFER + wIndexMsb), 7, b ; skip next if wIndexMsb bit 7 is 1
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN)
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN) | (1 << TX_STATF_RAM_DATA)

_erase_flash:
  rcall _assert_out_request
  fcall usb_assert_no_data
  rcall _set_data_addr
  fcall usb_load_rom_data_addr

  fcall erase_flash_block
  retlw (1 << TX_STATF_DIR_OUT)

_write_flash:
  rcall _assert_out_request
  rcall _set_data_addr
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_OUT) | (1 << TX_STATF_WRITE_FLASH)

_write_cfg:
  rcall _assert_out_request
  rcall _set_data_addr
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_OUT) | (1 << TX_STATF_WRITE_CFG)

_write_id:
  rcall _assert_out_request
  rcall _set_data_addr
  retlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_OUT) | (1 << TX_STATF_WRITE_ID)

_set_data_addr:
  movff EP0_OUT_BUFFER + wValueLsb, DATA_ADDR
  movff EP0_OUT_BUFFER + wValueMsb, DATA_ADDR+1
  movff EP0_OUT_BUFFER + wIndexLsb, DATA_ADDR+2
  return

usb_vendor_out_data:
  rcall _prepare_write
  btfsc TX_STATF, TX_STATF_WRITE_FLASH, a ; skip next if TX_STATF_WRITE_FLASH=0
  call write_flash
  btfsc TX_STATF, TX_STATF_WRITE_CFG, a ; skip next if TX_STATF_WRITE_CFG=0
  call write_cfg
  btfsc TX_STATF, TX_STATF_WRITE_ID, a ; skip next if TX_STATF_WRITE_ID=0
  call write_id
  ljmp usb_save_rom_data_addr

_prepare_write:
  lfsr 1, EP0_OUT_BUFFER
  movf BANKMASK(EP0_OUT_CNT), w, b
  ljmp usb_load_rom_data_addr

  end
