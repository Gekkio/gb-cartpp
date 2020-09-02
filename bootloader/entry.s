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

; Entry point and interrupt handlers

#include <xc.inc>
#include "protocol.inc"

  extrn bootloader_init

application_main equ 0x800
application_isr_high equ 0x808
application_isr_low equ 0x818

  psect reset_vec,abs,local,class=CODE,space=SPACE_CODE,reloc=2
reset_vec:
  clrf INTCON, a ; try to avoid immediate interrupts in a non-POR scenario
  ljmp bootloader_init

  org 0x0008
  ljmp application_isr_high

  org 0x0010
  db BOOTLOADER_VERSION_MINOR
  db BOOTLOADER_VERSION_MAJOR

  org 0x0018
  ljmp application_isr_low

  end reset_vec
