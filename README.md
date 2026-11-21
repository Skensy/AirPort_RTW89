# AirPort_RTW89

Native Wi-Fi on macOS for **Realtek rtw89 PCIe cards**, ported from the Linux
rtw89 driver. The card works from the Wi-Fi menu and System Settings like a
Mac's own.

> **Preview.** Works every day on the RTL8852BE it is developed on, and
> testers have it running on an RTL8852AE and an RTL8922DE. Other cards have
> not run it yet. Keep a USB stick that boots your current OpenCore EFI, in
> case macOS does not start.

## Supported cards

| Card | PCI ID (`10EC:`) | Wi-Fi | Status |
|---|---|---|---|
| RTL8852BE | `B852`, `B85B` | 6 | works |
| RTL8852AE | `8852`, `A85A` | 6 | works (testers) |
| RTL8922DE | `892D`, `882D`, `895D` | 7 | works (tester); 6 GHz and Wi-Fi 7 untested |
| RTL8852BTE | `B520` | 6 | untested |
| RTL8851BE | `B851` | 6 | untested |
| RTL8852CE | `C852` | 6E | works (tester) |
| RTL8922AE | `8922`, `892B` | 7 | untested |

USB versions of these cards are not supported.

### RTL8822CE on macOS Tahoe

The RTL8822CE (`10EC:C822` or `10EC:C82F`, Wi-Fi 5) has a separate
`AirPort_RTW88.kext` target. It uses the pinned AirPort_RTW88 frontend,
rtw88 PCIe backend and embedded RTL8822C firmware. On macOS with Xcode
Command Line Tools, run these commands from the repository root:

```sh
git submodule update --init --recursive third_party/AirPort_RTW88 third_party/rtw88-stable
make rtl8822ce
```

The output is `build/out/AirPort_RTW88.kext`. The experimental 1.0.2d2
build translates Tahoe station scan and association requests and connects the AWDL virtual interface to the rtw88 radio on a single
social channel (6, 44 or 149). For an associated test, put the access point
on an allowed fixed social channel and join it first. The driver pauses its
periodic sync transmissions during physical scans. Channel hopping while
associated is currently unsupported.

