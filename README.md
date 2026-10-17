# DoveOS

A compact desktop and streaming environment for the **HP Elite x3 Lap Dock**.
DoveOS runs from a USB drive using the dock's existing firmware. It provides a
720p desktop, settings, files, local media playback and receiver applications.

**Release status: preparing the USB release candidate.** The cleaned image
must pass a fresh-drive boot check before a final release is published.

[Install and boot](docs/INSTALL.md) · [Hardware compatibility](docs/COMPATIBILITY.md) ·
[Applications](docs/APPS.md) · [Source and build](docs/BUILD.md) · [Downloads](https://github.com/hakami55/DoveOS/releases)

## Get started

1. Download the USB image and its SHA-256 checksum from Releases when available.
2. Write the image to a spare USB drive with Rufus or balenaEtcher.
3. Insert it into a **right USB-C port**, leave the left port empty, and start the dock.
4. Set up your own network and pair your streaming sources in DoveOS.

Writing an image erases the selected USB drive. Keep the DoveOS drive connected
while using the desktop. This image is for the supported Lap Dock; it is not a
PC installer. Existing compatible HP firmware is required.

## Included

- DoveOS desktop, Files, Control Panel, Terminal, Notes and Calculator.
- Moonlight for PC streaming.
- DoveDeX for supported Android desktop or screen streaming over USB or Wi-Fi.
- DoveAir for iPhone screen mirroring and sound.
- DovePlayer for local media.
- DoveOS SDK for native applications.

See the compatibility and application tables for the tested scope and limits.
Experimental projects are outside this release.

## Source

This repository contains DoveOS code and the relevant upstream receiver sources.
Release images are separate downloads. Personal settings, device pairings,
recordings, firmware dumps and unrelated projects are excluded.
Third-party components retain their original licenses; see [Notices](NOTICE.md).
