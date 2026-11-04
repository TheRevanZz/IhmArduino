#!/bin/sh
printf '\033c\033]0;%s\a' MPS - IHM
base_path="$(dirname "$(realpath "$0")")"
"$base_path/mps-hmi.arm64" "$@"
