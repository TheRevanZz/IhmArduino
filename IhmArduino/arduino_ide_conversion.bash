#!/usr/bin/bash

destination_dir="$1" 

if [ -z "$destination_dir" ]; then
    echo "Usage: $0 <destination_directory>"
    exit 1
fi

if ! [ -d "$destination_dir" ]; then
  mkdir "$destination_dir"
fi

iterate() {

    local read_dir="$1"

    for file in "$read_dir"/*; do
        if [ -f "$file" ]; then
            filename=$(basename "$file")
            cp "$file" "$destination_dir/$filename"
        fi 

        if [ -d "$file" ]; then
            iterate "$file"
        fi
    done
}

iterate "./src"
iterate "./include"

current_location=$(pwd)


cd "$destination_dir"

find . -type f \( -name "*.cpp" -o -name "*.h" \) \
  -exec sed -i -E 's|(#include[[:space:]]+")(.*/)([^/"]+")|\1\3|' {} +

cd "$current_location"
mv "$destination_dir/$2" "$destination_dir/${destination_dir}.ino"

