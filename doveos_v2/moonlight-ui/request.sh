#!/bin/sh
exec chroot /proc/1/root /bin/sh /tmp/doveos-moonlight-ui/request-host.sh "$@"
