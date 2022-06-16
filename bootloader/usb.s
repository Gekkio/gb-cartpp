; SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
;
; SPDX-License-Identifier: MIT OR Apache-2.0

; Main USB functions

#include <xc.inc>
#include "pic_extra.inc"
#include "protocol.inc"
#include "usb.inc"

  global usb_reset, usb_attach, usb_detach, usb_service_ep0, usb_sof_handler
  global usb_assert_request_type, usb_assert_value, usb_assert_no_data, usb_assert_index
  global usb_save_rom_data_addr, usb_load_rom_data_addr, usb_save_ram_data_addr
  global EP0_OUT_BUFFER, EP0_OUT_CNT, DEV_CFG, TX_STATF, TX_ADDR, DATA_ADDR, DATA_CNT
  extrn memcpy_fsr0_from_tblptr, memclear_fsr0, memcpy_fsr0_from_fsr1
  extrn usb_ep0_std_setup
  extrn usb_vendor_setup, usb_vendor_out_data

  psect usb_ram,class=BANK4,space=SPACE_DATA,noexec,reloc=0x100
USB_RAM:
; BD0STAT: EP0 OUT
EP0_OUT_STAT: ds 1
EP0_OUT_CNT: ds 1
EP0_OUT_ADR: ds 2
; BD1STAT: EP0 IN
EP0_IN_STAT: ds 1
EP0_IN_CNT: ds 1
EP0_IN_ADR: ds 2
; EP0 buffers
EP0_OUT_BUFFER: ds EP0_BUFFER_SIZE
EP0_IN_BUFFER: ds EP0_BUFFER_SIZE

  psect udata_acs
; USB device state
DEV_CFG: ds 1
; USB transaction state
TX_STATF: ds 1
TX_DATAF: ds 1
TX_ADDR: ds 1
TX_SOF: ds 1
DATA_CNT: ds 2
DATA_ADDR: ds 3

  psect code

usb_detach:
  ; > 24.2 USB Status and Control
  ; > when disabling the USB module, make sure the SUSPND bit is clear prior to clearing the USBEN bit"
  bcf SUSPND
  clrf UCON, a
  clrf UCFG, a

  ; reset all endpoints
  lfsr 0, UEP0
  movlw 0x10
  ljmp memclear_fsr0

usb_reset:
  banksel(USB_RAM)

  ; clear USB interrupts
  clrf UEIR, a
  clrf UIR, a
  clrf UEIE, a
  clrf UIE, a

  ; use full speed mode with pullups
  movlw UCFG_UPUEN_MASK | UCFG_FSEN_MASK
  movwf UCFG, a

  ; clear 0x400-0x4ff
  lfsr 0, 0x400
  movlw 0x00
  fcall memclear_fsr0

  ; reset ping pong pointers
  bsf PPBRST
  ; clear USB address
  clrf UADDR, a
  ; clear configuration number
  clrf DEV_CFG, a
  ; enable packet processing
  bcf PKTDIS
  ; stop resetting ping pong pointers
  bcf PPBRST

_flush_transactions:
  btfss TRNIF ; skip next if TRNIF=1
  bra _ep0_init

  bcf TRNIF
  ; > 24.2.3 USB status register
  ; > If the next data in the FIFO holding register is valid, the SIE will reassert the interrupt within
  ; > 6 Tcy of clearing TRNIF
  ; 4xNOP(1) + BRA(2) = 6
  nop
  nop
  nop
  nop
  bra _flush_transactions

_ep0_init:
  movlw UEP0_EPHSHK_MASK | UEP0_EPOUTEN_MASK | UEP0_EPINEN_MASK
  movwf UEP0, a
  ; set endpoint 0 buffer addresses
  movlw low EP0_OUT_BUFFER
  movwf BANKMASK(EP0_OUT_ADR), b
  movlw low EP0_IN_BUFFER
  movwf BANKMASK(EP0_IN_ADR), b
  movlw high EP0_IN_BUFFER
  movwf BANKMASK(EP0_OUT_ADR+1), b
  movwf BANKMASK(EP0_IN_ADR+1), b

ep0_out_arm_setup:
  movlw (1 << BDSTAT_DTSEN) | (1 << BDSTAT_BSTALL)
