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

The current build for RTL8822CE is **2.0.2d2-audit1**. It pairs AirPort_RTW88
(bundle version 2.0.2d2) with OwlyAirLinkBridge (bundle version 0.1.0).
The release suffix identifies the exact pair. Keep both kexts together.

This build corrects receive-buffer bounds, hardware key-slot bounds and firmware
scan-result ordering. It also reports current native client sessions and separates
AWDL queue expiry from invalid entries and unreadable packets. The RTL8822CE
uses the software scanner for ordinary Wi-Fi. Hardware testing of this revision
is pending. The previous release passed file transfers in both directions between
a Mac and an iPhone. [Hardware-tested release](https://github.com/Skensy/AirPort_RTW89/releases/tag/rtl8822ce-2.0.2d2)

The upstream source archive is included verbatim in `third_party/rtl8822ce`
with its original notices. This repository applies its Tahoe patches during
the build. [Upstream source snapshot](https://github.com/xnoah222/Realtek-AirPort-Family/tree/6475f144f9620d1679a788f0f49052d8f06b944a)

On macOS with the Xcode x86_64 toolchain, run `make rtl8822ce` or `make`.
It prepares the source, runs the radio and bridge regression tests, builds
both kexts, checks their pinned executable hashes and verifies the bridge
binding. Use `make rtl8822ce-source` to inspect the patched source in
`build/rtl8822ce-2.0.2d2-audit1/source/AirPort_RTW88`.

| Build output under `build/rtl8822ce-2.0.2d2-audit1/` | SHA-256 of the Mach-O executable |
| --- | --- |
| `AirPort_RTW88-2.0.2d2-audit1.zip` | `9e15891f0fe618f6d3d9769d0a4b06f73bd4f0c42c5939ea48c121fe7b7cf16c` |
| `OwlyAirLinkBridge-rtl8822ce-2.0.2d2-audit1.zip` | `b49bae6487d8154b43fccf342b85ed50fac70d77b519aca0af70ee83bffbf6b7` |

The working OpenCore configuration loads the bridge after Lilu and the driver
after IO80211FamilyLegacy. The corresponding EFI directory names are
`OwlyAirLinkBridge-RTW88-multicast.kext` and `AirPort_RTW88-AWDL-multicast.kext`.
The RTW89 installation instructions below apply to a separate family of cards.

#### Station Wi-Fi with the 2.0.2d2 multicast build

On 23 November 2026, one RTL8822CE running Tahoe 26.7.1 (25G241) and
`2.0.2d2-awdl-multicast` produced the following results on three networks.

| Network | Observed result |
| --- | --- |
| iliadbox, 5 GHz, channel 44, WPA2 Personal | After startup, automatic association completed in 3.6 s and IPv4 arrived after 12.6 s. The gateway answered four pings and HTTPS succeeded over Wi-Fi. |
| iPhone Personal Hotspot, 2.4 GHz, channel 6 | DHCP and an HTTPS request bound to the Wi-Fi interface succeeded. |
| TIM, multiple access points | The link came up for about four seconds and dropped before IPv4. Attempts on both bands failed to sustain a connection. Using the hardware MAC address also reproduced the drop. |

The driver executable was the same for all three tests. The access point,
channel and boot session varied across trials, so the cause of the TIM
disconnect remains open. These checks exercised ordinary Wi-Fi. Wireless
Continuity Camera remains unverified on this build.

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

With these personalities, Tahoe 26.7.1 reported a powered RTL8822CU as
`THIRD_PARTY_DONGLE` with a valid address. For an AirDrop trial using the
standalone BroadTek 0.2.0 release, enable it after Lilu and add its
plugin-specific `-broadtekbeta` boot argument. The release declares Darwin
22 through 24 in its [Lilu configuration](https://github.com/FranciumOS-org/BroadTek/blob/2654ad368f39e833b03fd521b877a116dd015e00/src/plugin/kern_start.cpp#L11-L30).
Tahoe uses Darwin 25. After reboot, check `broadtek-patcher-ran` and
`broadtek-hooks` in IORegistry, then test a transfer in each direction.

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
