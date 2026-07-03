use crate::builder::nand::types::*;
use crate::crypto::{hmac_sha, Rc4};
use log::info;
use log::warn;
use zerocopy::FromBytes;

use crate::builder::chain::*;
use crate::builder::chain::{
    cb::BootloaderCb, cd::BootloaderCd, ce::BootloaderCe, cf::BootloaderCf, cg::BootloaderCg,
    sc::BootloaderSc,
};
use crate::builder::filesystem::corona::{self};
use crate::builder::filesystem::flashfs::FlashFS;
use crate::core::images::blocks::*;

impl NandSkeleton {
    pub fn parse_clean_encrypted(
        image: Vec<u8>,
        layout: NandLayout,
        flashfs: FlashFS,
    ) -> Result<Self> {
        let header_sz = std::mem::size_of::<NandHeader>();
        if image.len() < header_sz {
            return Err(BuilderError::ImageTooSmall {
                got: image.len(),
                need: header_sz,
            });
        }
        let header = NandHeader::read_from_prefix(&image[..header_sz])
            .map_err(|e| BuilderError::HeaderParse(e.to_string()))?
            .0;
        header.validate()?;
        //header.print_info();

        let kv_addr = header.kv_addr.get() as usize;
        let kv_size = header.kv_size.get() as usize;
        if kv_size != 0x4000 {
            return Err(BuilderError::InvalidKvSize { size: kv_size });
        }
        let kv_end = kv_addr
            .checked_add(kv_size)
            .ok_or_else(|| BuilderError::OffsetOverflow("KV".to_string()))?;
        if kv_end > image.len() {
            return Err(BuilderError::KvOutOfBounds {
                offset: kv_addr,
                size: kv_size,
                image_len: image.len(),
            });
        }
        info!(
            "[builder] Extracting Keyvault (encrypted) (Addr: 0x{:X}, Size: 0x{:X})...",
            kv_addr, kv_size
        );
        let kv = crate::builder::chain::kv::Keyvault::parse(&image[kv_addr..kv_addr + kv_size])
            .map_err(|e| BuilderError::Build(e.to_string()))?;

        let smc_offset = header.smc_boot_offset.get() as usize;
        let smc_size = header.smc_boot_size.get() as usize;
        let smc_end = smc_offset
            .checked_add(smc_size)
            .ok_or_else(|| BuilderError::OffsetOverflow("SMC".to_string()))?;
        if smc_end > image.len() {
            return Err(BuilderError::SmcOutOfBounds {
                offset: smc_offset,
                size: smc_size,
                image_len: image.len(),
            });
        }
        info!(
            "[builder] Extracting SMC (encrypted) (Addr: 0x{:X}, Size: 0x{:X})...",
            smc_offset, smc_size
        );
        let smc_data = image[smc_offset..smc_offset + smc_size].to_vec();
        let mut smc_probe = crate::builder::chain::smc::RawSmc::new(smc_data.clone());
        smc_probe.ensure_decrypted();
        let smc_metadata = smc_probe.metadata.clone();

        let config_offset = header.smc_config_offset.get() as usize;
        let config_size = 0x10000;
        let config_data = if config_offset > 0 && config_offset + config_size <= image.len() {
            info!(
                "[builder] Extracting SMC Config (Addr: 0x{:X}, Size: 0x{:X})...",
                config_offset, config_size
            );
            image[config_offset..config_offset + config_size].to_vec()
        } else {
            let fallback_offset =
                crate::builder::chain::smc_config::SmcConfig::get_scan_address(&layout) as usize;
            if fallback_offset > 0 && fallback_offset + config_size <= image.len() {
                let data = image[fallback_offset..fallback_offset + config_size].to_vec();
                if data.iter().any(|&b| b != 0xFF) {
                    info!(
                    "[builder] Extracting SMC Config (fallback) (Addr: 0x{:X}, Size: 0x{:X})...",
                    fallback_offset, config_size
                );
                    data
                } else {
                    Vec::new()
                }
            } else {
                Vec::new()
            }
        };

        let mut extra = NandExtra {
            smc: smc_data,
            smc_metadata: smc_metadata.clone(),
            smc_config: config_data,
            keyvault: kv.data.clone(),
            fcrt: None,
            khvpatch: None,
            lba_map: LbaMap::new(0x400),
        };

        info!(
            "[builder] Walking bootloader chain (encrypted) starting at offset 0x{:X}...",
            header.cb_offset()
        );
        let (bootloaders, update) = Self::parse_bootloader_chain(
            &image,
            header.cb_offset() as usize,
            header.cf_offset.get() as usize,
            layout,
            &flashfs,
        )?;

        let motherboard = smc_metadata
            .as_ref()
            .map(|meta| MotherboardType::from_smc(meta.type_byte))
            .unwrap_or(MotherboardType::Unknown);

        let mut final_flashfs = flashfs;
        if matches!(layout, NandLayout::Sb | NandLayout::Xsb) && final_flashfs.root.block_number < 0
        {
            let sb = SouthbridgeType::from(motherboard);
            let chain_profile = if bootloaders.cb_b.is_some() {
                "split"
            } else {
                "single"
            };
            let (_fs_root_addr, _smc_cfg, phys_fs_block) =
                layout_calculator(sb, chain_profile, layout);
            if phys_fs_block > 0 {
                final_flashfs.root.block_number = phys_fs_block as i32;
                final_flashfs.root.read(&image, &layout);
            }
        }

        let fcrt_from_nand = final_flashfs
            .root
            .entries
            .iter()
            .find(|e| !e.deleted && e.file_name.eq_ignore_ascii_case("fcrt.bin"))
            .map(|e| e.data.clone());
        if extra.fcrt.is_none() {
            extra.fcrt = fcrt_from_nand;
        }

        let total_blocks = layout.total_blocks(image.len());
        extra.lba_map = LbaMap::from_layout(layout, total_blocks);
        let corona_fs = if layout == NandLayout::Emmc {
            corona::load_slots(&image)
        } else {
            [Default::default(), Default::default()]
        };

        Ok(NandSkeleton {
            cpukey: None,
            build_options: BuildOptions::default(),
            options: NandConfig {
                layout,
                image_profile: match smc_metadata.as_ref().map(|meta| meta.smc_type) {
                    Some(crate::builder::chain::smc::SmcType::Retail) => "retail".to_string(),
                    Some(crate::builder::chain::smc::SmcType::Glitch) => "glitch".to_string(),
                    Some(crate::builder::chain::smc::SmcType::Jtag)
                    | Some(crate::builder::chain::smc::SmcType::RJtag) => "jtag".to_string(),
                    Some(crate::builder::chain::smc::SmcType::Cygnos) => "cygnos".to_string(),
                    _ => (if bootloaders.cb_b.is_some() {
                        "split"
                    } else {
                        "single"
                    })
                    .to_string(),
                },
                build_mode: BuildMode::Normal,
                motherboard,
                total_blocks,
                khv_header_size: 0x4000,
            },
            header,
            image,
            extra,
            kv: Some(kv),
            bootloaders,
            update: Some(update),
            payloads: None,
            flashfs: Some(final_flashfs),
            mobile: None,
            corona_fs: Some(corona_fs),
        })
    }

