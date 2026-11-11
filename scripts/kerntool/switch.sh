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

rm -f compile_flags.txt
ln -s "./config/compile-flags/$1.txt" "compile_flags.txt"
echo "compile_flags.txt linked to config/compile-flags/$1.txt"
