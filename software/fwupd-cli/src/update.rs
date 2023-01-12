// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use eyre::{bail, eyre, Context};
use gb_cartpp_fwupd::FirmwareArchive;
use log::{debug, error, warn};
use std::{
    fs::File,
    io::{self, BufReader},
    path::PathBuf,
};

use crate::bootloader;

pub fn update_cmd(input: &PathBuf, allow_invalid_signature: bool) -> Result<(), eyre::Report> {
    let fw: Option<FirmwareArchive> = if input.as_os_str() == "-" {
        debug!("Reading firmware image from standard input");
        FirmwareArchive::from_reader(BufReader::new(io::stdin()))
            .wrap_err("Failed to read firmware image")
    } else {
        debug!("Reading firmware image from {}", input.display());
        File::open(input)
            .wrap_err("Failed to open firmware file")
            .and_then(|file| {
                FirmwareArchive::from_reader(BufReader::new(file))
                    .wrap_err("Failed to read firmware image")
            })
    }?;
    let fw = fw.ok_or_else(|| eyre!("No valid firmware image detected"))?;
    let signature_ok = if fw.has_signature() {
        debug!("Validating firmware image digital signature");
        fw.has_valid_signature().unwrap_or_else(|err| {
            if allow_invalid_signature {
                warn!("Failed to read signature: {}", err);
            } else {
                error!("Failed to read signature: {}", err);
            }
            false
        }) || allow_invalid_signature
    } else if allow_invalid_signature {
        warn!("The firmware image has no digital signature!");
        true
    } else {
        error!("The firmware image has no digital signature!");
        false
    };
    if !signature_ok {
        error!("The firmware image is unofficial, corrupted, or has been tampered with, so flashing is prohibited");
        error!("If you are absolutely sure what you are doing, you can use --allow-invalid-signature to allow flashing anyway. *THIS IS NOT SAFE AND MAY BRICK THE DEVICE*");
        bail!("Aborting due to invalid digital signature");
    }
    bootloader::update_firmware(fw).wrap_err("Failed to update firmware")?;

    Ok(())
}
