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
use anyhow::Error;
use gb_cartpp_fwupd::{
    BootloaderDriver, FirmwareArchive, FirmwareVersion, Unclaimed, Usb, UsbDevice, UsbDeviceKind,
    VerifyResult,
};
use indicatif::{ProgressBar, ProgressStyle};
use itertools::Itertools;
use log::{debug, error, log_enabled};
use std::process;
use std::rc::Rc;
use std::thread;
use std::time::{Duration, Instant};

use crate::device::format_device;

fn poll_after_reset<F: Fn(&UsbDevice<Unclaimed>) -> bool>(
    usb: &Rc<Usb>,
    f: F,
) -> Result<UsbDevice<Unclaimed>, Error> {
    let start_time = Instant::now();
    loop {
        thread::sleep(Duration::from_millis(200));
        if let Ok(device) = Usb::list_devices(&usb)?
            .into_iter()
            .filter(&f)
            .exactly_one()
        {
            break Ok(device);
        }
        if start_time.elapsed() > Duration::from_secs(10) {
            error!("Failed to detect device after reset");
            process::exit(1);
        }
    }
}

pub fn update_firmware(fw: FirmwareArchive) -> Result<(), Error> {
    let fw = fw.decode()?;

    let usb = Usb::init()?;
    let devices = Usb::list_devices(&usb)?;
    debug!("Detected {} candidate devices", devices.len());
    let ready_devices = devices
        .iter()
        .map(|dev| match dev.kind {
            UsbDeviceKind::Bootloader { .. } | UsbDeviceKind::Firmware { .. } => 1,
            _ => 0,
        })
        .sum::<u32>();
    if ready_devices == 0 {
        error!("No GB-CARTPP-XC devices detected");
        for device in devices.iter().map(format_device) {
            error!("Detected but unusable {}", device);
        }
        process::exit(1);
    } else if ready_devices > 1 {
        error!(
            "{} GB-CARTPP-XC devices detected, but only one can be connected during firmware update",
            ready_devices
        );
        process::exit(1);
    }
    if log_enabled!(log::Level::Debug) {
        for device in devices.iter().map(format_device) {
            debug!("{}", device);
        }
    }
    let mut device = devices
        .into_iter()
        .filter(|dev| match dev.kind {
            UsbDeviceKind::Bootloader { .. } | UsbDeviceKind::Firmware { .. } => true,
            _ => false,
        })
        .exactly_one()
        .unwrap();
    println!("Using {}", format_device(&device));
    if !device.kind.is_bootloader() {
        debug!("Resetting {}", format_device(&device));
        let address_before_reset = device.usb_address();
        let _ = device.enter_bootloader();
        device = poll_after_reset(&usb, |d| {
            d.usb_address() != address_before_reset && d.kind.is_bootloader()
        })?;
    }
    let address = device.usb_address();
    let drv = BootloaderDriver::initialize(device)?;
    let fw_checksum = drv.calc_flash_checksum()?;

    let image_checksum = crc16::State::<crc16::XMODEM>::calculate(&fw.flash[0x800..]);
    let image_version = FirmwareVersion {
        major: fw.id[3],
        minor: fw.id[2],
    };
    println!(
        "Firmware image: v{} (checksum {:#04x})",
        image_version, image_checksum
    );
    println!(
        "Device:         v{} (checksum {:#04x})",
        drv.firmware_version(),
        fw_checksum
    );

    if drv.firmware_version() == image_version && fw_checksum == image_checksum {
        println!("No update is necessary");
        drv.reset()?;
        poll_after_reset(&usb, |d| d.usb_address() != address && d.kind.is_firmware())?;
        return Ok(());
    }

    let progress = ProgressBar::new(0x8000 - 0x800)
        .with_style(ProgressStyle::default_bar().template("{msg} {bar} {percent} %"));
    progress.enable_steady_tick(16);
    progress.set_message("Updating flash: ");
    drv.write_flash(&fw, |addr| {
        progress.set_position((addr - 0x800) as u64);
    })?;
    progress.finish();

    println!("Updating ID bytes");
    drv.write_id(&fw)?;

    let style = ProgressStyle::default_bar().template("{msg} {bar} {percent} %");
    let progress = ProgressBar::new(0x8000 - 0x800).with_style(style.clone());
    progress.enable_steady_tick(16);
    progress.set_message("Verifying flash:");
    let mut errored = false;
    let result = drv.verify_flash(&fw, |addr, result| {
        if let VerifyResult::Invalid { .. } = result {
            if !errored {
                progress.set_style(
                    style
                        .clone()
                        .template("{msg} {bar:.red} {percent} % {prefix:.red}"),
                );
                progress.set_prefix("errors detected");
                errored = true;
            }
        }
        progress.set_position((addr - 0x800) as u64);
    })?;
    progress.finish();
    if let VerifyResult::Invalid {
        errors,
        first_error_addr,
    } = result
    {
        error!(
            "Updating flash failed: {} errors, starting at {:#06x}",
            errors, first_error_addr
        );
        process::exit(1);
    }

    println!("Verifying ID bytes");
    let result = drv.verify_id(&fw)?;
    if let VerifyResult::Invalid {
        errors,
        first_error_addr,
    } = result
    {
        error!(
            "Updating ID bytes failed: {} errors, starting at {:#06x}",
            errors, first_error_addr
        );
        process::exit(1);
    }

    println!("Verifying config bytes");
    let result = drv.verify_cfg(&fw)?;
    if let VerifyResult::Invalid {
        errors,
        first_error_addr,
    } = result
    {
        error!(
            "Invalid config bytes: {} errors, starting at {:#06x}",
            errors, first_error_addr
        );
        process::exit(1);
    }

    println!("Resetting device");
    drv.reset()?;
    device = poll_after_reset(&usb, |d| d.usb_address() != address && d.kind.is_firmware())?;
    println!(
        "Firmware updated to v{}.{}",
        device.version().0,
        device.version().1
    );

    Ok(())
}
