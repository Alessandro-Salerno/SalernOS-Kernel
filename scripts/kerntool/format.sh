info() {
  echo "this command formats the whole directory with clang-format"
  echo "example: ./kerntool format"
}

if [ "$1" == "?" ]; then
  info
  exit 0
fi

find ./src/com ./src/platform ./src/opt ./include/kernel \
  -type d -name vendor -prune -o \
  -type f \( -iname '*.h' -o -iname '*.c' \) -print0 | xargs -0 -r clang-format -i
