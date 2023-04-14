#!/usr/bin/env python3

import random

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# terminate-scenarios-test
#
# Tests terminate scenarios for nodeos.  Uses "-c" flag to indicate "replay" (--replay-blockchain), "resync"
# (--delete-all-blocks), "hardReplay"(--hard-replay-blockchain), and "none" to indicate what kind of restart flag should
# be used. This is one of the only test that actually verify that nodeos terminates with a good exit status.
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-d","-s","-c","--kill-sig","--keep-logs"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--terminate-at-block","--unshared"})
pnodes=1
topo=args.s
delay=args.d
chainSyncStrategyStr=args.c
debug=args.v
total_nodes = pnodes
killSignal=args.kill_sig
dumpErrorDetails=args.dump_error_details
terminate=args.terminate_at_block

seed=1
Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    cluster.setChainStrategy(chainSyncStrategyStr)
    cluster.setWalletMgr(walletMgr)

    Print ("producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d, chain sync strategy: %s" % (
    pnodes, topo, delay, chainSyncStrategyStr))

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    Print("Kill cluster node instance.")
    if cluster.killSomeEosInstances(1, killSignal) is False:
        errorExit("Failed to kill Eos instances")
    assert not cluster.getNode(0).verifyAlive()
    Print("nodeos instances killed.")

    Print ("Relaunch dead cluster node instance.")
    nodeArg = "--terminate-at-block %d" % terminate if terminate > 0 else ""
    if nodeArg != "":
        if chainSyncStrategyStr == "hardReplay":
            nodeArg += " --truncate-at-block %d" % terminate
    if cluster.relaunchEosInstances(cachePopen=True, nodeArgs=nodeArg, waitForTerm=(terminate > 0)) is False:
        errorExit("Failed to relaunch Eos instance")
    Print("nodeos instance relaunched.")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