    pub fn parse_encrypted_chain(&self) -> Result<(NandBootloaders, NandUpdate)> {
        let flashfs = self
            .flashfs
            .as_ref()
            .ok_or_else(|| BuilderError::Build("Missing FlashFS".to_string()))?;
        Self::parse_bootloader_chain(
            &self.image,
            self.header.cb_offset() as usize,
            self.header.cf_offset.get() as usize,
            self.options.layout,
            flashfs,
        )
    }

    pub fn parse_clean(
        image: Vec<u8>,
        layout: NandLayout,
        cpukey: [u8; 16],
        flashfs: FlashFS,
    ) -> Result<Self> {
        let header_sz = std::mem::size_of::<NandHeader>();
        if image.len() < header_sz {
            return Err(BuilderError::ImageTooSmall {
                got: image.len(),
                need: header_sz,
            });
        }
        let header = NandHeader::read_from_prefix(&image[..header_sz])
            .map_err(|e| BuilderError::HeaderParse(e.to_string()))?
            .0;
        header.validate()?;
        // header.print_info();

        let kv_addr = header.kv_addr.get() as usize;
        let kv_size = header.kv_size.get() as usize;
        if kv_size != 0x4000 {
            return Err(BuilderError::InvalidKvSize { size: kv_size });
        }
        let kv_end = kv_addr
            .checked_add(kv_size)
            .ok_or_else(|| BuilderError::OffsetOverflow("KV".to_string()))?;
        if kv_end > image.len() {
            return Err(BuilderError::KvOutOfBounds {
                offset: kv_addr,
                size: kv_size,
                image_len: image.len(),
            });
        }
        info!(
            "[builder] Extracting and decrypting Keyvault (Addr: 0x{:X}, Size: 0x{:X})...",
            kv_addr, kv_size
        );
        let mut kv = crate::builder::chain::kv::Keyvault::parse(&image[kv_addr..kv_addr + kv_size])
            .map_err(|e| BuilderError::Build(e.to_string()))?;
        kv.decrypt(&cpukey)
            .map_err(|e| BuilderError::Build(e.to_string()))?;

        let smc_offset = header.smc_boot_offset.get() as usize;
        let smc_size = header.smc_boot_size.get() as usize;
        let smc_end = smc_offset
            .checked_add(smc_size)
            .ok_or_else(|| BuilderError::OffsetOverflow("SMC".to_string()))?;
        if smc_end > image.len() {
            return Err(BuilderError::SmcOutOfBounds {
                offset: smc_offset,
                size: smc_size,
                image_len: image.len(),
            });
        }
        info!(
            "[builder] Extracting and decrypting SMC (Addr: 0x{:X}, Size: 0x{:X})...",
            smc_offset, smc_size
        );
        let mut smc = crate::builder::chain::smc::RawSmc::new(
            image[smc_offset..smc_offset + smc_size].to_vec(),
        );
        smc.decrypt();

        let config_offset = header.smc_config_offset.get() as usize;
        let config_size = 0x10000;

        let config_data = if config_offset > 0 && config_offset + config_size <= image.len() {
            info!(
                "[builder] Extracting SMC Config (Addr: 0x{:X}, Size: 0x{:X})...",
                config_offset, config_size
            );
            image[config_offset..config_offset + config_size].to_vec()
        } else {
            if config_offset > 0 {
                warn!(
                    "[builder] SMC Config offset 0x{:X} is out of bounds, trying fallback scan",
                    config_offset
                );
            }
            let fallback_offset =
                crate::builder::chain::smc_config::SmcConfig::get_scan_address(&layout) as usize;
            if fallback_offset > 0 && fallback_offset + config_size <= image.len() {
                let data = image[fallback_offset..fallback_offset + config_size].to_vec();
                if data.iter().any(|&b| b != 0xFF) {
                    info!(
                    "[builder] Extracting SMC Config (fallback) (Addr: 0x{:X}, Size: 0x{:X})...",
                    fallback_offset, config_size
                );
                    data
                } else {
                    Vec::new()
                }
            } else {
                Vec::new()
            }
        };

        let mut extra = NandExtra {
            smc: smc.data,
            smc_metadata: smc.metadata.clone(),
            smc_config: config_data,
            keyvault: kv.data.clone(),
            fcrt: None,
            khvpatch: None,
            lba_map: LbaMap::new(0x400),
            // power_on_cause_a: 0,
            // power_on_cause_b: 0,
        };

        info!(
            "[builder] Walking bootloader chain starting at offset 0x{:X}...",
            header.cb_offset()
        );
        let (bl, mut update) = Self::parse_bootloader_chain(
            &image,
            header.cb_offset() as usize,
            header.cf_offset.get() as usize,
            layout,
            &flashfs,
        )?;

        info!("[builder] Decrypting bootloader chain...");
        let mut bl_mut = bl;
        if bl_mut.cb_a.is_none() {
            return Err(BuilderError::Build("Missing CB_A bootloader".to_string()));
        }
        if bl_mut.cd.is_none() {
            return Err(BuilderError::Build("Missing CD bootloader".to_string()));
        }
        if bl_mut.ce.is_none() {
            return Err(BuilderError::Build("Missing CE bootloader".to_string()));
        }

        decrypt_chain(
            bl_mut.cb_a.as_mut().unwrap(),
            bl_mut.cb_x.as_mut(),
            bl_mut.cb_b.as_mut(),
            bl_mut.sc.as_mut(),
            bl_mut.cd.as_mut().unwrap(),
            bl_mut.ce.as_mut().unwrap(),
            update.cf_0.as_mut(),
            update.cg_0.as_mut(),
            update.cf_1.as_mut(),
            update.cg_1.as_mut(),
            &cpukey,
        )?;
        info!("[builder] Bootloader chain successfully decrypted.");

        let motherboard = if extra.smc.len() > 0x100 {
            MotherboardType::from_smc(extra.smc[0x100])
        } else {
            MotherboardType::Unknown
        };

        let mut final_flashfs = flashfs;
        if matches!(layout, NandLayout::Sb | NandLayout::Xsb) && final_flashfs.root.block_number < 0
        {
            let sb = SouthbridgeType::from(motherboard);
            let chain_profile = if bl_mut.cb_b.is_some() {
                "split"
            } else {
                "single"
            };
            let (_fs_root_addr, _smc_cfg, phys_fs_block) =
                layout_calculator(sb, chain_profile, layout);
            if phys_fs_block > 0 {
                final_flashfs.root.block_number = phys_fs_block as i32;
                final_flashfs.root.read(&image, &layout);
            }
        }

        let fcrt_from_nand = final_flashfs
            .root
            .entries
            .iter()
            .find(|e| !e.deleted && e.file_name.eq_ignore_ascii_case("fcrt.bin"))
            .map(|e| e.data.clone());
        if extra.fcrt.is_none() {
            extra.fcrt = fcrt_from_nand;
        }

        let total_blocks = layout.total_blocks(image.len());
        extra.lba_map = LbaMap::from_layout(layout, total_blocks);

        let corona_fs = if layout == NandLayout::Emmc {
            corona::load_slots(&image)
        } else {
            [Default::default(), Default::default()]
        };

        Ok(NandSkeleton {
            cpukey: Some(cpukey),
            build_options: BuildOptions::default(),
            options: NandConfig {
                layout,
                image_profile: (if bl_mut.cb_b.is_some() {
                    "split"
                } else {
                    "single"
                })
                .to_string(),
                build_mode: BuildMode::Normal,
                motherboard,
                total_blocks,
                khv_header_size: 0x4000,
            },
            header,
            image,
            extra,
            kv: Some(kv),
            bootloaders: bl_mut,
            update: Some(update),
            payloads: None,
            flashfs: Some(final_flashfs),
            mobile: None,
            corona_fs: Some(corona_fs),
        })
    }

