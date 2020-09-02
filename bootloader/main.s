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

; Initialization and main loop

#include <xc.inc>
#include "protocol.inc"

  global sys_prepare_reset, bootloader_init
  extrn usb_reset, usb_attach, usb_detach, usb_service_ep0, usb_sof_handler
  extrn crc_check

application_main equ 0x800
application_isr_high equ 0x808
application_isr_low equ 0x818

  psect udata_acs
USTAT_SAVE: ds 1
SYS_RESET: ds 1
DELAY_CNT: ds 2

  psect shared_ram,class=BANK7,space=SPACE_DATA,noexec
RCON_SAVE: ds 1
RESET_MAGIC: ds 1

  psect code

bootloader_init:
  movff RCON, RCON_SAVE

  ; use SFR register bank for banked accesses (e.g. ANSELx)
  banksel(ANSELA)

  ; initialize port E, set RE2 to low (to keep cartridge VCC disabled)
  clrf LATE, a
  movlw TRISE_WPUE3_MASK | TRISE_TRISE1_MASK | TRISE_TRISE0_MASK
  movwf TRISE, a

  ; initialize ports A-D
  clrf WPUB, a
  setf TRISA, a
  setf TRISB, a
  setf TRISC, a
  setf TRISD, a
  setf ANSELA, b
  setf ANSELB, b
  setf ANSELC, b
  setf ANSELD, b
  setf ANSELE, b
  clrf LATA, a
  clrf LATB, a
  clrf LATC, a
  clrf LATD, a

  ; reset voltage regulator settings
  clrf VREGCON, b

  ; make sure USB is disabled even if a non-POR scenario
  fcall usb_detach

  ; set internal oscillator to 16 MHz, 3xPLL
_init_oscillator:
  bsf SPLLMULT, a
  movlw OSCCON_IRCF2_MASK | OSCCON_IRCF1_MASK | OSCCON_IRCF0_MASK
  movwf OSCCON, a
  movlw OSCCON2_PLLEN_MASK | OSCCON2_INTSRC_MASK
  movwf OSCCON2, a

_wait_stable_clock:
  btfss HFIOFS, a ; skip next if HFIOFS=1
  bra _wait_stable_clock
  btfss PLLRDY, a ; skip next if PLLRDY=1
  bra _wait_stable_clock

  ; stack overflow
  btfsc STKFUL, a ; skip next if STKFUL=0
  bra bootloader
  ; stack underflow
  btfsc STKUNF, a ; skip next if STKUNF=0
  bra bootloader
  ; watchdog
  btfss TO, a ; skip next if TO=1
  bra bootloader
  ; reset instruction
  btfss RI, a ; skip next if RI=1
  bra _check_reset_magic
  ; power-on reset
  btfss POR, a ; skip next if POR=1
  bra _run_application
  ; brown-out reset
  btfss BOR, a ; skip next if BOR=1
  bra _run_application

  bra bootloader

_check_reset_magic:
  movff RESET_MAGIC, WREG
  sublw 0x42
  bz bootloader

_run_application:
  fcall crc_check
  tstfsz WREG, a ; skip next if WREG=0
  goto application_main
  ; if CRC16 check failed, continue to the bootloader

bootloader:
  movlw ~(PMD0_USBMD_MASK | PMD0_ACTMD_MASK)
  movwf PMD0, a
  setf PMD1, a

  clrf STKPTR, a
  movlw RCON_IPEN_MASK | RCON_RI_MASK | RCON_TO_MASK | RCON_PD_MASK | RCON_POR_MASK | RCON_BOR_MASK
  movwf RCON, a

  ; enable active clock tuning based on USB
  clrf ACTCON, a
  movlw ACTCON_ACTEN_MASK | ACTCON_ACTSRC_MASK
  movwf ACTCON, a

  ; set all interrupt control registers to their POR values
  setf INTCON2, a
  movlw INTCON3_INT2IP_MASK | INTCON3_INT1IP_MASK
  movwf INTCON3, a

  banksel RESET_MAGIC
  clrf BANKMASK(RESET_MAGIC), b
  clrf SYS_RESET, a

  fcall usb_reset

_bootloader_detached:
  fcall usb_attach

_bootloader_attached:
  clrwdt
  ; keep looping until SE0 condition ends
  btfsc SE0, a ; skip next if SE0=0
  bra _bootloader_attached

  clrf UIR, a

_bootloader_powered:
  clrwdt
  ; keep looping until USB reset
  btfss URSTIF, a ; skip next if URSTIF=1
  bra _bootloader_powered

  fcall usb_reset

_bootloader_default:
  clrwdt
  tstfsz SYS_RESET, a ; skip next if SYS_RESET=0
  rcall _sys_reset_loop
  btfsc URSTIF, a ; skip next if URSTIF=0
  call usb_reset

  btfsc STALLIF, a ; skip next if STALLIF=0
  bcf STALLIF, a

  btfsc SOFIF, a ; skip next if SOFIF=0
  call usb_sof_handler

  btfss TRNIF, a ; skip next if TRNIF=1
  bra _bootloader_default

  movf USTAT, w, a
  movwf USTAT_SAVE, a
  bcf TRNIF, a
  andlw 0b01111000
  bz _ep0_transfer

  ; > 24.2.3 USB status register
  ; > If the next data in the FIFO holding register is valid, the SIE will reassert the interrupt within
  ; > 6 Tcy of clearing TRNIF
  ; ANDLW(1) + BZ(1) + BRA(2) + CLRDT (1) + BTFSC (1) = 6
  bra _bootloader_default

_ep0_transfer:
  movf USTAT_SAVE, w, a
  fcall usb_service_ep0
  bra _bootloader_default

_sys_reset_loop:
  decfsz SYS_RESET, a
  bra _reset_delay
  fcall usb_detach
  rcall _reset_delay
  reset

; Inputs:
;   W: desired RESET_MAGIC value
sys_prepare_reset:
  lfsr 0, RESET_MAGIC
  movwf INDF0, a
  movlw 0x20
  movwf SYS_RESET, a
  return

_reset_delay:
  clrf DELAY_CNT, a
  movlw 0x40
  movwf DELAY_CNT+1, a
_reset_delay_loop:
  clrwdt
  decfsz DELAY_CNT, a
  bra _reset_delay_loop
  decfsz DELAY_CNT+1, a
  bra _reset_delay_loop
  return

  end
