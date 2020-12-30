; SPDX-FileCopyrightText: 2019-2020 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; SPDX-License-Identifier: MIT OR Apache-2.0

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
