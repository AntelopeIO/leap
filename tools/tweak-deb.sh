#!/bin/bash
set -euo pipefail

# Tweaks a couple aspects of the built .deb's control file:
# 1. Removes Installed-Size field; this isn't reproducible for some reason, possibly different filesystems
#    reporting different sizes for directories?
# 2. Removes all but the first libc Depends rule as the rest are unnecessarily restrictive. The original was being
#    generated as,
#       libc6 (>= 2.27), libc6 (>> 2.28), libc6 (<< 2.29), libcurl4 (>= 7.16.2), libgcc1 (>= 1:3.3), libgmp10, zlib1g (>= 1:1.2.0)
#    and the included sed rule within this script will reduce it to
#       libc6 (>= 2.27), libcurl4 (>= 7.16.2), libgcc1 (>= 1:3.3), libgmp10, zlib1g (>= 1:1.2.0)
#    This may need to be tweaked in the future further; clearly not ideal.

WORKDIR="$(mktemp -d)"
trap 'rm -rf -- "${WORKDIR}"' EXIT

if [ $# -lt 1 ]; then
   echo "Must specify .deb file to tweak as argument to script"
   exit 1
fi

if [ ! -f "$1" ]; then
   echo "Argument passed is not a file"
   exit 1
fi

DEB_PATH="$(realpath ${1})"
cd "${WORKDIR}"

ar x "${DEB_PATH}" control.tar.gz
gzip -d control.tar.gz
tar xf control.tar ./control
tar --delete -f control.tar ./control
sed -i -E -e '/Installed-Size/d' -e 's/, libc6[^,]+//g' control
tar --update --mtime "@0" --owner=0 --group=0 --numeric-owner -f control.tar ./control
gzip -n control.tar
ar rD "${DEB_PATH}" control.tar.gz
