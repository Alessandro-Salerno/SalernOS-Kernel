# SalernOS kernel / kerntool
# Copyright (C) 2021 - 2026 Alessandro Salerno
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
  echo "this command formats the whole directory with clang-format"
  echo "example: ./kerntool format"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

find ./src/com -iname '*.h' -o -iname '*.c' | xargs clang-format -i
find ./src/platform -iname '*.h' -o -iname '*.c' | xargs clang-format -i
find ./include/kernel -iname '*.h' -o -iname '*.c' | xargs clang-format -i
find ./include/lib -iname '*.h' -o -iname '*.c' | xargs clang-format -i
