// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

struct UsbErrors {
  uint8_t pid_cnt;
  uint8_t crc5_cnt;
  uint8_t crc16_cnt;
  uint8_t dfn8_cnt;
  uint8_t bto_cnt;
  uint8_t bts_cnt;
  union {
    struct {
      unsigned pid_flag : 1;
      unsigned crc5_flag : 1;
      unsigned crc16_flag : 1;
      unsigned dfn8_flag : 1;
      unsigned bto_flag : 1;
      unsigned bts_flag : 1;
      unsigned : 2;
    };
    uint8_t flags;
  };
};

struct Diagnostics {
  uint16_t initial_res_voltage;
  uint8_t initial_rcon;
  uint8_t initial_stkptr;
  struct UsbErrors usb_errors;
};

extern volatile __bank(1) struct Diagnostics diagnostics;

#endif /* DIAGNOSTICS_H */
