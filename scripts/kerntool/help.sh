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
  echo "this command gives you information on how to use kerntool"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

echo "kerntool commands:"

for filename in ./scripts/kerntool/*; do
  IFS='/'
  read -ra pathsep <<< "${filename}"
  short_name=${pathsep[-1]}
  IFS='.'
  read -ra dotsep <<< "${short_name}"
  command=${dotsep[0]}
  echo "        ${command}"
done

echo
echo "commands can be invoked as follows: ./kerntool <command> [<arguments>]"
echo "additional information on each commaand can be found using the '?' argument"
echo "example: ./kerntool mkiso ?"
