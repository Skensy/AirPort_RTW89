#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

root=$1
out=$2
driver_zip=$3
make_cmd=${4:-make}
bundle=OwlyAirLinkBridge.kext

fail() {
    echo "rtl8822ce-eventbridge: $*" >&2
    exit 1
}

pins=$root/src/rtl8822ce/eventbridge-multicast.pins
[ -f "$pins" ] || fail "missing reviewed descriptor: $pins"
pin() {
    n=$(grep -c "^$1=" "$pins" || true)
    [ "$n" -eq 1 ] || fail "$1 must be declared once in $pins"
    sed -n "s/^$1=//p" "$pins"
}
driver_version=$(pin DRIVER_VERSION)
build_label_from=$(pin DRIVER_BUILD_FROM)
driver_build=$(pin DRIVER_BUILD_TO)
driver_sha=$(pin DRIVER_SHA256)
driver_uuid=$(pin DRIVER_UUID)
family_uuid=$(pin FAMILY_UUID)
name=OwlyAirLinkBridge-rtl8822ce-$driver_version.zip

[ -f "$driver_zip" ] || fail "missing paired driver ZIP: $driver_zip"
work=$(mktemp -d /tmp/rtl8822ce-eventbridge.XXXXXX)
pending=''
trap 'rm -rf "$work"; if [ -n "$pending" ]; then rm -f "$pending"; fi' EXIT
trap 'exit 1' HUP INT TERM

unzip -p "$driver_zip" AirPort_RTW88.kext/Contents/Info.plist > "$work/Info.plist"
unzip -p "$driver_zip" AirPort_RTW88.kext/Contents/MacOS/AirPort_RTW88 > "$work/driver"
python3 - "$work/Info.plist" "$driver_version" <<'PY'
import plistlib, sys
with open(sys.argv[1], 'rb') as f:
    p = plistlib.load(f)
expected = {'CFBundleIdentifier': 'com.rtw88.airport', 'CFBundleExecutable': 'AirPort_RTW88',
            'CFBundleVersion': sys.argv[2], 'CFBundleShortVersionString': sys.argv[2]}
if any(p.get(k) != v for k, v in expected.items()):
    sys.exit('Unexpected RTW88 bundle identity')
PY
(cd "$work" && echo "$driver_sha  driver" | shasum -a 256 -c -) || fail "incorrect RTW88 executable"
[ "$(lipo -archs "$work/driver")" = x86_64 ] || fail "incorrect architecture"
xcrun dwarfdump --uuid "$work/driver" | grep -Fq "UUID: $driver_uuid (x86_64)" || fail "incorrect driver UUID"

# Patch a private source copy. The vendor snapshot can be checked out with CRLF.
cp -R "$root/bridge/src" "$work/src"
python3 - "$work/src" <<'PY'
from pathlib import Path
import sys
for name in ('Plugin.cpp', 'OwlyTargets.hpp'):
    path = Path(sys.argv[1]) / name
    path.write_bytes(path.read_bytes().replace(b'\r\n', b'\n'))
PY
(cd "$work/src" && patch --batch -p1 < "$root/src/rtl8822ce/eventbridge.patch")
python3 - "$work/src/Plugin.cpp" "$build_label_from" "$driver_build" <<'PY'
from pathlib import Path
import difflib, sys
path, old_label, new_label = sys.argv[1:4]
old = 'stringPropertyMatches(radio, "DriverBuild", "%s")' % old_label
new = 'stringPropertyMatches(radio, "DriverBuild", "%s")' % new_label
text = Path(path).read_text()
if text.count(old) != 1:
    sys.exit('eventbridge: DriverBuild origin must appear exactly once')
for guard in ('radio->metaCast("AirportRTW88")',
              'stringPropertyMatches(radio, "AWDL_OUTPUT_FILTER_BSD", "awdl0")'):
    if text.count(guard) != 1:
        sys.exit('eventbridge: unexpected DriverBuild origin context')
lines = text.splitlines(keepends=True)
hits = [i for i, line in enumerate(lines) if old in line]
if len(hits) != 1:
    sys.exit('eventbridge: DriverBuild line must appear exactly once')
lines[hits[0]] = lines[hits[0]].replace(old, new)
updated = ''.join(lines)
changed = [l for l in difflib.unified_diff(text.splitlines(), updated.splitlines(), lineterm='')
           if l[:1] in '+-' and not l.startswith(('+++', '---'))]
if len(changed) != 2 or old not in changed[0] or new not in changed[1]:
    sys.exit('eventbridge: build-label materialization changed more than one line')
Path(path).write_text(updated)
PY
# The working bundle was signed with a CRLF Info.plist. Its exact bytes
# contribute to the signature even when the parsed plist is identical.
python3 - "$root/bridge/Info.plist" "$work/Info-bridge.plist" <<'PYTHON'
from pathlib import Path
import sys
Path(sys.argv[2]).write_bytes(Path(sys.argv[1]).read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
PYTHON
"$make_cmd" -C "$root" bridge BRIDGE_PLIST="$work/Info-bridge.plist" BRIDGE_DEPS= BRIDGE_UUID="$driver_uuid" \
    BRIDGE_SRC="$work/src" BRIDGE_OBJ="$work/obj" BRIDGE_BUNDLE="$work/$bundle"

exe="$work/$bundle/Contents/MacOS/OwlyAirLinkBridge"
[ "$(lipo -archs "$exe")" = x86_64 ] || fail "incorrect bridge architecture"
cmp "$work/Info-bridge.plist" "$work/$bundle/Contents/Info.plist"
python3 "$root/tools/bind_bridge.py" --check "$driver_uuid" "$exe"
python3 "$root/tools/bind_bridge.py" --check "$family_uuid" "$exe"
codesign --verify --strict "$work/$bundle"
strings "$exe" > "$work/strings"
for expected in com.rtw88.airport AirportRTW88 "$driver_build" AWDL_BOOTSTRAP_READY AWDL_OUTPUT_FILTER_BSD; do
    grep -Fxq "$expected" "$work/strings" || fail "missing RTW88 identifier: $expected"
done
if grep -Fxq com.rtw89.front "$work/strings"; then fail "incorrect RTW89 target"; fi
if grep -Fxq "$build_label_from" "$work/strings"; then
    fail "stale build label: $build_label_from"
fi

echo "a7b75a5b8b02c293fdfcb6e432350fcfba6cdd929d2f9cdae4a5ea1b7200c3b5  $exe" | shasum -a 256 -c - || fail "bridge differs from the working EFI"

mkdir -p "$out"
pending=$(mktemp "$out/.eventbridge.XXXXXX")
ditto -c -k --norsrc --keepParent "$work/$bundle" "$pending"
unzip -tq "$pending"
chmod 644 "$pending"
mv -f "$pending" "$out/$name"
pending=''
echo "Built $out/$name"
