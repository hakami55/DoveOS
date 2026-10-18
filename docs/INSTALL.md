# Install and boot DoveOS

## Requirements

- HP Elite x3 Lap Dock with compatible existing firmware.
- A healthy spare USB drive of **32 GB or larger**. The extracted disk image is approximately 16 GiB (17,180,917,760 bytes).
- A computer with an image writer and enough free space to extract the download.
- Power for the Lap Dock and a supported local network for wireless features.

## Prepare the USB

1. Download `DoveOS-current-fresh-20261018.img.zip`, `SHA256SUMS` and this guide from the same release.
2. Verify the ZIP's SHA-256 against `SHA256SUMS`.
3. Extract the ZIP to obtain `DoveOS-current-fresh-20261018.img`. Its SHA-256 is also listed in `SHA256SUMS`.
4. Open [Rufus](https://rufus.ie/) or [balenaEtcher](https://www.balena.io/etcher). You need only one image writer.
5. Select the extracted `.img` file and the correct spare USB. Use raw/DD mode if asked. **Writing erases all data on that USB.**
6. Wait for writing and verification to finish, then safely eject the USB.

Do not copy the image file into an ordinary USB folder. Keep your working DoveOS drive until the new one boots and your required apps are checked.

## Boot the Lap Dock

1. Leave the left USB-C port empty.
2. Connect the prepared USB to a right USB-C port, directly or through a compatible hub.
3. Connect power and start the dock. Wait for the desktop.
4. Open Settings (Control Panel) to configure your network, sound and preferences.
5. Pair your own streaming sources in the relevant app.

This image is for the supported Lap Dock, not a PC. The dock's existing firmware launches the USB environment; there is no PC-style BIOS installation or firmware replacement step. The optional startup logo requires the matching splash hook already installed in the dock. The USB image alone does not install that internal hook.

## Included apps

Files, Terminal, Settings, Moonlight, DoveDeX, DoveAir, DoveCast, DovePlayer, Local Transfer, HDMI In and Controller Tester are included. DoveCast currently supports YouTube only. HDMI In requires a supported external USB capture card.

## Everyday use

- Keep the system USB connected. Save work before restarting or safely unmounting it.
- `Ctrl+Alt+Shift+Q` returns from supported fullscreen streaming sessions.
- Prefer a supported 5 GHz network for wireless streaming.
- The 3.5 mm jack automatically routes sound to connected headphones.
- Settings > System shows the active renderer, desktop resolution, memory, storage and network addresses.

## Status and troubleshooting

This is a release candidate. The updated UI passed checks on the original dock, and the new USB passed its file scan. A physical boot check of this updated spare remains pending.

If startup fails, check power, leave the left port empty, verify the image and try a healthy USB written again from the verified image. Preserve the working installation.

See [hardware compatibility](https://github.com/hakami55/DoveOS/blob/main/docs/COMPATIBILITY.md) and [application support](https://github.com/hakami55/DoveOS/blob/main/docs/APPS.md) for scope and limits. Internal USB 3.0 support and using the left port as an ordinary USB host remain unresolved.
