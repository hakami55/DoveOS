# Install and boot

## Requirements

- HP Elite x3 Lap Dock with compatible existing firmware.
- A healthy spare USB drive of at least **4 GB**. The disk image is 2 GiB.
- A computer with a USB image writer and the downloaded DoveOS `.img` file.
- Power for the Lap Dock. Use a supported local network for wireless features.

## Prepare the USB

1. Download the image and `SHA256SUMS` from the same GitHub release.
2. Extract the compressed download to obtain the `.img` file.
3. Compare its SHA-256 with the published checksum before writing.
4. Open [Rufus](https://rufus.ie/) or [balenaEtcher](https://etcher.balena.io/).
5. Select the DoveOS `.img` and the correct spare USB drive. Start writing.
   Use raw/DD writing if the writer asks. **All data on that USB is erased.**
6. Wait for completion and verification, then safely eject the USB.

You need only one writer. Do not copy the `.img` into an ordinary USB folder.
The release will identify which writer was actually used for acceptance.

## Boot the Lap Dock

1. Keep the left USB-C port empty.
2. Connect the prepared USB to a right USB-C port, directly or through a compatible hub.
3. Connect power and start the Lap Dock. Wait for the DoveOS desktop.
4. Open Control Panel to set up your network, sound and preferences.
5. Pair your own streaming sources inside the relevant application.

The dock's existing firmware launches the USB environment. There is no PC-style
BIOS boot menu or firmware replacement step in this installation.
The optional startup logo requires the matching splash hook already installed
in the dock; the USB image alone does not install that internal hook.

## Everyday use

- Keep the boot USB attached. Save documents before restarting or ejecting.
- `Ctrl+Alt+Shift+Q` exits supported full-screen streaming sessions.
- Use a supported 5 GHz network for wireless streaming when available.
- A connected headphone jack automatically routes audio to the headphones.

## If startup fails

Check power, USB placement and image verification. Disconnect left-port sources.
Try a healthy USB drive written again from the verified image. Preserve your
working USB before testing a new release. Recovery and final image acceptance
are still being checked; do not overwrite your only working installation.
