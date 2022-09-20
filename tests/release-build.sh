#!/bin/bash
# test name and purpose
echo ''
echo '                         ##### Release Build Test #####'
echo ''
echo '    The purpose of this test is to ensure that nodeos was built with compiler'
echo 'optimizations enabled. While there is no way to programmatically determine that'
echo 'given one binary, we do set a debug flag in nodeos when it is built with'
echo 'asserts. This test checks that debug flag. Anyone intending to build and install'
echo 'nodeos from source should perform a "release build" which excludes asserts and'
echo 'debugging symbols, and performs compiler optimizations.'
echo ''

TDIR=$(mktemp -d || exit 1)
NODEOS_DEBUG=$(programs/nodeos/nodeos --config-dir "${TDIR}" --data-dir "${TDIR}" --extract-build-info >(python3 -c 'import json,sys; print(str(json.load(sys.stdin)["debug"]).lower());') &> /dev/null)
#avoiding an rm -rf out of paranoia, but with the tradeoff this could change somehow in the future
rm "${TDIR}/config.ini" || exit 1
rmdir "${TDIR}" || exit 1
if [[ "${NODEOS_DEBUG}" == 'false' ]]; then
    echo 'PASS: Debug flag is not set.'
    echo ''
    exit 0
fi

echo 'FAIL: Debug flag is set!'
exit 3