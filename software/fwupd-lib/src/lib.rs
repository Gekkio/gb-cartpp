// SPDX-FileCopyrightText: 2019-2020 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use std::error::Error;
use std::fmt;

pub mod bootloader;
pub mod fw_image;
mod usb;

pub use bootloader::*;
pub use fw_image::*;
pub use usb::*;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub struct FirmwareVersion {
    pub major: u8,
    pub minor: u8,
}

impl fmt::Display for FirmwareVersion {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match (self.major, self.minor) {
            (0xff, 0xff) => write!(f, "???"),
            (major, minor) => write!(f, "{}.{}", major, minor),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum DriverError {
    UsbPipe,
    UsbTimeout,
    UsbIo,
    NoDevice,
    UnsupportedUsbOperation,
    Other(i32, &'static str),
}

impl fmt::Display for DriverError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            DriverError::UsbPipe => write!(f, "USB pipe error"),
            DriverError::UsbTimeout => write!(f, "USB operation timed out"),
            DriverError::UsbIo => write!(f, "USB I/O error"),
            DriverError::NoDevice => write!(f, "No such device (device disconnected?)"),
            DriverError::UnsupportedUsbOperation => write!(f, "Unsupported USB operation"),
            DriverError::Other(_, msg) => write!(f, "{}", msg),
        }
    }
}

impl Error for DriverError {}

pub(crate) const FLASH_BLOCK_SIZE: usize = 64;
pub(crate) const CONFIG_BLOCK_SIZE: usize = 14;

#[derive(Copy, Clone, Eq, PartialEq)]
pub enum VerifyResult {
    Valid,
    Invalid { errors: u32, first_error_addr: u32 },
}

impl VerifyResult {
    fn mark_error(&mut self, addr: u32) {
        match self {
            VerifyResult::Valid => {
                *self = VerifyResult::Invalid {
                    errors: 1,
                    first_error_addr: addr,
                }
            }
            VerifyResult::Invalid { errors, .. } => *errors += 1,
        }
    }
}

impl fmt::Debug for VerifyResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            VerifyResult::Valid => f.debug_struct("Valid").finish(),
            VerifyResult::Invalid {
                errors,
                first_error_addr,
            } => f
                .debug_struct("Invalid")
                .field("errors", errors)
                .field(
                    "first_error_addr",
                    &format_args!("{:#06x}", first_error_addr),
                )
                .finish(),
        }
    }
}
