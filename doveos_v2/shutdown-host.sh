#!/bin/sh
# Power-off disabled after the physical wake test failed on stock firmware.
# This guard also protects a still-running supervisor from the earlier trial.
case "${1:---plan}" in
 --plan) echo SHUTDOWN_DISABLED_FIRMWARE_WAKE_TEST_FAILED;exit 0;;
 --execute) echo SHUTDOWN_DISABLED_NO_SERVICES_STOPPED;exit 3;;
 *) exit 2;;
esac
