#!/bin/sh

HEADER="header.txt"

if [ ! -f "$HEADER" ]; then
  echo "Error: $HEADER not found"
  exit 1
fi

for f in *.c *.h; do
  # Skip if no matches
  [ -e "$f" ] || continue

  first_line=$(sed -n '1p' "$f")

  case "$first_line" in
    '//'*) continue ;;
    '/*'*) continue ;;
  esac

  echo "Prepending header to $f"

  tmp=$(mktemp "${f}.XXXXXX") || exit 1

  {
    cat "$HEADER"
    echo
    cat "$f"
  } > "$tmp"

  mv "$tmp" "$f"
done
