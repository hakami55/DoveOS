# Applications

| Application | Function | Current scope |
| --- | --- | --- |
| Desktop and Files | Launch apps and manage USB documents. | Working; manual fresh-USB boot and 720p GPU desktop confirmed. |
| Settings | Network, Bluetooth, brightness, sound tests, controls, time, power and keyboard preferences. | Working within the hardware compatibility table; System is the last category. |
| Moonlight | PC streaming with controls and sound. | Tested H.264 and supported HEVC hosts; a compatible host is required. |
| DoveDeX | Supported Android desktop or screen streaming with controls and sound. | Tested USB and Wi-Fi; USB-debug authorization and source capabilities apply. |
| DoveAir | iPhone screen mirroring with sound and rotation. | Tested; some source-video quality cases still need improvement. |
| DoveCast | YouTube receiver with TV-code pairing. | YouTube only; other casting apps and general screen mirroring are not supported. |
| DovePlayer | Local video and audio. | Supported formats only; current local-video UI targets 16:9 H.264 at up to 720p60. |
| Local Transfer | Browser-based upload/download of a shared folder. | Devices use the same local network; closing the app stops sharing. |
| HDMI In | View a supported external USB HDMI capture card. | Card-dependent; 1080p60 remains experimental. The dock's micro-HDMI connector is an output. |
| Controller Tester | Check sticks, triggers, buttons and optional vibration. | Depends on the controller's exposed input and rumble capabilities. |
| Notepad and Calculator | Lightweight desktop tools. | Working in the tested configuration. |
| Terminal | Local commands. | Working. |
| DoveOS SDK | Native app development. | Source and templates included; development tools are separate from everyday setup. |

## Connect and use

- **Moonlight:** use a supported local network, find/add the PC, pair it, load its apps and start streaming.
- **DoveDeX:** authorize USB debugging; use USB directly or set up/check Wi-Fi from USB before disconnecting the phone cable.
- **DoveAir:** start the receiver on the same network, then select DoveAir from the iPhone's Screen Mirroring list.
- **DoveCast:** start the receiver, then use YouTube's TV-code linking with the displayed code.
- **DovePlayer:** select a supported local file.
- **Local Transfer:** use its QR code or browser link; only the selected shared folder is exposed.

Use your own networks and pairings. App availability does not imply universal
codec, device, radio, capture-card or firmware compatibility.
