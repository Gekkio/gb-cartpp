// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#include "config.h"

inline void osc_init(void) {
  OSCCONbits.IRCF = 0b111; // set internal oscillator to 16 MHz
  OSCCON2bits.INTSRC = 0;
  while (!OSCCON2bits.LFIOFS) {
  }
  while (!OSCCONbits.HFIOFS) {
  }
  OSCCON2bits.PLLEN = 1;
  while (!OSCCON2bits.PLLRDY) {
  }
  ACTCONbits.ACTEN = 1;
}

inline void osc_switch_fast(void) {
  OSCCON2bits.PLLEN = 1;
  OSCCONbits.IRCF = 0b111;
  OSCCONbits.SCS = 0b00;
  while (!OSCCONbits.HFIOFS) {
  }
  while (!OSCCON2bits.PLLRDY) {
  }
  ACTCONbits.ACTEN = 1;
}

inline void osc_switch_slow(void) {
  ACTCONbits.ACTEN = 0;
  OSCCON2bits.PLLEN = 0;
  OSCCONbits.IRCF = 0b000;
  OSCCONbits.SCS = 0b11;
}
