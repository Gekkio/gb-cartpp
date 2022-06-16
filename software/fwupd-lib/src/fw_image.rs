// SPDX-FileCopyrightText: 2019-2022 Joonas Javanainen <joonas.javanainen@gmail.com>
//
// SPDX-License-Identifier: MIT OR Apache-2.0

use flate2::read::GzDecoder;
use log::warn;
use pgp::{errors::Error as PgpError, Deserializable, SignedPublicKey, StandaloneSignature};
use rsa::errors::Error as RsaError;
use std::io::{self, Cursor, Read};
use thiserror::Error;

use crate::{FirmwareVersion, CONFIG_BLOCK_SIZE};

static SIGNING_KEYS: [&str; 1] = [include_str!(
    "../../../signing-keys/E2984F7B7562E0A759A75F36BCF068A71B6D5A67.asc"
)];

#[derive(Error, Debug)]
pub enum FirmwareError {
    #[error(transparent)]
    Io {
        #[from]
        source: io::Error,
    },
    #[error(transparent)]
    Pgp {
        #[from]
        source: PgpError,
    },
    #[error(transparent)]
    Rsa {
        #[from]
        source: RsaError,
    },
    #[error(transparent)]
    Ihex {
        #[from]
        source: ihex::ReaderError,
    },
}

#[derive(Debug)]
pub struct FirmwareArchive {
    hex_file: Vec<u8>,
    sig_file: Option<Vec<u8>>,
}

impl FirmwareArchive {
    pub fn from_reader<R: Read>(r: R) -> Result<Option<FirmwareArchive>, FirmwareError> {
        let mut hex_file = None;
        let mut sig_file = None;
        for entry in tar::Archive::new(GzDecoder::new(r)).entries()? {
            let mut entry = entry?;
            let path = entry.path()?;
            match path.to_str() {
                Some("GB-CARTPP-XC.hex") => {
                    let mut buf = Vec::new();
                    entry.read_to_end(&mut buf)?;
                    hex_file = Some(buf);
                }
                Some("GB-CARTPP-XC.hex.asc") => {
                    let mut buf = Vec::new();
                    entry.read_to_end(&mut buf)?;
                    sig_file = Some(buf);
                }
                _ => warn!(
                    "Skipping unknown firmware archive file {}",
                    path.to_string_lossy()
                ),
            }
        }
        Ok(hex_file.map(|hex_file| FirmwareArchive { hex_file, sig_file }))
    }
    pub fn has_signature(&self) -> bool {
        self.sig_file.is_some()
    }
    pub fn has_valid_signature(&self) -> Result<bool, FirmwareError> {
        if let Some(sig_file) = &self.sig_file {
            let sig_file = Cursor::new(sig_file);
            let (sig, _) = StandaloneSignature::from_armor_single(sig_file)?;
            for signing_key in &SIGNING_KEYS {
                let (signing_key, _) = SignedPublicKey::from_string(signing_key)?;
                match sig.verify(&signing_key, &self.hex_file) {
                    Ok(()) => return Ok(true),
                    Err(PgpError::RSAError(RsaError::Verification)) => (),
                    Err(err) => return Err(err.into()),
                }
            }
            Ok(false)
        } else {
            Ok(false)
        }
    }
    pub fn decode(self) -> Result<FirmwareImage, FirmwareError> {
        let mut image = FirmwareImage {
            flash: Box::new([0xff; 0x8000]),
            id: [0xff; 8],
            id_mask: [false; 8],
            config: [0xff; 14],
            config_mask: [false; 14],
        };
        let mut hex = String::new();
        (&self.hex_file[..]).read_to_string(&mut hex)?;
        let mut addr_high = 0;
        for record in ihex::Reader::new(&hex) {
            let record = record?;
            match record {
                ihex::Record::Data { offset, value } => {
                    let addr = addr_high | (offset as u32);
                    match addr {
                        0x00_0000..=0x00_7fff => {
                            assert!(addr + value.len() as u32 <= 0x00_8000);
                            let idx = (addr & 0x7fff) as usize;
                            let range = idx..(idx + value.len());
                            image.flash[range].copy_from_slice(&value);
                        }
                        0x20_0000..=0x20_0007 => {
                            assert!(addr + value.len() as u32 <= 0x20_0008);
                            let idx = (addr & 0b111) as usize;
                            let range = idx..(idx + value.len());
                            image.id[range.clone()].copy_from_slice(&value);
                            for m in &mut image.id_mask[range] {
                                *m = true;
                            }
                        }
                        0x30_0000..=0x30_000e => {
                            assert!(addr + value.len() as u32 <= 0x30_000e);
                            let idx = (addr & 0xf) as usize;
                            let range = idx..(idx + value.len());
                            image.config[range.clone()].copy_from_slice(&value);
                            for m in &mut image.config_mask[range] {
                                *m = true;
                            }
                        }
                        _ => panic!("Address out of bounds: {:08x}", addr),
                    }
                }
                ihex::Record::ExtendedLinearAddress(upper) => addr_high = (upper as u32) << 16,
                _ => (),
            }
        }
        image.config[4] = 0xff;
        image.config[7] = 0xff;
        image.config_mask[4] = false;
        image.config_mask[7] = false;
        Ok(image)
    }
}

#[derive(Clone)]
pub struct FirmwareImage {
    pub flash: Box<[u8; 0x8000]>,
    pub id: [u8; 8],
    pub id_mask: [bool; 8],
    pub config: [u8; CONFIG_BLOCK_SIZE],
    pub config_mask: [bool; CONFIG_BLOCK_SIZE],
}

const MAIN_FIRMWARE_START: usize = 0x800;

impl FirmwareImage {
    pub fn iter_flash_blocks(&self) -> impl Iterator<Item = (u32, &[u8])> {
        self.flash
            .chunks_exact(0x100)
            .enumerate()
            .filter_map(|(idx, block_data)| {
                let addr = (idx * 0x100) as u32;
                if addr < (MAIN_FIRMWARE_START as u32) {
                    None
                } else {
                    Some((addr, block_data))
                }
            })
    }
    pub fn iter_id_bytes<'a>(&'a self) -> impl Iterator<Item = (u32, u8)> + 'a {
        self.id
            .iter()
            .zip(self.id_mask.iter())
            .enumerate()
            .filter_map(|(idx, (&byte, &should_program))| {
                if should_program {
                    let addr = 0x20_0000 | (idx as u32);
                    Some((addr, byte))
                } else {
                    None
                }
            })
    }
    pub fn iter_config_bytes<'a>(&'a self) -> impl Iterator<Item = (u32, u8)> + 'a {
        self.config
            .iter()
            .zip(self.config_mask.iter())
            .enumerate()
            .filter_map(|(idx, (&byte, &should_program))| {
                if should_program {
                    let addr = 0x30_0000 | (idx as u32);
                    Some((addr, byte))
                } else {
                    None
                }
            })
    }
    pub fn checksum(&self) -> u16 {
        crc16::State::<crc16::XMODEM>::calculate(&self.flash[MAIN_FIRMWARE_START..])
    }
    pub fn version(&self) -> FirmwareVersion {
        FirmwareVersion {
            major: self.id[3],
            minor: self.id[2],
        }
    }
}