ep0_out_arm:
  movwf BANKMASK(EP0_OUT_STAT), b
  movlw EP0_BUFFER_SIZE
  movwf BANKMASK(EP0_OUT_CNT), b
  bsf BANKMASK(EP0_OUT_STAT), UOWN, b
  return
ep0_out_arm_data:
  rcall _ep0_dts_toggle
  bra ep0_out_arm
ep0_out_arm_status:
  movlw (1 << BDSTAT_DTS) | (1 << BDSTAT_DTSEN)
  bra ep0_out_arm

ep0_in_arm_stall:
  movlw (1 << BDSTAT_BSTALL)
ep0_in_arm:
  movwf BANKMASK(EP0_IN_STAT), b
  bsf BANKMASK(EP0_IN_STAT), UOWN, b
  return
ep0_in_arm_data:
  rcall _ep0_dts_toggle
  bra ep0_in_arm
ep0_in_arm_status:
  clrf BANKMASK(EP0_IN_CNT), b
  movlw (1 << BDSTAT_DTS) | (1 << BDSTAT_DTSEN)
  bra ep0_in_arm

_ep0_dts_toggle:
  movlw (1 << BDSTAT_DTSEN)
  btfsc TX_DATAF, TX_DATAF_DTS, a ; skip next if TX_DATAF_DTS=0
  bsf WREG, BDSTAT_DTS, a
  btg TX_DATAF, TX_DATAF_DTS, a
  return

; Attaches to the USB bus by enabling the USB module
;
; Inputs: -
usb_attach:
  clrf UCON, a
_usb_attach_poll:
  bsf USBEN
  btfss USBEN ; skip next if USBEN=1
  bra _usb_attach_poll
  return

_ep0_cancel_tx:
  bcf BANKMASK(EP0_OUT_STAT), UOWN, b
  bcf BANKMASK(EP0_IN_STAT), UOWN, b
  clrf TX_SOF, a
  clrf TX_STATF, a
  clrf DATA_CNT, a
  clrf DATA_CNT+1, a
  return

usb_sof_handler:
  bcf SOFIF
  btfsc TX_STATF, TX_STATF_DIR_OUT, a ; skip next if TX_STATF_DIR_OUT=0
  bra _ep0_timeout_check
  btfsc TX_STATF, TX_STATF_DIR_IN, a ; skip next if TX_STATF_DIR_IN=0
  bra _ep0_timeout_check
  return

_ep0_timeout_check:
  incfsz TX_SOF, a ; skip next if TX_SOF goes to 0
  return
  rcall _ep0_cancel_tx
  bra _ep0_stall

; Inputs:
;   W: copy of USTAT
usb_service_ep0:
  banksel(USB_RAM)
  btfsc WREG, USTAT_DIR_POSITION, a ; skip next if DIR=0
  bra _ep0_in
  ; DIR=0: either OUT or SETUP
  movf BANKMASK(EP0_OUT_STAT), w, b
  andlw 0b00111100
  sublw (PID_SETUP << 2)
  bnz _ep0_out

_ep0_setup:
  rcall _ep0_cancel_tx
  rcall _ep0_setup_request

  movwf TX_STATF, a
  bcf PKTDIS
  bsf TX_DATAF, TX_DATAF_DTS, a
  btfsc TX_STATF, TX_STATF_DIR_OUT, a ; skip next if TX_STATF_DIR_OUT=0
  bra _ep0_setup_done_out
  btfsc TX_STATF, TX_STATF_DIR_IN, a ; skip next if TX_STATF_DIR_IN=0
  bra _ep0_setup_done_in

_ep0_stall:
  clrf TX_STATF, a
  rcall ep0_out_arm_setup
  bra ep0_in_arm_stall

_ep0_setup_request:
  movlw 0b01100000
  andwf BANKMASK(EP0_OUT_BUFFER + bmRequestType), w, b
  ; bmRequestType bits 6-5 are 0b00 if it's a standard request
  btfsc ZERO ; skip next if Z=0
  goto usb_ep0_std_setup
  ; bmRequestType bits 6-5 are 0b10 if it's a vendor request
  sublw 0b01000000
  btfsc ZERO ; skip next if Z=0
  goto usb_vendor_setup

  retlw TX_STATF_REJECT