Use a bootable USB copy of your EFI for this experiment. Load the source-built
RTW88 bundle in place of the existing RTW88 bundle. The RTL8822CE Wi-Fi baseline on Tahoe 26.7.1 (25G241) is the Sonoma–Tahoe
binary from the [Realtek AirPort Family 1.0.1 release](https://github.com/xnoah222/Realtek-AirPort-Family/releases/tag/1.0.1).
Its bundle version is 2.0.1 and the executable SHA-256 is
`a2723f2531695511f5404a10ee4c586e06504da9561827671a26c2df9756eeb7`.
The [upstream 2.0.1 source archive](https://github.com/xnoah222/Realtek-AirPort-Family/blob/6475f144f9620d1679a788f0f49052d8f06b944a/AirPort_RTW88-Source.zip)
has SHA-256 `3d696ccc8066603044d280b01e1784bcc91fad1f1c8d2b2156f3df07308a47ba`.
A local `make airport` in its `Feixiao` directory builds a different executable
with additional BPF imports. Its driver-version response still says 2.0.0.
Use the release executable or a verified EFI backup for exact rollback.

The experimental 1.0.2d2 target in this repository uses an earlier frontend.
To inspect AWDL on that build, open AirDrop on both devices and run these commands
from Terminal:

```sh
kmutil showloaded | grep -Ei 'rtw88|broadtek'
ifconfig awdl0
ioreg -r -c AirportRTW88 -l | grep RTW88AWDL
```

`RTW88AWDLSyncTX` counts locally queued sync frames. `RTW88AWDLActionRX`
counts peer action frames submitted to the native manager. The counters
are published at the first event and every 64 events. Test AirDrop in each
direction and Handoff independently on hardware.

For OpenCore, back up your EFI and use the RTW88 bundle with the Tahoe
legacy Wi-Fi stack. Enable the driver after its dependencies in
`Kernel -> Add`, with executable `Contents/MacOS/AirPort_RTW88` and plist
`Contents/Info.plist`. The RTW89 installer below is for the cards in
the RTW89 table. For RTW88 compatibility and feature limits, see the
[AirPort_RTW88 project](https://github.com/xnoah222/Airport_RTW88).

#### RTL8822CU Bluetooth on Tahoe 26.7.1

On the internal USB radio `1358:C123`, the firmware loader published
`FirmwareLoaded = Yes` and `fw_name = rtl8822cu`. With the previous
internal-name override, bluetoothd selected `BCM_4350C2`, failed the
vendor command `Read Verbose Config Version Info` and restarted (reason 108).

For a hardware trial, merge each of the following plist fragments into
`IOKitPersonalities` in the indicated kext on a backed-up EFI:

| Fragment in `src/rtl8822ce/` | Destination |
| --- | --- |
| `BlueToolFixup-RTL8822CU.plist` | `BlueToolFixup.kext/Contents/Info.plist` |
| `RealtekBluetoothFirmware-RTL8822CU.plist` | `RealtekBluetoothFirmware.kext/Contents/Info.plist` |

When upgrading from 1.0.2d2, remove the three previous BlueToolFixup
personalities named `Realtek RTL8822CU internal Bluetooth` and
`Realtek RTL8822CU Bluetooth interface 0` / `1`. Keep the original
firmware personality `RealtekBluetoothFirmware_1358_c123`. The new device
personality presents vendor ID `0A12` and the product name `Bluetooth Radio`
to Tahoe. The additional firmware match checks for the original-vendor marker
to handle either driver match order. The vendor-ID approach has been
[reported on a different Tahoe Bluetooth controller](https://github.com/So1jon/Hackintosh-Lenovo-Ideapad-3-15IML05#73-fix).

After reboot, verify that the USB device still has `FirmwareLoaded = Yes`,
that System Information reports a powered controller with a valid address,
and that discovery and pairing work. Test sleep/wake and AirDrop separately.
To roll back, restore both previous Info.plist files together.

## What works

- **Networks:** WPA3, WPA2 (Personal and Enterprise), open, phone hotspots.
- **Speed:** Wi-Fi 6, up to 80 MHz (about 500 Mb/s measured). The driver
  prefers a network's 5 GHz side when its signal is good.
- **6 GHz and Wi-Fi 7** on the cards that have them (new, untested on hardware).
- Wi-Fi menu, saved networks, scanning, restart and shutdown.
- **AirDrop, both ways, and Handoff** (new in 0.7.0, on by default), with
  RTLAirLink's AWDL. Working on an RTL8852BE on macOS Tahoe 26.7.3: the iPhone
  lists the Mac, transfers go both ways, and Handoff works. While joined to a
  network on another channel, the card visits AirDrop's channel (6, 44 or
  149) in between. Not on the RTL8851BE or RTL8852AE, which have no channel
  contexts for it. `-owlyeb1off` turns it off.
- **BroadTek.kext comes with it and is needed for AirDrop from an iPhone and
  for Handoff.** It is a separate Lilu plugin for the Bluetooth side
  ([BroadTek](https://github.com/FranciumOS-org/BroadTek), see
  [below](#broadtek)); without it the iPhone never lists the Mac.
- **160 and 320 MHz** channels where card and network both have them (untested on hardware).

**Not yet:** Instant Hotspot, Apple Watch unlock, Sidecar over Wi-Fi, iPhone
Mirroring (needs Apple's Secure Enclave, which a Hackintosh does not have),
Wi-Fi 7 multi-link. Sleep and wake are untested.

Tested on macOS Sequoia 15 and Tahoe 26.

## Requirements

- A Hackintosh booting with **OpenCore**, with **Lilu**.
- `Misc -> Security -> SecureBootModel` set to `Disabled` (the installer can do this).
- The **old Wi-Fi stack**, which macOS needs for this driver. Download all
  three (each link is a zip with the kext inside):
  - [IOSkywalkFamily.kext](https://github.com/dortania/OpenCore-Legacy-Patcher/raw/d9604c36a432eaf243ea18659ff4d208187452d7/payloads/Kexts/Wifi/IOSkywalkFamily-v1.2.0.zip)
  - [IO80211FamilyLegacy.kext](https://github.com/dortania/OpenCore-Legacy-Patcher/raw/d9604c36a432eaf243ea18659ff4d208187452d7/payloads/Kexts/Wifi/IO80211FamilyLegacy-v1.0.0.zip)
  - [AMFIPass.kext](https://github.com/dortania/OpenCore-Legacy-Patcher/raw/d9604c36a432eaf243ea18659ff4d208187452d7/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-RELEASE.zip)

  They come from OpenCore Legacy Patcher; they are Apple's and Dhinak G's, so
  this project does not include them.

## Installing

1. **Back up:** copy your OpenCore EFI to a USB stick and check it boots.
2. Download the three kexts above and unzip them into your **Downloads** folder.
3. Download `AirPort_RTW89-<version>.zip` from
   [Releases](https://github.com/FranciumOS-org/Airport_RTW89/releases) and unzip it.
4. Run the **Kext Installer** that comes with it:
   - **macOS:** double-click `Kext Installer.command` (first time: right-click -> Open).
   - **Windows:** double-click `Kext Installer.cmd`.
   - **Linux:** `sh "Kext Installer.sh"`.

   It finds your `config.plist`, shows what it will change, and asks before
   doing anything. Your old config is saved as `config.plist.pre-airport-rtw89`.
   If Python is missing, it offers to install it.
5. Restart. Wi-Fi shows up in the menu bar within about 30 seconds.

To **update**, run the Kext Installer from the new release.

<details>
<summary><b>Installing by hand</b></summary>

1. Copy `AirPort_RTW89.kext` and the three kexts into `EFI/OC/Kexts`.
2. Delete `IO80211FamilyLegacy.kext/Contents/PlugIns/AirPortBrcmNIC.kext`
   (it is for Broadcom cards).
3. In ProperTree, run **OC Snapshot**, then check the order in
   `Kernel -> Add`. It must be: Lilu, IOSkywalkFamily, IO80211FamilyLegacy,
   AMFIPass, AirPort_RTW89, then its plugin
   `AirPort_RTW89.kext/Contents/PlugIns/AirPortRTW89Front.kext`. Move entries
   if Snapshot put them elsewhere.
4. Add to `Kernel -> Block`: `Identifier` `com.apple.iokit.IOSkywalkFamily`,
   `Strategy` `Exclude`, `Arch` `x86_64`, `MinKernel` `23.0.0`, `Enabled` true.
5. Set `Misc -> Security -> SecureBootModel` to `Disabled`.
6. On macOS Tahoe, add `-amfipassbeta` to `boot-args`.
7. If the same EFI also boots Ventura or older, give the four kexts
   `MinKernel` `23.0.0`.
8. For AirDrop: copy `OwlyAirLinkBridge.kext` out of
   `AirPort_RTW89.kext/Contents/PlugIns` into `EFI/OC/Kexts` and add it right
   after Lilu, and add BroadTek as in [BroadTek](#broadtek).

</details>

## BroadTek

AirDrop from an iPhone and Handoff need the Mac's Bluetooth LE adverts, and
on a non-Apple Bluetooth controller macOS sends them without the Mac's
private address, so the iPhone ignores them.
[BroadTek](https://github.com/FranciumOS-org/BroadTek) (a Lilu plugin, MIT
licence; `BroadTek.kext` is in the release zip next to `AirPort_RTW89.kext`) adds that
address to each advert set, and lets bluetoothd start advertising at all. The
Kext Installer copies it and adds it to `config.plist` with the boot-arg
`-broadtekp2poff` (which keeps it from hooking the same function as the
AirDrop bridge). By hand: copy it to `EFI/OC/Kexts`, add it to
`Kernel -> Add` right after Lilu, and add `-broadtekp2poff` to `boot-args`.

- It works on the HCI stream, not on any one chip, and was tested with the
  RTL8852BE's Bluetooth (RealtekBluetoothFirmware) and BlueToolFixup.
- Its patch for bluetoothd is made for macOS Tahoe 26.7.x; on other versions
  it may not apply, and then iPhones will not list the Mac.
- `-broadtekoff` turns it off; `-broadtekadvaddroff` keeps it loaded without
  changing the adverts.

## Troubleshooting

- **macOS does not start:** boot from your backup USB stick, then remove the
  kexts or restore `config.plist.pre-airport-rtw89`.
- **WPA3 causes trouble:** add the boot-arg `-rtw89nowpa3`.
- **Collect logs for a report:** download the tools zip
  (`AirPort_RTW89-<version>-tools.zip`) and run `sudo tools/collect_logs.sh`.
  It makes one zip with the driver's and macOS's Wi-Fi logs. Passwords are
  never included and MAC addresses are shortened, but network names can
  appear, so check before posting publicly.
- `tools/rtw89ctl status` (from the tools zip) shows the connection, channel and speed.

## Reporting

Open an issue at <https://github.com/FranciumOS-org/Airport_RTW89/issues>
with your card, macOS version and the log zip. [TESTING.md](TESTING.md) lists
what to try.

## Building

On macOS with the Command Line Tools:

```sh
make fetch-firmware   # once: Realtek's firmware, from linux-firmware
make release          # build/release/AirPort_RTW89-<version>.zip and the tools zip
```

The build runs the driver's tests in userspace first (`make hosttest`). To
install a build on this machine: `sudo efi/install.sh` (see
[efi/README.md](efi/README.md)).

| Path | What |
|---|---|
| `src/kext/`, `src/front/` | The kext: PCI device and macOS Wi-Fi interface |
| `src/compat_rtw89/`, `src/compat/` | The Linux parts rtw89 needs, written for macOS (association, WPA2/WPA3, data path) |
| `third_party/` | Linux rtw89, Linux 802.11 headers, hostap and Mbed TLS (for WPA3), unmodified |
| `installer/`, `efi/` | The Kext Installer and the shell installer |
| `tools/` | `rtw89ctl`, the log collector, the tests |

## Licence

GPL-2.0 ([LICENSE](LICENSE)). Vendored code keeps its own licence: Linux rtw89
GPL-2.0 OR BSD-3-Clause, hostap BSD-3-Clause, Mbed TLS Apache-2.0 OR
GPL-2.0-or-later. The kext includes Realtek's firmware, redistributed under
[firmware/LICENCE.rtlwifi_firmware.txt](firmware/LICENCE.rtlwifi_firmware.txt).
See [CREDITS.md](CREDITS.md).
