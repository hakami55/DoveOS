# Hardware compatibility

Status describes the tested DoveOS function, not every capability of a chip.

| Hardware | Status | Scope |
| --- | --- | --- |
| CPU | Working | Original ARMv7 dual-core processor and NEON runtime. |
| RAM | Working | About 253 MiB reported to Linux in the tested boot configuration; System displays the live available estimate and swap. |
| GPU | Working | Tested Mali OpenGL ES 2.0 path and 720p desktop. |
| Hardware video decoding | Working | H.264 and HEVC in supported streaming paths. |
| Built-in screen | Working | Desktop and video display. |
| Keyboard | Working | Normal typing; some controller-managed keys have fixed behaviour. |
| Touchpad | Working | Pointer, tapping, clicks, dragging and scrolling. |
| Speakers and 3.5 mm audio | Working | Speaker/headphone output and headset microphone; automatic jack routing. |
| Wi-Fi | Partial | Supported WPA2 2.4/5 GHz networks work; channel, security and recovery limits remain. |
| Bluetooth | Partial | Tested audio and basic keyboard/mouse paths; other profiles vary. |
| Battery and charging | Working | Percentage, external power, charge and discharge readings. |
| Power and lid | Partial | Screen dim/off/wake works in tested paths; full power-off/wake remains unresolved. |
| Right USB-C data | Partial | USB 2.0 path works; internal USB 3.0 speed is unresolved. |
| Left USB-C | Partial | Original external-source attachment path; ordinary flash-drive use in DoveOS is unsupported. |
| Micro-HDMI output | Working | User-confirmed output audio; supported display path depends on source mode. |
| HDMI input / USB capture | Card-dependent | Separate external USB capture-card app; 1080p60 remains experimental. |

Native desktop resolution is 720p. A 1080p desktop is outside the chosen release
scope; this does not limit the separate supported 1080p video paths.
Hardware limitations and excluded experiments do not reduce an application's
release checklist. No universal compatibility or artificial completion score is claimed.
