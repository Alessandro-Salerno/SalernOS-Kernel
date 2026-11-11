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
