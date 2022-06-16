// SPDX-FileCopyrightText: 2019-2021 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use anyhow::Error;
use clap::{builder::OsStringValueParser, App, Arg, ArgAction, ArgMatches};
use gb_cartpp_fwupd::FirmwareArchive;
use log::{debug, error, warn};
use simplelog::{ColorChoice, LevelFilter, TermLogger, TerminalMode};
use std::{
    borrow::Cow,
    ffi::OsString,
    fs::File,
    io::{self, BufReader},
    process,
};

mod bootloader;

fn build_app() -> App<'static> {
    App::new("gb-cartpp-fwupd")
        .version(env!("CARGO_PKG_VERSION"))
        .author(env!("CARGO_PKG_AUTHORS"))
        .arg(
            Arg::with_name("v")
                .short('v')
                .action(ArgAction::Count)
                .help("Sets the level of verbosity"),
        )
        .arg(
            Arg::with_name("allow-invalid-signature")
                .long("allow-invalid-signature")
                .help("Allow flashing firmware without a valid signature"),
        )
        .arg(
            Arg::with_name("IMAGE")
                .help("Sets the firmware image file to use")
                .required(true)
                .value_parser(OsStringValueParser::new())
                .index(1),
        )
}

fn run(matches: &ArgMatches) -> Result<(), Error> {
    let level_filter = match matches.get_one::<u8>("v").copied().unwrap_or_default() {
        0 => LevelFilter::Info,
        1 => LevelFilter::Debug,
        _ => LevelFilter::Trace,
    };

    let _ = TermLogger::init(
        level_filter,
        simplelog::Config::default(),
        TerminalMode::Mixed,
        ColorChoice::Auto,
    );

    let input = matches.get_one::<OsString>("IMAGE").unwrap();
    let source = if input == "-" {
        Cow::Borrowed("standard input")
    } else {
        input.to_string_lossy()
    };
    debug!("Reading firmware image from {}", source);
    let fw = (if input == "-" {
        FirmwareArchive::from_reader(BufReader::new(io::stdin()))
    } else {
        let file = File::open(input)?;
        FirmwareArchive::from_reader(BufReader::new(file))
    })
    .unwrap_or_else(|err| {
        error!("Failed to read firmware image from {}: {}", source, err);
        process::exit(1);
    })
    .unwrap_or_else(|| {
        error!("No valid firmware image detected in {}", source);
        process::exit(1);
    });
    let allow_invalid_signature = matches.is_present("allow-invalid-signature");
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
        process::exit(1);
    }
    bootloader::update_firmware(fw)?;

    Ok(())
}

fn main() -> Result<(), Error> {
    let matches = build_app().get_matches();
    run(&matches)
}
