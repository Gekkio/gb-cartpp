// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use libusb1_sys::constants::*;
use libusb1_sys::*;
use std::char;
use std::ffi::CStr;
use std::fmt;
use std::marker::PhantomData;
use std::mem;
use std::ptr;
use std::rc::Rc;
use std::slice;

mod bootloader;

pub use crate::usb::bootloader::BootloaderMode;
use crate::{DriverError, FirmwareVersion};

#[derive(Debug)]
pub struct Usb {
    ctx: *mut libusb_context,
}

impl Drop for Usb {
    fn drop(&mut self) {
        unsafe { libusb_exit(self.ctx) };
    }
}

#[derive(Debug)]
pub struct UsbDeviceHandle {
    raw: *mut libusb_device_handle,
    _port_numbers: [u8; 7],
    pub(crate) address: u8,
    pub(crate) version: (u8, u8),
    _usb: Rc<Usb>,
}

impl Drop for UsbDeviceHandle {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            unsafe { libusb_close(self.raw) }
        }
    }
}

pub(crate) const LIBUSB_MAX_PAYLOAD: usize = 4096;

pub(crate) fn check_libusb(ret: i32) -> Result<usize, DriverError> {
    match ret {
        LIBUSB_SUCCESS => Ok(ret as usize),
        _ if ret >= 0 => Ok(ret as usize),
        LIBUSB_ERROR_PIPE => Err(DriverError::UsbPipe),
        LIBUSB_ERROR_TIMEOUT => Err(DriverError::UsbTimeout),
        LIBUSB_ERROR_NOT_SUPPORTED => Err(DriverError::UnsupportedUsbOperation),
        LIBUSB_ERROR_NO_DEVICE => Err(DriverError::NoDevice),
        LIBUSB_ERROR_IO => Err(DriverError::UsbIo),
        _ => Err(DriverError::Other(ret, unsafe {
            CStr::from_ptr(libusb_strerror(ret))
                .to_str()
                .unwrap_or_default()
        })),
    }
}

pub fn list_devices() -> Result<Vec<UsbDevice<Unclaimed>>, DriverError> {
    let usb = Usb::init()?;
    Usb::list_devices(&usb)
}

impl Usb {
    pub fn init() -> Result<Rc<Usb>, DriverError> {
        let mut ctx: *mut libusb_context = ptr::null_mut();
        check_libusb(unsafe { libusb_init(&mut ctx) })?;
        Ok(Rc::new(Usb { ctx }))
    }
    pub fn list_devices(usb: &Rc<Usb>) -> Result<Vec<UsbDevice<Unclaimed>>, DriverError> {
        let mut raw: *const *mut libusb_device = ptr::null_mut();
        let count = check_libusb(unsafe { libusb_get_device_list(usb.ctx, &mut raw) as i32 })?;
        let list = unsafe { slice::from_raw_parts(raw, count) };
        let result = Self::detect_devices(usb, list);
        unsafe { libusb_free_device_list(raw, 1) };
        result
    }
    fn open(
        usb: &Rc<Usb>,
        device: *mut libusb_device,
        descriptor: &libusb_device_descriptor,
    ) -> Result<UsbDeviceHandle, DriverError> {
        let mut port_numbers = [0; 7];
        check_libusb(unsafe {
            libusb_get_port_numbers(device, port_numbers.as_mut_ptr(), port_numbers.len() as i32)
        })?;
        let address = unsafe { libusb_get_device_address(device) };
        let mut handle = ptr::null_mut();
        match check_libusb(unsafe { libusb_open(device, &mut handle) }) {
            Ok(_) => (),
            // Windows returns this if a driver isn't yet installed
            Err(DriverError::UnsupportedUsbOperation) => (),
            Err(err) => return Err(err),
        }
        let [ver_l, ver_h] = descriptor.bcdDevice.to_le_bytes();
        Ok(UsbDeviceHandle {
            raw: handle,
            _port_numbers: port_numbers,
            address,
            version: (ver_h, ver_l),
            _usb: usb.clone(),
        })
    }
    fn identify_device(
        usb: &Rc<Usb>,
        device: *mut libusb_device,
    ) -> Result<Option<UsbDevice<Unclaimed>>, DriverError> {
        let mut descriptor = unsafe { mem::zeroed() };
        check_libusb(unsafe { libusb_get_device_descriptor(device, &mut descriptor) })?;
        if descriptor.idVendor != 0x16c0
            || (descriptor.idProduct != 0x05dc && descriptor.idProduct != 0x05e1)
            || descriptor.iManufacturer == 0
            || descriptor.iProduct == 0
        {
            Ok(None)
        } else {
            let handle = Self::open(usb, device, &descriptor)?;
            if handle.raw.is_null() {
                Ok(Some(UsbDevice::from_handle(
                    handle,
                    UsbDeviceKind::Unusable,
                )))
            } else {
                let vendor = handle.get_string_descriptor(descriptor.iManufacturer)?;
                let product = handle.get_string_descriptor(descriptor.iProduct)?;
                let kind = match (vendor.as_str(), product.as_str()) {
                    ("gekkio.fi", "GB-CARTPP-XC") => handle.identify()?,
                    _ => return Ok(None),
                };
                Ok(Some(UsbDevice::from_handle(handle, kind)))
            }
        }
    }
    fn detect_devices(
        usb: &Rc<Usb>,
        device_list: &[*mut libusb_device],
    ) -> Result<Vec<UsbDevice<Unclaimed>>, DriverError> {
        let mut devices = Vec::new();
        for &device in device_list {
            match Self::identify_device(usb, device) {
                Ok(Some(device)) => devices.push(device),
                Ok(None) => (),
                Err(DriverError::NoDevice)
                | Err(DriverError::UsbIo)
                | Err(DriverError::UsbPipe) => (),
                Err(err) => return Err(err),
            }
        }
        Ok(devices)
    }
}

