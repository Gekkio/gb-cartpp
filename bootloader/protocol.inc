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

; USB vendor protocol constants

BOOTLOADER_VERSION_MAJOR equ 0x01
BOOTLOADER_VERSION_MINOR equ 0x01

VENDOR_REQUEST_RESET       equ 0x40
VENDOR_REQUEST_IDENTIFY    equ 0x41
VENDOR_REQUEST_UNLOCK      equ 0x42
VENDOR_REQUEST_LOCK        equ 0x43
VENDOR_REQUEST_READ        equ 0x44
VENDOR_REQUEST_ERASE_FLASH equ 0x45
VENDOR_REQUEST_WRITE_FLASH equ 0x46
VENDOR_REQUEST_WRITE_CFG   equ 0x47
VENDOR_REQUEST_WRITE_ID    equ 0x48
