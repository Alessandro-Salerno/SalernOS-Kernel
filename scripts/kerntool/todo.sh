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
