// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use clap::{builder::PathBufValueParser, Arg, ArgAction, Command};
use eyre::{eyre, Report};

use log::error;
use simplelog::{ColorChoice, LevelFilter, TermLogger, TerminalMode};
use std::{path::PathBuf, process};

mod bootloader;
mod update;

fn build_cmd() -> Command {
    Command::new("gbcartpp-fwupd")
        .version(env!("CARGO_PKG_VERSION"))
        .author(env!("CARGO_PKG_AUTHORS"))
        .arg(
            Arg::new("v")
                .short('v')
                .action(ArgAction::Count)
                .help("Sets the level of verbosity")
                .global(true),
        )
        .subcommand(
            Command::new("update-firmware")
                .about("Update the firmware of a GB-CARTPP device")
                .arg(
                    Arg::new("input")
                        .help("Firmware image file")
                        .value_name("FILE")
                        .value_parser(PathBufValueParser::new()),
                )
                .arg(
                    Arg::new("allow-invalid-signature")
                        .long("allow-invalid-signature")
                        .action(ArgAction::SetTrue)
                        .help("Allow flashing firmware without a valid signature"),
                ),
        )
}

fn main() -> Result<(), Report> {
    simple_eyre::install()?;

    let matches = build_cmd().get_matches();

    let (level_filter, config) = match matches.get_count("v") {
        0 => (
            LevelFilter::Info,
            simplelog::ConfigBuilder::new()
                .set_time_level(LevelFilter::Debug)
                .set_max_level(LevelFilter::Debug)
                .build(),
        ),
        1 => (LevelFilter::Debug, simplelog::Config::default()),
        _ => (LevelFilter::Trace, simplelog::Config::default()),
    };

    let _ = TermLogger::init(level_filter, config, TerminalMode::Mixed, ColorChoice::Auto);

    let result = {
        if let Some(matches) = matches.subcommand_matches("update-firmware") {
            let input = matches
                .get_one::<PathBuf>("input")
                .ok_or_else(|| eyre!("No input file specified"))?;
            let allow_invalid_signature = matches.get_flag("allow-invalid-signature");
            update::update_cmd(input, allow_invalid_signature)
        } else {
            Ok(build_cmd().print_help()?)
        }
    };
    match result {
        Ok(()) => Ok(()),
        Err(err) => {
            error!("{:#}", err);
            process::exit(1);
        }
    }
}
