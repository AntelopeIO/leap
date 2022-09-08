#!/usr/bin/env python3

import os
import sys

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
import log_reader

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs"})

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = max(2, pnodes if args.n < pnodes else args.n)
Utils.Debug = args.v
killAll=args.clean_run
dumpErrorDetails=args.dump_error_details
dontKill=args.leave_running
killEosInstances = not dontKill
killWallet=not dontKill
keepLogs=args.keep_logs

testSuccessful = False
# try:
data = log_reader.chainData()
log_reader.scrapeLog(data, "tests/performance_tests/sample_nodeos_log.txt")
expected = log_reader.chainData()
expected.startBlock = 0
expected.ceaseBlock = 0
expected.totalTransactions = 0
expected.totalNet = 0
expected.totalCpu = 0
expected.totalElapsed = 0
expected.totalTime = 0
expected.totalLatency = 0
assert data == expected, f"Error: Actual log:\n{data}\ndid not match expected log:\n{expected}"
data.assertEquality(expected)
testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)

