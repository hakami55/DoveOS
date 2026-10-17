# Startup artwork

The verified USB artwork is used by the existing optional early-start splash
hook. Keep `trial.sha256`: its historical name is part of that interface.
Without this manifest the hook deliberately uses the original connection screen.
The desktop can still boot through USB autorun.

The hook and foreground helper depend on the matching existing HP firmware.
This directory provides the original helper source and USB artwork; inserting
the USB does not install or replace internal firmware. Standard firmware without
that optional hook may show its original connection screen before DoveOS starts.
