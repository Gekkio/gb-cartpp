; SPDX-FileCopyrightText: 2019-2021 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; SPDX-License-Identifier: MIT OR Apache-2.0

; CRC16 checksum calculation for main code area (0x0800-0x7fff)

#include <xc.inc>
#include "macros.inc"

  psect crc_table_l,class=RAM,space=SPACE_DATA,noexec
CRC_TABLE_L: ds 0x100

  psect crc_table_h,class=RAM,space=SPACE_DATA,noexec
CRC_TABLE_H: ds 0x100

  psect udata_acs
ROM_CRC: ds 2
CRC_DIV: ds 1
CRC_BIT: ds 1

  psect code
  global crc_check
  extrn load_cfg_tblptr

_load_crc_table_fsr:
  lfsr 0, CRC_TABLE_L
  lfsr 1, CRC_TABLE_H
  return

; Inputs: -
; Outputs:
;   W: 0x00 on failure, 0xff on success
crc_check:
  rcall _prepare_crc_table
  ; calculate CRC16 from 0x800-0x7fff
_calc_rom_crc:
  clrf ROM_CRC, a
  clrf ROM_CRC+1, a
  mov_tblptr 0x800
  rcall _load_crc_table_fsr
_crc_calc:
  tblrd*+
  movf TABLAT, w, a
  xorwf ROM_CRC+1, w, a
  ; W = CRC table lookup index
  movwf FSR0L, a
  movwf FSR1L, a
  ; ROM_CRC_h = ROM_CRC_l ^ data_h
  ; ROM_CRC_l = data_l
  movff ROM_CRC, ROM_CRC+1
  movf INDF1, w, a
  xorwf ROM_CRC+1, a
  movff INDF0, ROM_CRC
  movf TBLPTRH, w, a
  sublw 0x80
  bnz _crc_calc

  ; compare with little-endian CRC16 checksum at 0x200000
_crc_compare:
  movlw 0x00
  fcall load_cfg_tblptr
  tblrd*+
  movf TABLAT, w, a
  cpfseq ROM_CRC, a ; skip next if ROM_CRC_l=W
  retlw 0x00
  tblrd*+
  movf TABLAT, w, a
  cpfseq ROM_CRC+1, a ; skip next if ROM_CRC_h=W
  retlw 0x00

  retlw 0xff

_prepare_crc_table:
  rcall _load_crc_table_fsr
  clrf CRC_DIV, a
_prepare_crc_table_loop:
  clrf ROM_CRC, a
  movff CRC_DIV, ROM_CRC+1
  movlw 0x08
  movwf CRC_BIT, a
_prepare_crc_table_bit:
  bcf CARRY
  rlcf ROM_CRC, a
  rlcf ROM_CRC+1, a
  btfsc CARRY ; skip next if C is 0
  rcall _xor_polynomial
  decfsz CRC_BIT, a ; skip next if CRC_BIT goes to 0
  bra _prepare_crc_table_bit
  movff ROM_CRC, POSTINC0
  movff ROM_CRC+1, POSTINC1
  incfsz CRC_DIV, a ; skip next if CRC_DIV goes to 0
  bra _prepare_crc_table_loop
  return

_xor_polynomial:
  movlw 0x21
  xorwf ROM_CRC, a
  movlw 0x10
  xorwf ROM_CRC+1, a
  return

  end
