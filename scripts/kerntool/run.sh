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
  echo "this command is used to build and run the kernel"
  echo "example: ./kerntool run"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

debug_flags=

if [ "$1" == "debug" ]; then
  debug_flags="-S -s"
fi

if [ "$1" == "monitor" ]; then
  debug_flags="-monitor stdio"
fi

make \
  && ./kerntool mkiso \
  && qemu-system-x86_64 -M q35 -m 2g -smp cpus=1 -no-shutdown -no-reboot -debugcon file:/dev/stdout -serial file:/dev/stdout -netdev user,id=net0 -device virtio-net,netdev=net0 -object filter-dump,id=f1,netdev=net0,file=netdump.dat -cdrom image.iso $debug_flags
