[![Windows Build Status](https://img.shields.io/github/actions/workflow/status/pbatard/uefi-md5sum/Windows.yml?style=flat-square&label=VS2022/gnu-efi%20Build)](https://github.com/pbatard/uefi-md5sum/actions/workflows/Windows.yml)
[![Linux Build status](https://img.shields.io/github/actions/workflow/status/pbatard/uefi-md5sum/Linux.yml?style=flat-square&label=gcc/EDK2%20Build)](https://github.com/pbatard/uefi-md5sum/actions/workflows/Linux.yml)
[![Tests status](https://img.shields.io/github/actions/workflow/status/pbatard/uefi-md5sum/Tests.yml?style=flat-square&label=Tests)](https://github.com/pbatard/uefi-md5sum/actions/workflows/Tests.yml)
[![Coverity Scan Build Status](https://img.shields.io/coverity/scan/29422.svg?style=flat-square&label=Coverity)](https://scan.coverity.com/projects/pbatard-uefi-md5sum)
[![Licence](https://img.shields.io/badge/license-GPLv2-blue.svg?style=flat-square&label=License)](https://www.gnu.org/licenses/gpl-2.0.en.html)

uefi-md5sum - MD5 checksum validation for UEFI
==============================================

## Description

uefi-md5sum is a UEFI bootloader designed to perform MD5 hash verification of a
media containing an `md5sum.txt` list of hashes.

This is primarily aimed at being used with [Rufus](https://rufus.ie) for the
creation of USB bootable media (such as Linux or Windows installation drives)
that can perform self-verification on each boot.

The reasoning with wanting to perform validation on boot rather than on media
creation is based on the fact that flash based media, and especially cheap USB
flash drives or SD cards, are exceedingly prone to failures **after** the media
was written.

As such, we assert that, only validating the content at write-time (like
balenaEtcher and, in part, Microsoft's Media Creation Tool do) is not enough to
help users ensure that their installation media hasn't become compromised.

This boot time validation can also prove itself useful if the boot process or
installation process produces errors, in which case, the first thing a user
may want to do, is reboot and let uefi-md5sum perform its check, to highlight
or rule out data corruption.

## Usage

uefi-md5sum is intended to replace, and then chain load, the original UEFI
bootloader.

To accomplish this, the original `/efi/boot/boot###.efi` should be renamed to
`/efi/boot/boot###_original.efi` with uefi-md5sum bootloader then installed as
`/efi/boot/boot###.efi`.

## md5sum.txt extensions

If `md5sum.txt` sets an `md5sum_totalbytes` variable, in the form of a comment
similar to:
```
# md5sum_totalbytes = 0x1234abcd
```
Then uefi-md5sum interprets this value to be sum of all the file sizes of the
files referenced in `md5sum.txt`, and uses it for more accurate progress
reporting. Otherwise, progress is only incremented after each file has been
processed, regardless of its actual size.

Thus, the provision of `md5sum_totalbytes` allows for accurate progress report,
as well the avoidance of apparent progress "freezeouts" when very large files
are being hashed (such as large squashfs or install.wim images).

It should be noted however that, currently, uefi-md5sum supports only the
provision of an `md5sum_totalbytes` value in **lowercase** hexadecimal (no
uppercase hex, no decimal). On the other hand, there is no restriction to where,
in `md5sum.txt`, `md5sum_totalbytes` needs to be specified (i.e. it does not
necessarily mean to appear at the beginning of the file).

## Prerequisites

* [Visual Studio 2022](https://www.visualstudio.com/vs/community/) or gcc/EDK2.
* [QEMU](http://www.qemu.org) __v2.7 or later__
  (NB: You can find QEMU Windows binaries [here](https://qemu.weilnetz.de/w64/))

## Compilation

* If using the Visual Studio solution (`.sln`), just press <kbd>F5</kbd> to
have the application compiled and launched in the QEMU emulator. Remember
however that you may first have to initialize the `gnu-efi` git submodules.

* If using gcc with EDK2 on Linux, and assuming that your edk2 directory resides
in `/usr/src/edk2`:  

        export EDK2_PATH="/usr/src/edk2"
        export WORKSPACE=$PWD
        export PACKAGES_PATH=$WORKSPACE:$EDK2_PATH
        . $EDK2_PATH/edksetup.sh --reconfig
        build -a X64 -b RELEASE -t GCC5 -p uefi-md5sum.dsc

## Testing

* The automated GitHub Actions build process is designed to run a very
comprehensive list of tests under QEMU. You can find a detailed summary of
all the tests being run in `tests/test_list.txt`.
