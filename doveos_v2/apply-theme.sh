#!/bin/sh
# DOVEOS_BOOT_TIMING_V1: uptime survives calendar-clock corrections.
boot_trace(){
 read dove_boot_up dove_boot_idle < /proc/uptime
 printf '%s %s\n' "$dove_boot_up" "$*" >> /media/usb0/DoveOS-v2/boot-timing.log 2>/dev/null || :
}
# USB theme assets and RAM-only binds; stock firmware is untouched.
set -e
B=/media/usb0/DoveOS-v2
M=/tmp/doveos-v2-root
T=/tmp/doveos-theme
test -f "$B/classic-theme.tar.gz"
mkdir -p "$T"
# A prior live sound-helper bind must be released before tar replaces its inode.
if awk -v target="$M/usr/bin/doveos-settings-proxy" '$2==target{found=1}END{exit !found}' /proc/mounts;then
 umount "$M/usr/bin/doveos-settings-proxy"
fi
boot_trace theme_extract_begin
if [ -r "$B/Memory/static-runtime.sh" ];then /bin/sh "$B/Memory/static-runtime.sh" unseal theme;fi
gunzip -c "$B/classic-theme.tar.gz" | tar -xf - -C "$T"
boot_trace theme_extract_end
if [ -r "$B/Memory/static-runtime.sh" ];then
 /bin/sh "$B/Memory/static-runtime.sh" stage theme || echo THEME_STATIC_RAM_FALLBACK
fi
if [ -r "$B/Cursors/install-theme.sh" ];then
 /bin/sh "$B/Cursors/install-theme.sh"
fi
test -f "$T/jwmrc"
test -f "$T/doveos-status"
mkdir -p "$T/live-etc" "$B/home/.icons/DoveClassic"
mkdir -p "$B/home/.local/share/fonts"
boot_trace theme_fonts_begin
cp "$T/fonts/dove-ui.ttf" "$T/fonts/dove-sans.ttf" "$T/fonts/dove-sans-bold.ttf" "$B/home/.local/share/fonts/"
boot_trace theme_fonts_end
# Fonts are installed on USB; keeping their installation copies in tmpfs
# adds 1.7 MiB to every desktop session without helping font rendering.
for name in dove-ui.ttf dove-sans.ttf dove-sans-bold.ttf;do
 if cmp -s "$T/fonts/$name" "$B/home/.local/share/fonts/$name";then rm -f "$T/fonts/$name";fi
done
cp -a "$M/etc/doveos/." "$T/live-etc/"
cp "$T/jwmrc" "$T/gtkrc" "$T/dove-start.png" "$T/start-sidebar.png" "$T/desktop-items-0.conf" "$T/live-etc/"
cp -a "$T/classic-icons" "$T/live-etc/"
boot_trace theme_usb_icons_begin
# Refresh the persistent icon cache only when the source theme changes.
icon_signature=$(sha256sum "$B/classic-theme.tar.gz")
icon_signature=${icon_signature%% *}
icon_stamp="$B/home/.icons/DoveClassic/.doveos-theme-sha256"
if [ "$(cat "$icon_stamp" 2>/dev/null || :)" != "$icon_signature" ] || [ ! -r "$B/home/.icons/DoveClassic/48/folder.png" ];then
 cp -R "$T/classic-icons/." "$B/home/.icons/DoveClassic/"
 printf '%s\n' "$icon_signature" > "$icon_stamp"
else
 boot_trace theme_usb_icons_cached
fi
if [ -r "$B/AirPlay/install-shortcuts.sh" ];then
 /bin/sh "$B/AirPlay/install-shortcuts.sh" || echo AIRPLAY_SHORTCUT_UNAVAILABLE
fi
if [ -r "$B/Capture/install-shortcuts.sh" ];then
 /bin/sh "$B/Capture/install-shortcuts.sh" || echo CAPTURE_SHORTCUT_UNAVAILABLE
fi
boot_trace theme_usb_icons_end
# Preserve existing launch commands; replace only selected icon fields.
for file in "$T/live-etc"/*.desktop;do
 test -f "$file" || continue
 icon=$(sed -n 's/^Icon=//p' "$file" | head -1)
 case "$icon" in
 system-file-manager) icon=folder;;
 accessories-text-editor|utilities-terminal|applications-games|preferences-system|video-display) ;;
 *) continue;;
 esac
 size=48
 test -f "$T/classic-icons/$size/$icon.png" || size=24
 test -f "$T/classic-icons/$size/$icon.png" || continue
 sed -i "s|^Icon=.*|Icon=/etc/doveos/classic-icons/$size/$icon.png|" "$file"
done
for file in "$T/live-etc"/*.desktop;do
 test -f "$file" || continue
 if grep -q '^Exec=/usr/bin/doveos-settings-ui' "$file";then
  sed -i 's/^Name=.*/Name=Control Panel/' "$file"
  sed -i 's|^Icon=.*|Icon=/etc/doveos/classic-icons/48/preferences-system.png|' "$file"
 fi
