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

error() {
  echo "kerntool error: $1, see ./kerntool switch ?"
  exit -1
}

info() {
  echo "this command updates the 'compile_flags.txt' file using the correct one for the given platform"
  echo "example: ./kerntool switch x86-64"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

if [ ! -f "./config/compile-flags/$1.txt" ]; then
  error "unknown platform '$1'"
fi

ln -s "./config/compile-flags/$1.txt" "compile_flags.txt"
echo "compile_flags.txt linked to config/compile-flags/$1.txt"
