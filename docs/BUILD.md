# Source and build

The repository preserves the DoveOS source layout used by the build scripts.
Applications target ARMv7 hard-float; desktop tools use GTK2 and X11. The
existing Lap Dock kernel and vendor display/media interfaces remain required.

- `doveos_v2/`: desktop, settings, SDK, graphics, input, power and applications.
- `moonlight_dock_prototype/`: the adapted streaming client and upstream libraries.
- `airplay_receiver_study/`: the receiver frontend, hardware bridge and upstream receiver.
- `dovedeX_v01/`: Android desktop integration source.
- `vendor-interfaces/`: interface headers used by the hardware bridge.

The current scripts document the original Linux/WSL build environment. Some
paths still refer to that environment and require adjustment. Source upload
does not establish an independently reproducible complete USB build yet.

`sh tools/build-frontends.sh OUTPUT_DIRECTORY` builds the core frontends using
the configured ARMHF compiler, GTK2 sysroot and desktop runtime. Override
`DOVEOS_CC`, `DOVEOS_SDK_SYSROOT` and `DOVEOS_RUNTIME` for your environment.
`tools/build-usb-image.sh` assembles an already prepared USB tree into a new
image file; it does not format a physical drive.

The base environment is built with Buildroot. The GUI uses the recorded ARMHF
runtime packages. See `doveos_v2/build.sh`, `fetch_desktop.py`,
`sdk/build-app.sh` and each application build script.

Vendor drivers must come from compatible existing firmware or an authorized
distribution. Internal firmware dumps and signing/debug keys are not included.