    fn parse_bootloader_chain(
        image: &[u8],
        cb_offset: usize,
        cf_ptr: usize,
        layout: NandLayout,
        flashfs: &crate::builder::filesystem::flashfs::FlashFS,
    ) -> Result<(NandBootloaders, NandUpdate)> {
        let mut bl = NandBootloaders {
            cb: None,
            cb_a: None,
            cb_x: None,
            cb_b: None,
            sc: None,
            cd: None,
            ce: None,
            // khvpatch: None,
            xell: None,
        };
        let mut update = NandUpdate {
            cf_0: None,
            cg_0: None,
            cf_1: None,
            cg_1: None,
        };

        let mut off = cb_offset;
        let mut cf_count = 0;
        let mut cg_count = 0;
        let mut cb_seen = 0;
        let mut cf0_offset = 0;
        let mut cf1_offset = 0;

        let fetch_cg_data = |off: usize,
                             bl_size: usize,
                             cg_count: usize,
                             cf0_offset: usize,
                             cf1_offset: usize,
                             cf_meta: Option<&crate::builder::chain::cf::CfMetadata>,
                             image: &[u8],
                             layout: &NandLayout,
                             flashfs: &FlashFS|
         -> Vec<u8> {
            let target_size = (bl_size + 0xF) & !0xF;
            let slot_start = if cg_count == 0 {
                cf0_offset
            } else {
                cf1_offset
            };
            let mut read_size = target_size;
            if slot_start > 0 {
                let max_slot_end = slot_start + 0x10000;
                if off + read_size > max_slot_end {
                    read_size = max_slot_end - off;
                }
            }
            if off + read_size > image.len() {
                return vec![];
            }
            let mut data = image[off..off + read_size].to_vec();
            if data.len() < target_size {
                let needed = target_size - data.len();
                let mut appended = false;

                if let Some(meta) = cf_meta {
                    if meta.cg_blocks_used > 0 && !meta.cg_block_numbers.is_empty() {
                        let start_block = meta.cg_block_numbers[0];
                        let chain_data = flashfs.root.get_chain_data(image, layout, start_block);
                        if !chain_data.is_empty() {
                            let take = std::cmp::min(needed, chain_data.len());
                            data.extend_from_slice(&chain_data[..take]);
                            appended = true;
                        }
                    }
                }

                if !appended {
                    let sysupdate_name = if cg_count == 0 {
                        "sysupdate.xexp1"
                    } else {
                        "sysupdate.xexp2"
                    };
                    if let Some(entry) = flashfs
                        .root
                        .entries
                        .iter()
                        .find(|e| e.file_name.to_lowercase() == sysupdate_name)
                    {
                        data.extend_from_slice(&entry.data[..std::cmp::min(needed, entry.data.len())]);
                    } else {
                        warn!(
                            "[builder] CG{} overflows patch slot but neither CF block chain nor {} was available in FlashFS!",
                            cg_count + 1,
                            sysupdate_name
                        );
                        if off + target_size <= image.len() {
                            data = image[off..off + target_size].to_vec();
                        }
                    }
                }
            }
            data
        };

        // Primary chain walk
        let mut iteration = 0;
        while iteration < 16 {
            iteration += 1;

            if off + 0x10 > image.len() {
                info!("[builder] End of bootloader chain at offset 0x{:08X}", off);
                break;
            }

            let bl_header = match BootloaderHeader::read_from_prefix(&image[off..off + 0x10]) {
                Ok((h, _)) => h,
                Err(_) => {
                    info!(
                        "[builder] Invalid bootloader header at offset 0x{:08X}",
                        off
                    );
                    break;
                }
            };

            let bl_size = bl_header.size.get() as usize;
            let bl_version = bl_header.version.get();

            // Validate size bounds - if invalid, stop the chain walk gracefully
            // and let CF_Ptr bridging handle the gap (common between CE and CF)
            if bl_size < 0x10 || bl_size > 0x2000000 {
                info!(
                    "[builder] Invalid bootloader size at 0x{:08X} (0x{:X}), stopping chain walk",
                    off, bl_size
                );
                break;
            }
            if off + bl_size > image.len() {
                info!(
                "[builder] Bootloader at 0x{:08X} extends past image end (size 0x{:X}), stopping",
                off, bl_size
            );
                break;
            }

            let bl_data = image[off..off + bl_size].to_vec();
            let aligned_size = (bl_size + 0xF) & 0xFFFFFFF0;

            match bl_header.get_type() {
                XenonBlType::CB => {
                    cb_seen += 1;
                    let flags = bl_header.flags.get();
                    let has_cba_flag = (flags & 0x800) == 0x800;
                    let is_single = cb_seen == 1 && !has_cba_flag;
                    let is_cba = cb_seen == 1 && has_cba_flag;
                    let is_cbx = cb_seen == 2
                        && has_cba_flag                   // stub still carries 0x800
                        && bl_size <= 0x500               // stub is very small (0x400 on Corona)
                        && bl_header.pairing.get() == 0; // stub pairing word is 0x0000
                                                         // CB_B = anything that doesn't match the above (cb_seen >= 2, or cb_seen == 3)

                    if is_single {
                        info!(
                            "[builder] CB (single) at 0x{:08X} (v{}, 0x{:X} bytes)",
                            off, bl_version, bl_size
                        );
                        bl.cb = Some(
                            BootloaderCb::parse(&bl_data)
                                .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                        );
                    } else if is_cba {
                        info!(
                            "[builder] CB_A at 0x{:08X} (v{}, 0x{:X} bytes)",
                            off, bl_version, bl_size
                        );
                        bl.cb_a = Some(
                            BootloaderCb::parse(&bl_data)
                                .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                        );
                    } else if is_cbx {
                        info!(
                            "[builder] CB_X (RGH3 stub) at 0x{:08X} (v{}, 0x{:X} bytes)",
                            off, bl_version, bl_size
                        );
                        bl.cb_x = Some(
                            BootloaderCb::parse(&bl_data)
                                .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                        );
                    } else {
                        info!(
                            "[builder] CB_B at 0x{:08X} (v{}, 0x{:X} bytes)",
                            off, bl_version, bl_size
                        );
                        bl.cb_b = Some(
                            BootloaderCb::parse(&bl_data)
                                .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                        );
                    }
                }
                XenonBlType::SC => {
                    info!(
                        "[builder] SC at 0x{:08X} (v{}, 0x{:X} bytes)",
                        off, bl_version, bl_size
                    );
                    bl.sc = Some(
                        BootloaderSc::parse(&bl_data)
                            .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                    );
                }
                XenonBlType::CD => {
                    info!(
                        "[builder] CD at 0x{:08X} (v{}, 0x{:X} bytes)",
                        off, bl_version, bl_size
                    );
                    bl.cd = Some(
                        BootloaderCd::parse(&bl_data)
                            .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                    );
                }
                XenonBlType::CE => {
                    info!(
                        "[builder] CE at 0x{:08X} (v{}, 0x{:X} bytes)",
                        off, bl_version, bl_size
                    );
                    bl.ce = Some(
                        BootloaderCe::parse(&bl_data)
                            .map_err(|e| BuilderError::Bootloader(e.to_string()))?,
                    );
                }
                XenonBlType::CF => {
                    info!(
                        "[builder] CF_{} at 0x{:08X} (v{}, 0x{:X} bytes)",
                        cf_count + 1,
                        off,
                        bl_version,
                        bl_size
                    );
                    let cf = BootloaderCf::parse(&bl_data)
                        .map_err(|e| BuilderError::Bootloader(e.to_string()))?;
                    if cf_count == 0 {
                        update.cf_0 = Some(cf);
                        cf0_offset = off;
                    } else {
                        update.cf_1 = Some(cf);
                        cf1_offset = off;
                    }
                    cf_count += 1;
                }
                XenonBlType::CG => {
                    info!(
                        "[builder] CG_{} at 0x{:08X} (v{}, 0x{:X} bytes)",
                        cg_count + 1,
                        off,
                        bl_version,
                        bl_size
                    );
                    let cf_meta = if cg_count == 0 {
                        update.cf_0.as_ref().and_then(|cf| cf.metadata.as_ref())
                    } else {
                        update.cf_1.as_ref().and_then(|cf| cf.metadata.as_ref())
                    };
                    let actual_data = fetch_cg_data(
                        off, bl_size, cg_count, cf0_offset, cf1_offset, cf_meta, image, &layout, &flashfs,
                    );
                    if actual_data.is_empty() {
                        info!("[builder] Failed to fetch complete CG data, stopping chain walk");
                        break;
                    }
                    let cg = BootloaderCg::parse(&actual_data)
                        .map_err(|e| BuilderError::Bootloader(e.to_string()))?;
                    if cg_count == 0 {
                        update.cg_0 = Some(cg);
                    } else {
                        update.cg_1 = Some(cg);
                    }
                    cg_count += 1;
                }
                _ => {
                    // For zeropaired or unencryped payloads
                    let zero_key = [0u8; 16];
                    let probe_len = 0x100.min(image.len().saturating_sub(off));
                    if probe_len >= 0x10 {
                        if let Some(disco) =
                            bl_try_identify(&image[off..off + probe_len], &zero_key)
                        {
                            info!(
                                "[builder] Discovery probe at 0x{:08X}: {} v{} (0x{:X} bytes) via {} key — not a standard chain type, stopping",
                                off, disco.magic, disco.version, disco.size, disco.key_source
                            );
                        } else {
                            info!(
                            "[builder] Unknown bootloader type at 0x{:08X}, stopping chain walk",
                            off
                        );
                        }
                    } else {
                        info!(
                            "[builder] Unknown bootloader type at 0x{:08X}, stopping chain walk",
                            off
                        );
                    }
                    break;
                }
            }

            off += aligned_size;
        }

        // if not found in primary walk, try CF_Ptr
        if update.cf_0.is_none() && cf_ptr > 0 && cf_ptr < image.len() {
            info!(
                "[builder] CF not found after CE, trying CF_Ptr at 0x{:08X}",
                cf_ptr
            );
            off = cf_ptr;

            while off + 0x10 <= image.len() && cf_count < 2 {
                let bl_header = match BootloaderHeader::read_from_prefix(&image[off..off + 0x10]) {
                    Ok((h, _)) => h,
                    Err(_) => break,
                };

                let bl_size = bl_header.size.get() as usize;
                if bl_size < 0x10 || bl_size > 0x2000000 {
                    break;
                }
                if off + bl_size > image.len() {
                    break;
                }

                let bl_data = image[off..off + bl_size].to_vec();
                let aligned_size = (bl_size + 0xF) & 0xFFFFFFF0;

                match bl_header.get_type() {
                    XenonBlType::CF => {
                        info!(
                            "[builder] CF_{} at 0x{:08X} (v{}, 0x{:X} bytes)",
                            cf_count + 1,
                            off,
                            bl_header.version.get(),
                            bl_size
                        );
                        let cf = BootloaderCf::parse(&bl_data)
                            .map_err(|e| BuilderError::Bootloader(e.to_string()))?;
                        if cf_count == 0 {
                            update.cf_0 = Some(cf);
                            cf0_offset = off;
                        } else {
                            update.cf_1 = Some(cf);
                            cf1_offset = off;
                        }
                        cf_count += 1;
                    }
                    XenonBlType::CG => {
                        info!(
                            "[builder] CG_{} at 0x{:08X} (v{}, 0x{:X} bytes)",
                            cg_count + 1,
                            off,
                            bl_header.version.get(),
                            bl_size
                        );
                        let cf_meta = if cg_count == 0 {
                            update.cf_0.as_ref().and_then(|cf| cf.metadata.as_ref())
                        } else {
                            update.cf_1.as_ref().and_then(|cf| cf.metadata.as_ref())
                        };
                        let actual_data = fetch_cg_data(
                            off, bl_size, cg_count, cf0_offset, cf1_offset, cf_meta, image, &layout, &flashfs,
                        );
                        if actual_data.is_empty() {
                            info!("[builder] Failed to fetch complete CG data, stopping CF_Ptr chain walk");
                            break;
                        }
                        let cg = BootloaderCg::parse(&actual_data)
                            .map_err(|e| BuilderError::Bootloader(e.to_string()))?;
                        if cg_count == 0 {
                            update.cg_0 = Some(cg);
                        } else {
                            update.cg_1 = Some(cg);
                        }
                        cg_count += 1;
                    }
                    _ => break,
                }

                off += aligned_size;
            }

            // if CF still not found after direct CF_Ptr read, scan in 0x10-byte steps through a 128KB window
            if update.cf_0.is_none() {
                let zero_key = [0u8; 16];
                let scan_range = 0x20000;
                let image_ref: &[u8] = image;
                let mut read_fn = |offset: usize, len: usize| -> Option<Vec<u8>> {
                    if offset + len <= image_ref.len() {
                        Some(image_ref[offset..offset + len].to_vec())
                    } else {
                        None
                    }
                };
                if let Some((found_off, disco)) =
                    bl_scan(&mut read_fn, cf_ptr, scan_range, &zero_key)
                {
                    info!(
                        "[builder] Discovery scan found {} v{} at 0x{:08X} via {} key",
                        disco.magic, disco.version, found_off, disco.key_source
                    );
                    off = found_off;
                    while off + 0x10 <= image.len() && cf_count < 2 {
                        let bl_header =
                            match BootloaderHeader::read_from_prefix(&image[off..off + 0x10]) {
                                Ok((h, _)) => h,
                                Err(_) => break,
                            };
                        let bl_size = bl_header.size.get() as usize;
                        if bl_size < 0x10 || bl_size > 0x2000000 {
                            break;
                        }
                        if off + bl_size > image.len() {
                            break;
                        }
                        let bl_data = image[off..off + bl_size].to_vec();
                        let aligned_size = (bl_size + 0xF) & 0xFFFFFFF0;
                        match bl_header.get_type() {
                            XenonBlType::CF => {
                                info!(
                                    "[builder] CF_{} at 0x{:08X} (v{}, 0x{:X} bytes) [discovery]",
                                    cf_count + 1,
                                    off,
                                    bl_header.version.get(),
                                    bl_size
                                );
                                let cf = BootloaderCf::parse(&bl_data)
                                    .map_err(|e| BuilderError::Bootloader(e.to_string()))?;
                                if cf_count == 0 {
                                    update.cf_0 = Some(cf);
                                    cf0_offset = off;
                                } else {
                                    update.cf_1 = Some(cf);
                                    cf1_offset = off;
                                }
                                cf_count += 1;
                            }
                            XenonBlType::CG => {
                                info!(
                                    "[builder] CG_{} at 0x{:08X} (v{}, 0x{:X} bytes) [discovery]",
                                    cg_count + 1,
                                    off,
                                    bl_header.version.get(),
                                    bl_size
                                );
                                let cf_meta = if cg_count == 0 {
                                    update.cf_0.as_ref().and_then(|cf| cf.metadata.as_ref())
                                } else {
                                    update.cf_1.as_ref().and_then(|cf| cf.metadata.as_ref())
                                };
                                let actual_data = fetch_cg_data(
                                    off, bl_size, cg_count, cf0_offset, cf1_offset, cf_meta, image, &layout, &flashfs,
                                );
                                if actual_data.is_empty() {
                                    info!("[builder] Failed to fetch complete CG data, stopping discovery scan");
                                    break;
                                }
                                let cg = BootloaderCg::parse(&actual_data)
                                    .map_err(|e| BuilderError::Bootloader(e.to_string()))?;
                                if cg_count == 0 {
                                    update.cg_0 = Some(cg);
                                } else {
                                    update.cg_1 = Some(cg);
                                }
                                cg_count += 1;
                            }
                            _ => break,
                        }
                        off += aligned_size;
                    }
                } else {
                    info!(
                        "[builder] Discovery scan: no CF found in 0x{:08X}..0x{:08X}",
                        cf_ptr,
                        cf_ptr + scan_range
                    );
                }
            }
        }

        Ok((bl, update))
    }
}

