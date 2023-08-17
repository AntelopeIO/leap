#!/usr/bin/env python3

import errno
import pathlib
import shutil
import signal
import socket
import time

from TestHarness import Node, TestHelper, Utils

###############################################################
# p2p_no_listen_test
#
# Test nodeos disabling p2p
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"--keep-logs","-v","--leave-running","--unshared"})
debug=args.v

Utils.Debug=debug
testSuccessful=False

try:
    TestHelper.printSystemInfo("BEGIN")

    cmd = [
        Utils.EosServerPath,
        '-e',
        '-p',
        'eosio',
        '--p2p-listen-endpoint',
        '',
        '--plugin',
        'eosio::chain_api_plugin',
        '--config-dir',
        Utils.ConfigDir,
        '--data-dir',
        Utils.DataDir,
        '--http-server-address',
        'localhost:8888'
    ]
    node = Node('localhost', '8888', '00', data_dir=pathlib.Path(Utils.DataDir),
                config_dir=pathlib.Path(Utils.ConfigDir), cmd=cmd)

    time.sleep(1)
    if not node.verifyAlive():
        raise RuntimeError
    time.sleep(10)
    node.waitForBlock(5)

    s = socket.socket()
    err = s.connect_ex(('localhost',9876))
    assert err == errno.ECONNREFUSED, 'Connection to port 9876 must be refused'

    testSuccessful=True
finally:
    Utils.ShuttingDown=True

    if not args.leave_running:
        node.kill(signal.SIGTERM)

    if not (args.leave_running or args.keep_logs or not testSuccessful):
        shutil.rmtree(Utils.DataPath, ignore_errors=True)

    if testSuccessful:
        Utils.Print("Test succeeded.")
    else:
        Utils.Print("Test failed.")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
