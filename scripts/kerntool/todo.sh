# SalernOS kernel / kerntool
# Copyright (C) 2021 - 2025 Alessandro Salerno
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
  echo "this command helps you find forgotten to-do statements in the code"
  echo "example: ./kerntool todo"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

TODOS=`rg -i "// TODO" --vimgrep`
echo "$TODOS"