done
mkdir -p "$B/SDK"
boot_trace theme_usb_sdk_begin
cp -R "$T/sdk/." "$B/SDK/"
boot_trace theme_usb_sdk_end
printf '[Desktop Entry]\nType=Application\nName=SDK 2 Demo\nIcon=/media/usb0/DoveOS-v2/ClassicApps/icons/sdk-48.png\nExec=/tmp/doveos-theme/doveos-open doveos-sdk-v2 /tmp/doveos-theme/doveos-sdk-v2\nTerminal=false\n' > "$T/live-etc/SDK-Demo.desktop"
# Reuse app windows rather than creating a second desktop instance.
for file in "$T/live-etc"/*.desktop;do
 test -f "$file" || continue
 case "$(sed -n 's/^Exec=//p' "$file" | head -1)" in
  pcmanfm\ *) id=pcmanfm;;
  xedit\ *) id=xedit;;
  xterm*) id=xterm;;
  /usr/bin/doveos-settings-ui*) id=DoveSettings;;
  /tmp/doveos-theme/doveos-sdk-demo*) id=doveos-sdk-demo;;
  /usr/bin/doveos-moonlight*) id=doveos-control-x;;
  *) continue;;
 esac
 sed -i "s|^Exec=|Exec=/tmp/doveos-theme/doveos-open $id |" "$file"
done
if [ -r "$B/Moonlight/install-ui.sh" ];then
 /bin/sh "$B/Moonlight/install-ui.sh"
fi
for path in etc/doveos usr/bin/jwm usr/bin/doveos-status usr/bin/doveos-settings-ui usr/bin/doveos-settings-proxy usr/bin/doveos-session;do
 case "$path" in
 etc/doveos) source=$T/live-etc;;
 *) source=$T/${path##*/};;
 esac
 test -e "$M/$path"
 mount --bind "$source" "$M/$path"
done
boot_trace clock_restore_begin
/bin/sh "$T/time-start.sh"
boot_trace clock_restore_end
echo DOVEOS_CLASSIC_THEME_APPLIED

# Install the local browser, document and PDF shortcuts on every boot.
if [ -d "$T/light-desktops" ];then
 cp "$T/light-desktops/"*.desktop "$T/live-etc/"
 mkdir -p "$B/home/.local/share/applications"
 cp "$T/light-desktops/"*.desktop "$B/home/.local/share/applications/"
 for pair in application/pdf:Dove-pdf application/x-abiword:Dove-word application/rtf:Dove-word text/rtf:Dove-word application/msword:Dove-word application/vnd.oasis.opendocument.text:Dove-word text/html:Dove-browser x-scheme-handler/http:Dove-browser x-scheme-handler/https:Dove-browser;do
  mime=${pair%:*};app=${pair#*:}
  if ! grep -q "^$mime=" "$T/live-etc/mimeapps.list";then sed -i "/^\[Default Applications\]/a $mime=$app.desktop" "$T/live-etc/mimeapps.list";fi
 done
fi

# Requested app icons are applied after all shortcut packages.
if [ -r "$B/AppIcons/install.sh" ];then
 /bin/sh "$B/AppIcons/install.sh" || echo APP_ICONS_UNAVAILABLE
fi

# Native SDK Explorer shortcuts and file launchers.
if [ -r "$B/Explorer/install-shortcuts.sh" ];then
 /bin/sh "$B/Explorer/install-shortcuts.sh" || echo EXPLORER_SHORTCUT_UNAVAILABLE
fi

# Local browser file transfer shortcut.
if [ -r "$B/Transfer/install-shortcuts.sh" ];then
 /bin/sh "$B/Transfer/install-shortcuts.sh" || echo TRANSFER_SHORTCUT_UNAVAILABLE
fi

# DOS-style terminal launcher and keyboard shortcut.
if [ -r "$B/Terminal/install-shortcuts.sh" ];then
 /bin/sh "$B/Terminal/install-shortcuts.sh" || echo TERMINAL_SHORTCUT_UNAVAILABLE
fi

# DovePlayer local hardware playback preview.
if [ -r "$B/DovePlayer/install.sh" ];then
 /bin/sh "$B/DovePlayer/install.sh" || echo DOVEPLAYER_SHORTCUT_UNAVAILABLE
fi

# Keep the receiver apps on the desktop; the other installed tools remain in Start.
if [ -r "$B/DesktopLayout/apply.sh" ];then
 /bin/sh "$B/DesktopLayout/apply.sh" || echo DOVEOS_DESKTOP_LAYOUT_UNAVAILABLE
fi

# Classic SDK apps and complete menu icon paths, after every shortcut installer.
if [ -r "$B/ClassicApps/install.sh" ];then
 /bin/sh "$B/ClassicApps/install.sh" || echo CLASSIC_APPS_UNAVAILABLE
fi

# Optional modern browser launcher.
if [ -r "$B/ModernBrowser/install.sh" ];then
 /bin/sh "$B/ModernBrowser/install.sh" || echo DOVEBROWSER_SHORTCUT_UNAVAILABLE
fi
