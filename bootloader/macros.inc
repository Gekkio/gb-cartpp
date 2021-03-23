; SPDX-FileCopyrightText: 2019-2021 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; SPDX-License-Identifier: MIT OR Apache-2.0

; Loads the 24-bit address `addr` as 8-bit parts into the TBLPTRx registers
mov_tblptr macro addr
  movlw low highword addr
  movwf TBLPTRU, a
  movlw high addr
  movwf TBLPTRH, a
  movlw low addr
  movwf TBLPTRL, a
  endm