impl UsbDeviceHandle {
    fn get_string_descriptor(&self, index: u8) -> Result<String, DriverError> {
        assert!(!self.raw.is_null());
        let mut buffer = [0u8; 64];
        let len = unsafe {
            libusb_get_string_descriptor(
                self.raw,
                index,
                0x0000,
                buffer.as_mut_ptr(),
                buffer.len() as i32,
            )
        };
        check_libusb(len)?;
        let code_points = buffer[..(len as usize)]
            .chunks_exact(2)
            .skip(1)
            .filter_map(|chunk| match *chunk {
                [a, b] => Some(u16::from_le_bytes([a, b])),
                _ => None,
            });
        Ok(char::decode_utf16(code_points)
            .collect::<Result<String, _>>()
            .unwrap_or_default())
    }
    fn ctrl_request(
        &self,
        req: VendorCtrlRequest,
        params: CtrlRequestParams,
    ) -> Result<usize, DriverError> {
        let (req_type, value, index, data) = match params {
            CtrlRequestParams::Out { value, index, data } => (
                LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                value,
                index,
                data.map(|data| (data.as_ptr() as *mut u8, data.len())),
            ),
            CtrlRequestParams::In { value, index, data } => (
                LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE,
                value,
                index,
                data.map(|data| (data.as_mut_ptr(), data.len())),
            ),
        };
        let (data_ptr, data_len) = data.unwrap_or((ptr::null_mut(), 0));
        assert!(data_len <= LIBUSB_MAX_PAYLOAD);
        let timeout = 1000 + data_len as u32 / 16;
        check_libusb(unsafe {
            libusb_control_transfer(
                self.raw,
                req_type,
                req as u8,
                value,
                index,
                data_ptr,
                data_len as u16,
                timeout,
            )
        })
    }
    fn identify(&self) -> Result<UsbDeviceKind, DriverError> {
        let mut buffer = [0x00; 5];
        self.ctrl_request(
            VendorCtrlRequest::Identify,
            CtrlRequestParams::In {
                value: 0,
                index: 0,
                data: Some(&mut buffer),
            },
        )?;
        let bl_version = FirmwareVersion {
            major: buffer[2],
            minor: buffer[1],
        };
        let fw_version = FirmwareVersion {
            major: buffer[4],
            minor: buffer[3],
        };
        match buffer[0] {
            0x42 => Ok(UsbDeviceKind::Bootloader {
                bl_version,
                fw_version,
            }),
            0x99 => Ok(UsbDeviceKind::Firmware {
                bl_version,
                fw_version,
            }),
            _ => Ok(UsbDeviceKind::Unusable),
        }
    }
}

#[derive(Debug)]
pub enum Unclaimed {}