_ep0_setup_done_out:
  btfsc TX_STATF, TX_STATF_DATA_STAGE, a ; skip next if TX_STATF_DATA_STAGE=0
  bra _ep0_setup_done_out_data

  rcall ep0_in_arm_status
  bra ep0_out_arm_setup
_ep0_setup_done_out_data:
  movff EP0_OUT_BUFFER + wLengthMsb, DATA_CNT+1
  movff EP0_OUT_BUFFER + wLengthLsb, DATA_CNT
  bra ep0_out_arm_data

_ep0_setup_done_in:
  btfsc TX_STATF, TX_STATF_DATA_STAGE, a ; skip next if TX_STATF_DATA_STAGE=0
  bra _ep0_setup_done_in_data

  rcall ep0_out_arm_status
  bra ep0_in_arm_stall
_ep0_setup_done_in_data:
  ; W = wLengthMsb - DATA_CNT+1
  movf DATA_CNT+1, w, a
  subwf BANKMASK(EP0_OUT_BUFFER + wLengthMsb), w, b
  ; wLengthMsb < DATA_CNT+1
  bnc _ep0_cnt_truncate
  ; wLengthMsb > DATA_CNT+1
  bnz _ep0_in_serve_data
  ; wLengthMsb = DATA_CNT+1
_ep0_cnt_check_lsb:
  ; W = IN_CNT - wLengthLsb
  movf BANKMASK(EP0_OUT_BUFFER + wLengthLsb), w, b
  subwf DATA_CNT, w, a
  ; IN_CNT = wLengthLsb
  bz _ep0_cnt_exact
  ; IN_CNT < wLengthLsb
  bnc _ep0_in_serve_data
  ; IN_CNT > wLengthLsb
_ep0_cnt_truncate:
  movff EP0_OUT_BUFFER + wLengthMsb, DATA_CNT+1
  movff EP0_OUT_BUFFER + wLengthLsb, DATA_CNT
_ep0_cnt_exact:
  bsf TX_DATAF, TX_DATAF_EXACT, a
  bra _ep0_in_serve_data

; Inputs:
;   W: expected bmRequestType
usb_assert_request_type:
  cpfseq BANKMASK(EP0_OUT_BUFFER + bmRequestType), b
  bra _usb_assert_fail
  return
_usb_assert_fail:
  pop
  retlw TX_STATF_REJECT

; Inputs:
;   W: expected wValue
usb_assert_value:
  cpfseq BANKMASK(EP0_OUT_BUFFER + wValueLsb), b
  bra _usb_assert_fail
  tstfsz BANKMASK(EP0_OUT_BUFFER + wValueMsb), b ; skip next if wValueMsb=0
  bra _usb_assert_fail
  return

; Inputs:
;   W: expected wIndex
usb_assert_index:
  cpfseq BANKMASK(EP0_OUT_BUFFER + wIndexLsb), b
  bra _usb_assert_fail
  tstfsz BANKMASK(EP0_OUT_BUFFER + wIndexMsb), b ; skip next if wIndexMsb=0
  bra _usb_assert_fail
  return

usb_assert_no_data:
  tstfsz BANKMASK(EP0_OUT_BUFFER + wLengthLsb), b ; skip next if wLengthLsb=0
  bra _usb_assert_fail
  tstfsz BANKMASK(EP0_OUT_BUFFER + wLengthMsb), b ; skip next if wLengthMsb=0
  bra _usb_assert_fail
  return

_ep0_out:
  movf TX_STATF, w, a
  andlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN) | (1 << TX_STATF_DIR_OUT)
  sublw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_OUT)
  bnz _ep0_out_done
_ep0_out_data:
  fcall usb_vendor_out_data
  movf BANKMASK(EP0_OUT_CNT), w, b
  subwf DATA_CNT, a
  movlw 0x00
  subwfb DATA_CNT+1, a

  tstfsz DATA_CNT+1, a ; skip next if DATA_CNT+1=0
  bra ep0_out_arm_data
  tstfsz DATA_CNT, a ; skip next if DATA_CNT=0
  bra ep0_out_arm_data
_ep0_out_status:
  bcf TX_STATF, TX_STATF_DATA_STAGE, a
  clrf BANKMASK(EP0_IN_CNT), b
  rcall ep0_in_arm_status
  bra ep0_out_arm_setup
