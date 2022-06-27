// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef PIPE_H
#define PIPE_H

#include "common.h"
#include "usb.h"
#include <stdbool.h>
#include <stdint.h>

struct PipeState {
  union {
    struct {
      unsigned device_reset : 1;
      unsigned rx_slice_valid : 1;
      unsigned tx_slice_valid : 1;
      unsigned tx_need_zlp : 1;
    };
    uint8_t flags;
  };
  uint8_t reset_magic;
  struct VolatileReadSlice rx_slice;
  struct VolatileWriteSlice tx_slice;
};

extern volatile struct PipeState pipe_state;

void pipe_reset(void);
void pipe_rx_acquire(void);
void pipe_tx_acquire(void);
void pipe_tx_flush(void);

#endif /* PIPE_H */