pub trait UsbDeviceMode {}
impl UsbDeviceMode for Unclaimed {}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum UsbDeviceKind {
    Bootloader {
        bl_version: FirmwareVersion,
        fw_version: FirmwareVersion,
    },
    Firmware {
        fw_version: FirmwareVersion,
        bl_version: FirmwareVersion,
    },
    Unusable,
}

impl UsbDeviceKind {
    pub fn is_bootloader(&self) -> bool {
        matches!(self, UsbDeviceKind::Bootloader { .. })
    }
    pub fn is_firmware(&self) -> bool {
        matches!(self, UsbDeviceKind::Firmware { .. })
    }
}

#[derive(Debug)]
pub struct UsbDevice<T: UsbDeviceMode> {
    handle: UsbDeviceHandle,
    pub kind: UsbDeviceKind,
    _mode: PhantomData<T>,
}

#[repr(u8)]
#[derive(Copy, Clone, Debug, Eq, PartialEq)]
enum VendorCtrlRequest {
    Reset = 0x40,
    Identify = 0x41,
    Unlock = 0x42,
    Lock = 0x43,
    Read = 0x44,
    EraseFlash = 0x45,
    WriteFlash = 0x46,
    WriteCfg = 0x47,
    WriteId = 0x48,
}

#[derive(Debug)]
enum CtrlRequestParams<'a> {
    Out {
        value: u16,
        index: u16,
        data: Option<&'a [u8]>,
    },
    In {
        value: u16,
        index: u16,
        data: Option<&'a mut [u8]>,
    },
}

impl<T: UsbDeviceMode> UsbDevice<T> {
    pub fn version(&self) -> (u8, u8) {
        self.handle.version
    }
    pub fn usb_address(&self) -> u8 {
        self.handle.address
    }
    pub fn release(self) -> Result<UsbDevice<Unclaimed>, DriverError> {
        check_libusb(unsafe { libusb_release_interface(self.handle.raw, 0) })?;
        Ok(UsbDevice::<Unclaimed> {
            handle: self.handle,
            kind: self.kind,
            _mode: PhantomData,
        })
    }
    pub fn enter_bootloader(self) -> Result<(), DriverError> {
        self.handle.ctrl_request(
            VendorCtrlRequest::Reset,
            CtrlRequestParams::Out {
                value: 0x42,
                index: 0,
                data: None,
            },
        )?;
        Ok(())
    }
    pub fn reset(self) -> Result<(), DriverError> {
        self.handle.ctrl_request(
            VendorCtrlRequest::Reset,
            CtrlRequestParams::Out {
                value: 0x99,
                index: 0,
                data: None,
            },
        )?;
        Ok(())
    }
}

impl UsbDevice<Unclaimed> {
    fn from_handle(handle: UsbDeviceHandle, kind: UsbDeviceKind) -> UsbDevice<Unclaimed> {
        UsbDevice {
            handle,
            kind,
            _mode: PhantomData,
        }
    }
    fn claim_interface(&self) -> Result<(), DriverError> {
        if unsafe { libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER) } != 0 {
            check_libusb(unsafe {
                libusb_set_auto_detach_kernel_driver(self.handle.raw, true as i32)
            })?;
        }
        check_libusb(unsafe { libusb_claim_interface(self.handle.raw, 0) })?;
        Ok(())
    }
    pub fn claim_bootloader(self) -> Result<UsbDevice<BootloaderMode>, DriverError> {
        self.claim_interface()?;
        Ok(UsbDevice::<BootloaderMode> {
            handle: self.handle,
            kind: self.kind,
            _mode: PhantomData,
        })
    }
}

impl fmt::Display for UsbDevice<Unclaimed> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self.kind {
            UsbDeviceKind::Bootloader {
                bl_version,
                fw_version,
            } => write!(
                f,
                "USB device {:03}: GB-CARTPP-XC v{} (bootloader v{})",
                self.usb_address(),
                fw_version,
                bl_version,
            ),
            UsbDeviceKind::Firmware { fw_version, .. } => write!(
                f,
                "USB device {:03}: GB-CARTPP-XC v{}",
                self.usb_address(),
                fw_version,
            ),
            UsbDeviceKind::Unusable => write!(
                f,
                "USB device {:03}: GB-CARTPP-XC? (no driver installed)",
                self.usb_address()
            ),
        }
    }
}
