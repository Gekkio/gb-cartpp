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
use crate::usb::{CtrlRequestParams, UsbDevice, UsbDeviceMode, VendorCtrlRequest};
use crate::DriverError;

#[derive(Debug)]
pub enum BootloaderMode {}
impl UsbDeviceMode for BootloaderMode {}

impl UsbDevice<BootloaderMode> {
    pub fn unlock(&self) -> Result<(), DriverError> {
        self.handle.ctrl_request(
            VendorCtrlRequest::Unlock,
            CtrlRequestParams::Out {
                value: 0xc2f2,
                index: 0xf09a,
                data: None,
            },
        )?;
        Ok(())
    }
    pub fn lock(&self) -> Result<(), DriverError> {
        self.handle.ctrl_request(
            VendorCtrlRequest::Lock,
            CtrlRequestParams::Out {
                value: 0,
                index: 0,
                data: None,
            },
        )?;
        Ok(())
    }
    pub fn read(&self, addr: u32, buffer: &mut [u8]) -> Result<usize, DriverError> {
        assert!(!buffer.is_empty());
        self.handle.ctrl_request(
            VendorCtrlRequest::Read,
            CtrlRequestParams::In {
                value: addr as u16,
                index: (addr >> 16) as u16,
                data: Some(buffer),
            },
        )
    }
    pub fn erase_flash(&self, addr: u32) -> Result<(), DriverError> {
        assert!(addr < 0x00_8000);
        self.handle.ctrl_request(
            VendorCtrlRequest::EraseFlash,
            CtrlRequestParams::Out {
                value: addr as u16,
                index: (addr >> 16) as u16,
                data: None,
            },
        )?;
        Ok(())
    }
    pub fn write_flash(&self, addr: u32, data: &[u8]) -> Result<(), DriverError> {
        assert!(!data.is_empty());
        assert!(addr & 0x3f == 0);
        assert!((addr as usize + data.len()) <= 0x00_8000);
        self.handle.ctrl_request(
            VendorCtrlRequest::WriteFlash,
            CtrlRequestParams::Out {
                value: addr as u16,
                index: (addr >> 16) as u16,
                data: Some(data),
            },
        )?;
        Ok(())
    }
    pub fn write_cfg(&self, addr: u32, data: &[u8]) -> Result<(), DriverError> {
        assert!(!data.is_empty() && data.len() <= 16);
        assert!(addr >= 0x30_0000);
        assert!((addr as usize + data.len()) <= 0x30_0010);
        self.handle.ctrl_request(
            VendorCtrlRequest::WriteCfg,
            CtrlRequestParams::Out {
                value: addr as u16,
                index: (addr >> 16) as u16,
                data: Some(data),
            },
        )?;
        Ok(())
    }
    pub fn write_id(&self, addr: u32, data: &[u8]) -> Result<(), DriverError> {
        assert!(!data.is_empty() && data.len() <= 8);
        assert!(addr >= 0x20_0000);
        assert!((addr as usize + data.len()) <= 0x20_0008);
        self.handle.ctrl_request(
            VendorCtrlRequest::WriteId,
            CtrlRequestParams::Out {
                value: addr as u16,
                index: (addr >> 16) as u16,
                data: Some(data),
            },
        )?;
        Ok(())
    }
    pub fn read_byte(&self, addr: u32) -> Result<u8, DriverError> {
        let mut buffer = [0u8];
        assert_eq!(self.read(addr, &mut buffer)?, 1);
        Ok(buffer[0])
    }
    pub fn read_to_vec(&self, addr: u32, len: usize) -> Result<Vec<u8>, DriverError> {
        let mut buffer = vec![0; len];
        assert_eq!(self.read(addr, &mut buffer)?, len);
        Ok(buffer)
    }
}
