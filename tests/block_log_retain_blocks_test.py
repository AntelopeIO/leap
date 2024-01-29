#!/usr/bin/env python3

import random
import os
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# block_log_retain_blocks_test
#
# A basic test for --block-log-retain-blocks option. It validates
#   * no blocks.log is generated when the option is set to 0
#   * blocks.log is generated when the option is set to greater than 0
#   * blocks.log is generated when the option is not present.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"--keep-logs","--activate-if","--dump-error-details","-v","--leave-running","--unshared"})
debug=args.v
dumpErrorDetails=args.dump_error_details
activateIF=args.activate_if

seed=1
Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

# the first  node for --block-log-retain-blocks 0,
# the second for --block-log-retain-blocks 10,
# the third for -block-log-retain-blocks not configured
pnodes=1
total_nodes=pnodes + 2

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[0]=f' --block-log-retain-blocks 0 '
    specificExtraNodeosArgs[1]=f' --block-log-retain-blocks 10 '

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, activateIF=activateIF, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")
    Print ("Cluster stabilized")

    # node 0 started with --block-log-retain-blocks 0. no blocks.log should
    # be generated
    blocksLog0=os.path.join(Utils.getNodeDataDir(0), "blocks", "blocks.log")
    if os.path.exists(blocksLog0):
       errorExit(f'{blocksLog0} not expected to exist. Test failed')
    Print ("Verified no blocks.log existed for --block-log-retain-blocks 0");

    # node 1 started with --block-log-retain-blocks 10. blocks.log should
    # be generated
    blocksLog1=os.path.join(Utils.getNodeDataDir(1), "blocks", "blocks.log")
    if not os.path.exists(blocksLog1):
       errorExit(f'{blocksLog1} expected to exist. Test failed')
    Print ("Verified blocks.log existed for --block-log-retain-blocks 10");

    # node 2 started without --block-log-retain-blocks. blocks.log should
    # be generated
    blocksLog2=os.path.join(Utils.getNodeDataDir(2), "blocks", "blocks.log")
    if not os.path.exists(blocksLog2):
       errorExit(f'{blocksLog2} expected to exist. Test failed')
    Print ("Verified blocks.log existed for no --block-log-retain-blocks configured");

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
