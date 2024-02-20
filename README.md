[![Windows Build Status](https://img.shields.io/github/actions/workflow/status/pbatard/uefi-md5sum/Windows.yml?style=flat-square&label=VS2022/gnu-efi%20Build)](https://github.com/pbatard/uefi-md5sum/actions/workflows/Windows.yml)
[![Linux Build status](https://img.shields.io/github/actions/workflow/status/pbatard/uefi-md5sum/Linux.yml?style=flat-square&label=gcc/EDK2%20Build)](https://github.com/pbatard/uefi-md5sum/actions/workflows/Linux.yml)
[![Tests status](https://img.shields.io/github/actions/workflow/status/pbatard/uefi-md5sum/Tests.yml?style=flat-square&label=Tests)](https://github.com/pbatard/uefi-md5sum/actions/workflows/Tests.yml)
[![Coverity Scan Build Status](https://img.shields.io/coverity/scan/29422.svg?style=flat-square&label=Coverity)](https://scan.coverity.com/projects/pbatard-uefi-md5sum)
[![Licence](https://img.shields.io/badge/license-GPLv2-blue.svg?style=flat-square&label=License)](https://www.gnu.org/licenses/gpl-2.0.en.html)

uefi-md5sum - MD5 checksum validation for UEFI
==============================================

uefi-md5sum is a UEFI bootloader designed to perform MD5 checksum verification
from media containing an `md5sum.txt` list of hashes.

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
