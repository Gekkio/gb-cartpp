// Copyright (C) 2019-2020 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
use crate::fw_image::FirmwareImage;
use crate::usb::{BootloaderMode, Unclaimed, UsbDevice, UsbDeviceKind};
use crate::{DriverError, FirmwareVersion, VerifyResult, FLASH_BLOCK_SIZE};

pub struct BootloaderDriver {
    device: UsbDevice<BootloaderMode>,
    fw_version: FirmwareVersion,
    bl_version: FirmwareVersion,
}

impl BootloaderDriver {
    pub fn initialize(device: UsbDevice<Unclaimed>) -> Result<BootloaderDriver, DriverError> {
        let bl;
        let fw;
        if let UsbDeviceKind::Bootloader {
            bl_version,
            fw_version,
        } = device.kind
        {
            bl = bl_version;
            fw = fw_version;
        } else {
            panic!("Not in bootloader mode");
        }
        let device = device.claim_bootloader()?;
        device.unlock()?;
        Ok(BootloaderDriver {
            device,
            fw_version: fw,
            bl_version: bl,
        })
    }
    pub fn deinitialize(self) -> Result<UsbDevice<Unclaimed>, DriverError> {
        self.device.release()
    }
    pub fn bootloader_version(&self) -> FirmwareVersion {
        self.bl_version
    }
    pub fn firmware_version(&self) -> FirmwareVersion {
        self.fw_version
    }
    pub fn reset(self) -> Result<(), DriverError> {
        self.device.reset()
    }
    pub fn reset_bootloader(self) -> Result<(), DriverError> {
        self.device.enter_bootloader()
    }
    pub fn write_flash<F: FnMut(u32) -> ()>(
        &self,
        fw: &FirmwareImage,
        mut cb: F,
    ) -> Result<(), DriverError> {
        for (addr, block_data) in fw.iter_flash_blocks() {
            if block_data.iter().all(|&byte| byte == 0xff) {
                self.device.erase_flash(addr)?;
            } else {
                self.device.write_flash(addr, block_data)?;
            }
            cb(addr);
        }
        Ok(())
    }
    pub fn calc_flash_checksum(&self) -> Result<u16, DriverError> {
        let mut data = Vec::new();
        let mut buffer = [0; 0x100];
        for chunk in 0x08..0x80 {
            self.device.read(chunk << 8, &mut buffer)?;
            data.extend(&buffer[..]);
        }
        Ok(crc16::State::<crc16::XMODEM>::calculate(&data))
    }
    pub fn verify_flash<F: FnMut(u32, VerifyResult) -> ()>(
        &self,
        fw: &FirmwareImage,
        mut cb: F,
    ) -> Result<VerifyResult, DriverError> {
        let mut actual: [u8; FLASH_BLOCK_SIZE] = [0xff; FLASH_BLOCK_SIZE];
        let mut result = VerifyResult::Valid;
        for (block_addr, expected) in fw.iter_flash_blocks() {
            self.device.read(block_addr, &mut actual)?;
            for (idx, (actual, expected)) in actual.iter().zip(expected.iter()).enumerate() {
                let addr = block_addr | (idx as u32);
                if actual != expected {
                    result.mark_error(addr);
                }
            }
            cb(block_addr, result);
        }
        Ok(result)
    }
    pub fn write_id(&self, fw: &FirmwareImage) -> Result<(), DriverError> {
        for (addr, byte) in fw.iter_id_bytes() {
            self.device.write_id(addr, &[byte])?;
        }
        Ok(())
    }
    pub fn verify_id(&self, fw: &FirmwareImage) -> Result<VerifyResult, DriverError> {
        let mut result = VerifyResult::Valid;
        for (addr, expected) in fw.iter_id_bytes() {
            let actual = self.device.read_byte(addr)?;
            if actual != expected {
                result.mark_error(addr);
            }
        }
        Ok(result)
    }
    pub fn write_cfg(&self, fw: &FirmwareImage) -> Result<(), DriverError> {
        for (addr, byte) in fw.iter_config_bytes() {
            self.device.write_cfg(addr, &[byte])?;
        }
        Ok(())
    }
    pub fn verify_cfg(&self, fw: &FirmwareImage) -> Result<VerifyResult, DriverError> {
        let mut result = VerifyResult::Valid;
        for (addr, expected) in fw.iter_id_bytes() {
            let actual = self.device.read_byte(addr)?;
            if actual != expected {
                result.mark_error(addr);
            }
        }
        Ok(result)
    }
}
