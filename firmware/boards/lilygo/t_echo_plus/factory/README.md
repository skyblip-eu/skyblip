# What the factory bootloader reports

`INFO_UF2.TXT` is copied verbatim off the TECHOBOOT volume of our own T-Echo Plus, read on 12 September 2026 before anything had been installed on it. It is the only evidence we have for what the partition map in `../t_echo_plus.dts` assumes, so it is kept rather than quoted.

What it pins down:

| The file says | What depends on it |
|---|---|
| `SoftDevice: S140 version 6.1.1` | `softdevice_partition`, 0x0..0x26000, and `mkuf2.py`'s refusal to write below it |
| `UF2 Bootloader 0.6.1-2-g1224915` | the write window, below |
| `Board-ID: nRF52840-TEcho-v1` | the SoC variant, which is what picks the window's size |

At tag 0.6.1 the write window is `USER_FLASH_START..USER_FLASH_END` = `MBR_SIZE .. BOOTLOADER_REGION_START - DFU_APP_DATA_RESERVED` (`src/usb/uf2/uf2cfg.h:20-21`), and the nRF52840 branch of `Makefile:117` sets `DFU_APP_DATA_RESERVED = 10*4096`. With the region starting at 0xF4000 that is 0x1000..0xEA000, which is what `scripts/mkuf2.py` guards and `scripts/test_mkuf2.py` asserts. The device is two commits past that tag on LilyGO's build, and neither constant moved between them.

A unit that reports a different bootloader or SoftDevice version has not been checked against any of this. Read its `INFO_UF2.TXT` first: a smaller `USER_FLASH_END` silently truncates an install, and that is the failure the guards in `mkuf2.py` exist to catch.
