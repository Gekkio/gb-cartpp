// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define U16_FROM_LE_BYTES(lsb, msb) ((uint16_t)((msb << 8) | lsb))

extern const uint16_t bootloader_version __at(0x0010);
extern volatile uint8_t reset_magic __at(0x7ff);

#ifdef DEBUG
void panic(const char *msg);
#endif

#define FLIP_BIT(v)                                                            \
  do {                                                                         \
    if (v) {                                                                   \
      v = 0;                                                                   \
    } else {                                                                   \
      v = 1;                                                                   \
    }                                                                          \
  } while (0);

struct ReadSlice {
  const __ram uint8_t *ptr;
  uint8_t len;
};

struct WriteSlice {
  __ram uint8_t *ptr;
  uint8_t len;
};

struct VolatileReadSlice {
  const volatile __ram uint8_t *ptr;
  uint8_t len;
};

struct VolatileWriteSlice {
  volatile __ram uint8_t *ptr;
  uint8_t len;
};

__bit peie_save;

#define BEGIN_CRITICAL_SECTION()                                               \
  do {                                                                         \
    peie_save = INTCONbits.PEIE;                                               \
    INTCONbits.PEIE = 0;                                                       \
  } while (0);

#define END_CRITICAL_SECTION()                                                 \
  do {                                                                         \
    if (peie_save) {                                                           \
      INTCONbits.PEIE = 1;                                                     \
    }                                                                          \
  } while (0);

#endif /* COMMON_H */
