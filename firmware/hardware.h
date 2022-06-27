// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef HARDWARE_H
#define HARDWARE_H

inline void osc_init(void);
inline void osc_switch_fast(void);
inline void osc_switch_slow(void);

#define VCART_EN_OUT(v)                                                        \
  do {                                                                         \
    LATEbits.LATE2 = v;                                                        \
  } while (0);

#define PHI_PIN_OUT(v)                                                         \
  do {                                                                         \
    LATCbits.LATC6 = v;                                                        \
  } while (0);

#define WR_PIN_OUT(v)                                                          \
  do {                                                                         \
    LATCbits.LATC7 = v;                                                        \
  } while (0);

#define RD_PIN_OUT(v)                                                          \
  do {                                                                         \
    LATCbits.LATC2 = v;                                                        \
  } while (0);

#define CS_PIN_OUT(v)                                                          \
  do {                                                                         \
    LATCbits.LATC0 = v;                                                        \
  } while (0);

#define ADDRH_BUS_OUT(addr)                                                    \
  do {                                                                         \
    LATA = addr;                                                               \
  } while (0);

#define ADDRL_BUS_OUT(addr)                                                    \
  do {                                                                         \
    LATD = addr;                                                               \
  } while (0);

#define ADDR_BUS_OUT(addr)                                                     \
  do {                                                                         \
    ADDRL_BUS_OUT((uint8_t)addr);                                              \
    ADDRH_BUS_OUT(addr >> 8);                                                  \
  } while (0);

#define A15_PIN_OUT(v)                                                         \
  do {                                                                         \
    LATAbits.LATA7 = v;                                                        \
  } while (0);

#define DATA_BUS_IN() (PORTB)
#define DATA_BUS_OUT(data)                                                     \
  do {                                                                         \
    LATB = data;                                                               \
  } while (0);

#define RES_PIN_IN(v) (PORTEbits.RE0)
#define RES_PIN_OUT(v)                                                         \
  do {                                                                         \
    TRISEbits.TRISE0 = v;                                                      \
  } while (0);

#define VIN_PIN_OUT(v)                                                         \
  do {                                                                         \
    LATCbits.LATC1 = v;                                                        \
  } while (0);

#define ENABLE_DATA_BUS_PULLUPS()                                              \
  do {                                                                         \
    WPUB = 0xff;                                                               \
    INTCON2bits.RBPU = 0;                                                      \
  } while (0);

#define DISABLE_DATA_BUS_PULLUPS()                                             \
  do {                                                                         \
    INTCON2bits.RBPU = 1;                                                      \
    WPUB = 0x00;                                                               \
  } while (0);

#define READ_BYTE(addr, data, assert_cs, deassert_cs)                          \
  do {                                                                         \
    PHI_PIN_OUT(1);                                                            \
    ADDR_BUS_OUT(addr);                                                        \
    assert_cs(0);                                                              \
    PHI_PIN_OUT(0);                                                            \
    data = DATA_BUS_IN();                                                      \
    deassert_cs(1);                                                            \
  } while (0);

#define READ_BURST_FAST_BYTE(addr, data_ptr)                                   \
  do {                                                                         \
    ADDRL_BUS_OUT(addr);                                                       \
    addr += 1;                                                                 \
    *(data_ptr++) = DATA_BUS_IN();                                             \
  } while (0);

#define READ_BURST_FAST(addr, data_ptr, len)                                   \
  do {                                                                         \
    for (uint8_t i = len / 8; i > 0; i--) {                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
    }                                                                          \
    for (uint8_t i = len % 8; i > 0; i--) {                                    \
      READ_BURST_FAST_BYTE(addr, data_ptr);                                    \
    }                                                                          \
  } while (0);

#define READ_BURST(addr, data_ptr, len, assert_cs, deassert_cs)                \
  for (uint8_t i = len; i > 0; i--) {                                          \
    PHI_PIN_OUT(1);                                                            \
    ADDR_BUS_OUT(addr);                                                        \
    assert_cs(0);                                                              \
    PHI_PIN_OUT(0);                                                            \
    addr += 1;                                                                 \
    *(data_ptr++) = DATA_BUS_IN();                                             \
    deassert_cs(1);                                                            \
  }

#define WRITE_BYTE(addr, data, assert_cs, deassert_cs, assert_wr, deassert_wr) \
  do {                                                                         \
    PHI_PIN_OUT(1);                                                            \
    ADDR_BUS_OUT(addr);                                                        \
    assert_cs(0);                                                              \
    PHI_PIN_OUT(0);                                                            \
    assert_wr(0);                                                              \
    DATA_BUS_OUT(data);                                                        \
    TRISB = 0x00;                                                              \
    NOP();                                                                     \
    deassert_wr(1);                                                            \
    TRISB = 0xff;                                                              \
    deassert_cs(1);                                                            \
  } while (0);

#endif /* HARDWARE_H */
