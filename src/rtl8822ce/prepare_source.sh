#!/bin/sh
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
set -eu
patch_dir=$(cd "$(dirname "$0")" && pwd)
dest=$1
if [ -e "$dest" ]; then
    echo "Source directory already exists. Save local edits and move it aside before preparing updated inputs: $dest" >&2
    exit 1
fi
mkdir -p "$(dirname "$dest")"
work=$(mktemp -d "${dest}.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
cd "$work"
archive="$patch_dir/../../third_party/rtl8822ce/AirPort_RTW88-Source.zip"
echo "3d696ccc8066603044d280b01e1784bcc91fad1f1c8d2b2156f3df07308a47ba  $archive" | shasum -a 256 -c -
unzip -q "$archive"
cd AirPort_RTW88/Feixiao
patch -F 0 -p1 < "$patch_dir/tahoe-retries.patch"
patch -F 0 -p1 < "$patch_dir/tahoe-multicast.patch"
cd ..
patch -F 0 -p1 < "$patch_dir/tahoe-audit.patch"
patch -F 0 -p1 < "$patch_dir/tahoe-awdl-tx.patch"
cd "$work"
touch .prepared
mv "$work" "$dest"
