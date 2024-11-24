# SalernOS kernel / kerntool
# Copyright (C) 2021 - 2024 Alessandro Salerno
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>. 

info() {
  echo "this command is used to generate an ISO image of the OS"
  echo "example: ./kerntool mkiso"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

git clone https://github.com/limine-bootloader/limine.git --branch=v8.x-binary --depth=1

make -C limine

mkdir -p iso_root

mkdir -p iso_root/boot
cp -v bin/vmsalernos iso_root/boot/
mkdir -p iso_root/boot/limine
cp -v limine.conf limine/limine-bios.sys limine/limine-bios-cd.bin \
      limine/limine-uefi-cd.bin iso_root/boot/limine/

mkdir -p iso_root/EFI/BOOT
cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/

xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
        -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        iso_root -o image.iso

./limine/limine bios-install image.iso
