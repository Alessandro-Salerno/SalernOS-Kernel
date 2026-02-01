#!/usr/bin/env bash

# I don't take credit for this, it is AI-generated because I am unabel to write
# bash scripts like this

RULE_DIR="./config/fix-license"

if [[ ! -d "$RULE_DIR" ]]; then
  echo "$RULE_DIR not found"
  exit 1
fi

# Build a list of rule files
mapfile -t RULE_FILES < <(find "$RULE_DIR" -type f ! -path '*/.*/*')

if [[ ${#RULE_FILES[@]} -eq 0 ]]; then
  echo "no rule files found in $RULE_DIR"
  exit 0
fi

find . -type f \
  ! -path './.*/*' |
while IFS= read -r file; do
  base="${file##*/}"

  # Skip files without an extension
  [[ "$base" != *.* ]] && continue

  ext="${base##*.}"

  for rule in "${RULE_FILES[@]}"; do
    rule_name="$(basename "$rule")"

    # rule_name: c-cpp-h-hpp-S
    IFS='-' read -r -a exts <<< "$rule_name"

    for rule_ext in "${exts[@]}"; do
      if [[ "$ext" == "$rule_ext" ]]; then
        # Check second line
        if sed -n '2p' "$file" | grep -q 'SalernOS'; then
          tmp="$(mktemp)"

          # Apply header + remainder of file
          cat "$rule" > "$tmp"
          tail -n +18 "$file" >> "$tmp"

          chmod --reference="$file" "$tmp"
          mv "$tmp" "$file"

          echo "Updated: $file (rule: $rule_name)"
        fi

        # Stop after first matching rule
        break 2
      fi
    done
  done
done

