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

; Miscellaneous utilities

#include <xc.inc>

  psect udata_acs
WRITE_LEN: ds 1
WRITE_PTR: ds 1
WRITE_OFF: ds 1
BLOCK_LEN: ds 1

  psect utils_buf,class=RAM,space=SPACE_DATA,noexec
WRITE_BUF: ds 0x40

  psect code
  global memcpy_fsr0_from_tblptr, memcpy_tblptr_from_fsr0, memclear_fsr0, memcpy_fsr0_from_fsr1, load_cfg_tblptr
  global erase_flash_block, write_cfg, write_id, write_flash

; Inputs:
;   FSR0: RAM target address
;   TBLPTR: ROM source address
;   W: byte count (0 = 0x100)
; Outputs:
;   FSR0: incremented RAM target address
;   TBLPTR: incremented ROM source address
memcpy_fsr0_from_tblptr:
  tblrd*+
  movff TABLAT, POSTINC0
  decfsz WREG, f, a ; skip next if W goes to 0
  bra memcpy_fsr0_from_tblptr

  return

; Inputs:
;   TBLPTR: ROM target address
;   FSR0: RAM source address
;   W: byte count (0 = 0x100)
; Outputs:
;   TBLPTR: incremented ROM target address
;   FSR0: incremented RAM source address
memcpy_tblptr_from_fsr0:
  movff POSTINC0, TABLAT
  tblwt*+
  decfsz WREG, f, a ; skip next if W goes to 0
  bra memcpy_tblptr_from_fsr0

  return

; Inputs:
;   FSR0: RAM target address
;   FSR1: RAM source address
;   W: byte count (0 = 0x100)
; Outputs:
;   FSR0: incremented RAM target address
;   FSR1: incremented RAM source address
memcpy_fsr0_from_fsr1:
  movff POSTINC1, POSTINC0
  decfsz WREG, f, a ; skip next if WREG goes to 0
  bra memcpy_fsr0_from_fsr1

  return

; Inputs:
;   FSR0: RAM target address
;   W: byte count (0 = 0x100)
; Outputs:
;   FSR0: incremented RAM target address
memclear_fsr0:
  clrf POSTINC0, a
  decfsz WREG, f, a ; skip next if WREG goes to 0
  bra memclear_fsr0

  return

; Required sequence for flash/config/EEPROM writes. Has to be *precisely this*,
; because even a single NOP in the middle can break the write
_eecon_write_sequence:
  clrwdt
  movlw 0x55
  movwf EECON2, a
  movlw 0xAA
  movwf EECON2, a
  bsf WR
  return

_eecon_write_exit:
  bcf WREN
  return

; Inputs:
;   TBLPTR: target address
erase_flash_block:
  movlw EECON1_EEPGD_MASK | EECON1_FREE_MASK | EECON1_WREN_MASK
  movwf EECON1, a
  rcall _eecon_write_sequence
  bra _eecon_write_exit

; Inputs:
;   FSR1: RAM source address
;   TBLPTR: target address
;   W: byte count
; Outputs:
;   W: remaining byte count if write errored, 0 otherwise
;   FSR1: incremented RAM source address
;   TBLPTR: incremented target address
write_cfg:
  movwf WRITE_LEN, a
  movlw EECON1_EEPGD_MASK | EECON1_CFGS_MASK | EECON1_WREN_MASK
  movwf EECON1, a
_write_cfg_loop:
  movff POSTINC1, TABLAT
  tblwt*
  rcall _eecon_write_sequence
  btfsc WRERR ; skip next if WRERR=0
  bra _eecon_write_exit

  tblrd*+

  decfsz WRITE_LEN, a ; skip next if WRITE_LEN goes to 0
  bra _write_cfg_loop

  bra _eecon_write_exit

; Inputs:
;   FSR1: RAM source address
;   TBLPTR: target address
;   W: byte count
write_id:
  movwf WRITE_LEN, a
  movlw 0x8
  movwf BLOCK_LEN, a
  movlw 0b11111000
  bra _write_flash_block

; Inputs:
;   FSR1: RAM source address
;   TBLPTR: target address
;   W: byte count
write_flash:
  movwf WRITE_LEN, a
  movlw 0x40
  movwf BLOCK_LEN, a
  movlw 0b11000000

; Inputs:
;   W: block address mask
;   FSR1: RAM source address
;   TBLPTR: block start address
;   WRITE_LEN: byte count
;   WRITE_OFF: offset from block start
;   BLOCK_LEN: block length
_write_flash_block:
  movff TBLPTRL, WRITE_OFF
  andwf TBLPTRL, a
  movff TBLPTRL, WRITE_PTR
  movf TBLPTRL, w, a
  subwf WRITE_OFF, a

  ; copy entire flash block to RAM
  lfsr 0, WRITE_BUF
  movf BLOCK_LEN, w, a
  rcall memcpy_fsr0_from_tblptr
  tblrd*-

  ; overwrite WRITE_LEN bytes starting from WRITE_OFF
  lfsr 0, WRITE_BUF
  movf WRITE_OFF, w, a
  addwf FSR0L, a
  movf WRITE_LEN, w, a
  rcall memcpy_fsr0_from_fsr1

  ; erase entire flash block
  movff WRITE_PTR, TBLPTRL
  rcall erase_flash_block
  tblrd*-

  ; prepare flash holding registers
  lfsr 0, WRITE_BUF
_prepare_flash:
  movf POSTINC0, w, a
  movwf TABLAT, a
  tblwt+*
  decfsz BLOCK_LEN, a ; skip next if W goes to 0
  bra _prepare_flash

  ; begin write cycle
  movlw EECON1_EEPGD_MASK | EECON1_WREN_MASK
  movwf EECON1, a
  rcall _eecon_write_sequence
  tblrd*+
  bra _eecon_write_exit

; Inputs:
;   W: low value
load_cfg_tblptr:
  movwf TBLPTRL, a
  clrf TBLPTRH, a
  movlw 0x20
  movwf TBLPTRU, a
  return

  end
