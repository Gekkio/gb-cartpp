// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use eyre::Report;
use gb_cartpp_fwupd::{
    BootloaderDriver, FirmwareArchive, FirmwareVersion, Unclaimed, Usb, UsbDevice, UsbDeviceKind,
    VerifyResult,
};
use indicatif::{ProgressBar, ProgressStyle};
use itertools::Itertools;
use log::{debug, error, info, log_enabled};
use std::{
    process,
    rc::Rc,
    thread,
    time::{Duration, Instant},
};

fn poll_after_reset<F: Fn(&UsbDevice<Unclaimed>) -> bool>(
    usb: &Rc<Usb>,
    f: F,
) -> Result<UsbDevice<Unclaimed>, Report> {
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

pub fn update_firmware(fw: FirmwareArchive) -> Result<(), Report> {
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
        for device in &devices {
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
        for device in &devices {
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
    info!("Using {}", device);
    if !device.kind.is_bootloader() {
        debug!("Resetting {}", device);
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
    info!(
        "Firmware image: v{} (checksum 0x{:04x})",
        image_version, image_checksum
    );
    info!(
        "Device:         v{} (checksum 0x{:04x})",
        drv.firmware_version(),
        fw_checksum
    );

    if drv.firmware_version() == image_version && fw_checksum == image_checksum {
        info!("No update is necessary");
        drv.reset()?;
        poll_after_reset(&usb, |d| d.usb_address() != address && d.kind.is_firmware())?;
        return Ok(());
    }

    let progress = ProgressBar::new(0x8000 - 0x800)
        .with_style(ProgressStyle::default_bar().template("{msg} {bar} {percent} %")?);
    progress.enable_steady_tick(Duration::from_millis(16));
    progress.set_message("Updating flash: ");
    drv.write_flash(&fw, |addr| {
        progress.set_position((addr - 0x800) as u64);
    })?;
    progress.finish();

    info!("Updating ID bytes");
    drv.write_id(&fw)?;

    let style = ProgressStyle::default_bar().template("{msg} {bar} {percent} %")?;
    let error_style =
        ProgressStyle::default_bar().template("{msg} {bar:.red} {percent} % {prefix:.red}")?;
    let progress = ProgressBar::new(0x8000 - 0x800).with_style(style.clone());
    progress.enable_steady_tick(Duration::from_millis(16));
    progress.set_message("Verifying flash:");
    let mut errored = false;
    let result = drv.verify_flash(&fw, |addr, result| {
        if let VerifyResult::Invalid { .. } = result {
            if !errored {
                progress.set_style(error_style.clone());
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

    info!("Verifying ID bytes");
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

    info!("Verifying config bytes");
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

    info!("Resetting device");
    drv.reset()?;
    device = poll_after_reset(&usb, |d| d.usb_address() != address && d.kind.is_firmware())?;
    info!(
        "Firmware updated to v{}.{}",
        device.version().0,
        device.version().1
    );

    Ok(())
}
