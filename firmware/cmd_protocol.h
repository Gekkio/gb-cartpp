// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

#ifndef CMD_PROTOCOL_H
#define CMD_PROTOCOL_H

// 5-bit command
enum Command {
  CMD_PING = 0x01,
  CMD_UNLOCK = 0x02,
  CMD_SET_MODE = 0x03,
  CMD_GET_MODE = 0x04,
  CMD_READ = 0x05,
  CMD_READ_BURST = 0x06,
  CMD_WRITE = 0x07,
  CMD_WRITE_BURST = 0x08,
  CMD_POLL_FLASH_DATA = 0x09,
  CMD_SET_FLASH_WRITE_SEQUENCE = 0x0a,
  CMD_FLASH_BURST = 0x0b,

  CMD_DIAGNOSTICS = 0x1d,
  CMD_RESET = 0x1e,
  CMD_IDENTIFY = 0x1f,
};

#define CMD_MASK 0x1f

#endif /* CMD_PROTOCOL_H */