_ep0_out_done:
  bcf TX_STATF, TX_STATF_DIR_OUT, a
  bra ep0_out_arm_setup

_ep0_in:
  btfsc TX_STATF, TX_STATF_ADDR, a ; skip next if TX_STATF_ADDR=0
  movff TX_ADDR, UADDR
  movf TX_STATF, w, a
  andlw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN) | (1 << TX_STATF_DIR_OUT)
  sublw (1 << TX_STATF_DATA_STAGE) | (1 << TX_STATF_DIR_IN)
  bnz _ep0_in_done
_ep0_in_data:
  movf BANKMASK(EP0_IN_CNT), w, b
  subwf DATA_CNT, a
  movlw 0x00
  subwfb DATA_CNT+1, a

  tstfsz DATA_CNT+1, a ; skip next if DATA_CNT+1=0
  bra _ep0_in_serve_data
  tstfsz DATA_CNT, a ; skip next if DATA_CNT=0
  bra _ep0_in_serve_data

  btfss TX_DATAF, TX_DATAF_EXACT, a ; skip next if TX_DATAF_EXACT=1
  bra _ep0_in_zlp_check

_ep0_in_status:
  bcf TX_STATF, TX_STATF_DATA_STAGE, a
  rcall ep0_out_arm_status
  bra ep0_in_arm_stall

_ep0_in_zlp_check:
  movlw EP0_BUFFER_SIZE
  cpfseq BANKMASK(EP0_IN_CNT), b ; skip next if EP0_IN_CNT == EP0_BUFFER_SIZE
  bra _ep0_in_status

_ep0_in_zlp:
  clrf BANKMASK(EP0_IN_CNT), b
  bra ep0_in_arm_data

_ep0_in_done:
  bcf TX_STATF, TX_STATF_DIR_IN, a
  bra ep0_in_arm_stall

_ep0_in_serve_data:
  rcall _ep0_in_buffer_cnt
  rcall _ep0_in_buffer_memcpy
  bra ep0_in_arm_data

_ep0_in_buffer_cnt:
  tstfsz DATA_CNT+1, a ; skip next if DATA_CNT+1=0
  retlw EP0_BUFFER_SIZE
  movlw EP0_BUFFER_SIZE
  cpfslt DATA_CNT, a ; skip next if DATA_CNT < EP0_BUFFER_SIZE
  return
  movf DATA_CNT, w, a
  return

; Inputs:
;   W: byte count
;   DATA_ADDR: source address
_ep0_in_buffer_memcpy:
  movwf BANKMASK(EP0_IN_CNT), b
  movf BANKMASK(EP0_IN_CNT), w, b
  btfsc ZERO ; skip next if Z=0
  return
  lfsr 0, EP0_IN_BUFFER
  btfss TX_STATF, TX_STATF_RAM_DATA, a ; skip next if TX_STATF_RAM_DATA=1
  bra _ep0_in_buffer_memcpy_rom

; Inputs:
;   DATA_ADDR: RAM source address
;   W: byte count
; Outputs:
;   DATA_ADDR: RAM source adddres
_ep0_in_buffer_memcpy_ram:
  movff DATA_ADDR, FSR1L
  movff DATA_ADDR+1, FSR1H
  fcall memcpy_fsr0_from_fsr1
usb_save_ram_data_addr:
  movff FSR1L, DATA_ADDR
  movff FSR1H, DATA_ADDR+1
  return

; Inputs:
;   DATA_ADDR: ROM source address
;   W: byte count
; Outputs:
;   DATA_ADDR: ROM source adddres
_ep0_in_buffer_memcpy_rom:
  rcall usb_load_rom_data_addr
  fcall memcpy_fsr0_from_tblptr
usb_save_rom_data_addr:
  movff TBLPTRL, DATA_ADDR
  movff TBLPTRH, DATA_ADDR+1
  movff TBLPTRU, DATA_ADDR+2
  return

usb_load_rom_data_addr:
  movff DATA_ADDR, TBLPTRL
  movff DATA_ADDR+1, TBLPTRH
  movff DATA_ADDR+2, TBLPTRU
  return

  end
