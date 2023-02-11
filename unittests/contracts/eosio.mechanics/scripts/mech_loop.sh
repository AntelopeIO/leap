#!/bin/sh
#
# EOS Mechanics loop script. Loop and run the actions with a random sleep.
#

SCRIPT_DIR="/path/to/mech/scripts"
ACTIONS_LOG="/path/to/actions.log"

while :; do
    $SCRIPT_DIR/mech_actions.sh >>$ACTIONS_LOG 2>&1
    sleep $(shuf -i 12-18 -n 1)
done
