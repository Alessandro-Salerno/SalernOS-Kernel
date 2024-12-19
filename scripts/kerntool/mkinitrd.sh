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
  echo "this command is used to generate an initrd tarball"
  echo "example: ./kerntool mkinitrd"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

mkdir -p ./initrd \
  && mkdir -p ./bin/initrd \
  && cp ./initrd/* ./bin/initrd/ \
  && cd ./bin/initrd/ \
  && nasm -felf64 -g ./test.asm -o ./test.asm.o \
  && ld ./test.asm.o -m elf_x86_64 -o ./test \
  && tar -cf ../initrd.tar . \
  && cd ../.. \
  && cp ./bin/initrd.tar ./iso_root/initrd
