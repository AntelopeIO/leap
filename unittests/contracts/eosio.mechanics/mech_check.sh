#!/bin/sh
#
# EOS Mechanics check script. Schedule this to run every minute or so to make sure the loop script is up.
#

SCRIPT_DIR="/path/to/mech/scripts"

pgrep mech_loop >/dev/null 2>&1 || $SCRIPT_DIR/mech_loop.sh &