pub fn bl_is_valid_magic(magic: u16) -> bool {
    matches!(
        magic & 0xFFF,
        0x342 | 0x343 | 0x344 | 0x345 | 0x346 | 0x347 | 0x341
    )
}

pub fn bl_magic_to_str(magic: u16) -> String {
    match magic {
        0x4342 | 0x5342 => "CB",
        0x4343 | 0x5343 => "SC",
        0x4344 | 0x5344 => "CD",
        0x4345 | 0x5345 => "CE",
        0x4346 | 0x5346 => "CF",
        0x4347 | 0x5347 => "CG",
        0x0341 => "1BL",
        _ => "??",
    }
    .to_string()
}

pub fn hex_to_bytes(hex: &str) -> Result<Vec<u8>> {
    if hex.len() % 2 != 0 {
        return Err(BuilderError::InvalidHex(format!(
            "odd length ({}): '{}'",
            hex.len(),
            hex
        )));
    }
    (0..hex.len())
        .step_by(2)
        .map(|i| {
            u8::from_str_radix(&hex[i..i + 2], 16)
                .map_err(|e| BuilderError::InvalidHex(format!("byte '{}': {}", &hex[i..i + 2], e)))
        })
        .collect()
}

pub fn bl_try_identify(data: &[u8], parent_key: &[u8; 16]) -> Option<BlDiscovery> {
    if data.len() < 0x10 {
        return None;
    }

    let magic_val = u16::from_be_bytes([data[0], data[1]]);
    let version = u16::from_be_bytes([data[2], data[3]]);
    let size = u32::from_be_bytes([data[12], data[13], data[14], data[15]]);

    if bl_is_valid_magic(magic_val) && version >= 1888 && version < 20000 && size < 0x2000000 {
        return Some(BlDiscovery {
            magic: bl_magic_to_str(magic_val),
            version,
            size,
            key_source: "Plain".to_string(),
        });
    }

    let devkit_key = [0u8; 16];
    let key_sources: [(&str, &[u8; 16]); 3] = [
        ("Retail", &NAND_RETAIL_1BL_KEY),
        ("Devkit", &devkit_key),
        ("Chain", parent_key),
    ];

    for (key_name, key_base) in key_sources {
        if key_name == "Chain" && key_base.iter().all(|&x| x == 0) {
            continue;
        }
        for &salt_off in &[0x10usize, 0x20usize] {
            if data.len() < salt_off + 0x10 {
                continue;
            }
            let dk = match hmac_sha(key_base, &[&data[salt_off..salt_off + 0x10]]) {
                Ok(k) => k,
                Err(_) => continue,
            };
            let mut block = data[0..0x10].to_vec();
            if let Ok(mut rc4) = Rc4::new(&dk[..0x10]) {
                let _ = rc4.crypt(&mut block);
            } else {
                continue;
            }

            let dm = u16::from_be_bytes([block[0], block[1]]);
            let dv = u16::from_be_bytes([block[2], block[3]]);
            let ds = u32::from_be_bytes([block[12], block[13], block[14], block[15]]);

            if bl_is_valid_magic(dm) && dv >= 1888 && dv < 20000 && ds < 0x2000000 {
                return Some(BlDiscovery {
                    magic: bl_magic_to_str(dm),
                    version: dv,
                    size: ds,
                    key_source: key_name.to_string(),
                });
            }
        }
    }
    None
}

/// Scans `scan_range` bytes from `start_offset` in 0x10-byte steps for a valid bootloader.
pub fn bl_scan<F>(
    read_fn: &mut F,
    start_offset: usize,
    scan_range: usize,
    parent_key: &[u8; 16],
) -> Option<(usize, BlDiscovery)>
where
    F: FnMut(usize, usize) -> Option<Vec<u8>>,
{
    for offset in (start_offset..start_offset + scan_range).step_by(0x10) {
        let buf = read_fn(offset, 0x100)?;
        if let Some(r) = bl_try_identify(&buf, parent_key) {
            info!(
                "[builder] bl_scan: found {} v{} at 0x{:08X} via {} key",
                r.magic, r.version, offset, r.key_source
            );
            return Some((offset, r));
        }
    }
    None
}
