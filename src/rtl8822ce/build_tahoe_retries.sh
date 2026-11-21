#!/bin/sh
# SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
set -eu

patch_dir=$(cd "$(dirname "$0")" && pwd)
out_dir=$1
mkdir -p "$out_dir"
out_dir=$(cd "$out_dir" && pwd)
work=$(mktemp -d /tmp/rtl8822ce-tahoe.XXXXXX)
archive=$(mktemp "$out_dir/.AirPort_RTW88.XXXXXX")
trap 'rm -rf "$work"; rm -f "$archive"' EXIT
trap 'exit 1' HUP INT TERM

cd "$work"
commit=6475f144f9620d1679a788f0f49052d8f06b944a
curl --fail --location --silent --show-error --retry 2 \
    "https://raw.githubusercontent.com/xnoah222/Realtek-AirPort-Family/$commit/AirPort_RTW88-Source.zip" -o source.zip
echo '3d696ccc8066603044d280b01e1784bcc91fad1f1c8d2b2156f3df07308a47ba  source.zip' | shasum -a 256 -c -
unzip -q source.zip
cd AirPort_RTW88/Feixiao
patch -p1 < "$patch_dir/tahoe-retries.patch"

python3 tests/test_version_coherence.py
python3 tests/test_v216_awdl_retries.py
python3 tests/test_v214_awdl_output_ownership.py
python3 tests/test_v211_awdl_bsd_input.py
make airport
bundle=build/out/AirPort_RTW88.kext
exe="$bundle/Contents/MacOS/AirPort_RTW88"
xcrun nm -uj "$exe" > "$work/imports"
for symbol in _bpf_tap_in _bpf_tap_out; do
    grep -Fxq "$symbol" "$work/imports"
done
if grep -Eq '__Z[0-9]+bpf_tap_(in|out)' "$work/imports"; then
    echo "BPF functions have C++ linkage" >&2
    exit 1
fi
cmp AirPort_RTW88.kext/Contents/Info.plist "$bundle/Contents/Info.plist"
codesign --force --sign - --timestamp=none "$bundle"
codesign --verify --strict "$bundle"
ditto -c -k --norsrc --keepParent "$bundle" "$archive"
unzip -tq "$archive"
chmod 644 "$archive"
mv -f "$archive" "$out_dir/AirPort_RTW88-2.0.2d1.zip"
echo "Built $out_dir/AirPort_RTW88-2.0.2d1.zip"
