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
use clap::{App, Arg, ArgMatches};
use gb_cartpp_fwupd::FirmwareArchive;
use log::{debug, error, warn};
use simplelog::{LevelFilter, TermLogger, TerminalMode};
use std::borrow::Cow;
use std::fs::File;
use std::io::{self, BufReader};
use std::process;

mod bootloader;
mod device;

fn build_app() -> App<'static, 'static> {
    App::new("gb-cartpp-fwupd")
        .version(env!("CARGO_PKG_VERSION"))
        .author(env!("CARGO_PKG_AUTHORS"))
        .arg(
            Arg::with_name("v")
                .short("v")
                .multiple(true)
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
                .index(1),
        )
}

fn run(matches: &ArgMatches) -> Result<(), Error> {
    let level_filter = match matches.occurrences_of("v") {
        0 => LevelFilter::Info,
        1 => LevelFilter::Debug,
        _ => LevelFilter::Trace,
    };

    let _ = TermLogger::init(
        level_filter,
        simplelog::Config::default(),
        TerminalMode::Mixed,
    );

    let input = matches.value_of_os("IMAGE").unwrap();
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
