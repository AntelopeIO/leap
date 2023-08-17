#!/usr/bin/env python3

import random
import signal
import time

from TestHarness import Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType

###############################################################
# large-lib-test
#
# Test LIB in a network will advance when an invalid larger LIB 
# than current one is received from a speculative node.
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"--kill-sig","--kill-count","--keep-logs"
                            ,"--dump-error-details","-v","--leave-running","--unshared"
                            })
pnodes=1
total_nodes=3 # first one is producer, and last two are speculative nodes
debug=args.v
dumpErrorDetails=args.dump_error_details
relaunchTimeout=10
# Don't want to set too big, trying to reduce test time, but needs to be large enough for test to finish before
# restart re-creates this many blocks.
numBlocksToProduceBeforeRelaunch=80
numBlocksToWaitBeforeChecking=20

Utils.Debug=debug
testSuccessful=False

seed=1
random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)
cluster.setWalletMgr(walletMgr)

def relaunchNode(node: Node, chainArg="", skipGenesis=True, relaunchAssertMessage="Fail to relaunch"):
   isRelaunchSuccess=node.relaunch(chainArg=chainArg, timeout=relaunchTimeout, skipGenesis=skipGenesis)
   time.sleep(1) # Give a second to replay or resync if needed
   assert isRelaunchSuccess, relaunchAssertMessage
   return isRelaunchSuccess

try:
    TestHelper.printSystemInfo("BEGIN")

    Print("Stand up cluster")
    if cluster.launch(
            pnodes=pnodes,
            totalNodes=total_nodes,
            totalProducers=1,
            topo="mesh") is False:
        errorExit("Failed to stand up eos cluster.")

    producingNode=cluster.getNode(0)
    speculativeNode1=cluster.getNode(1)
    speculativeNode2=cluster.getNode(2)

    Print ("Wait for Cluster stabilization")
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")
    Print ("Cluster stabilized")

    Print("Wait for producing {} blocks".format(numBlocksToProduceBeforeRelaunch))
    producingNode.waitForBlock(numBlocksToProduceBeforeRelaunch, blockType=BlockType.lib)
    producingNode.waitForProducer("defproducera")

    Print("Kill all node instances.")
    for clusterNode in cluster.nodes:
        clusterNode.kill(signal.SIGTERM)
    cluster.biosNode.kill(signal.SIGTERM)
    Print("All nodeos instances killed.")

    # Remove both state and blocks such that no replay happens
    Print("Remove producer node's state and blocks directories")
    Utils.rmNodeDataDir(0)
    Print("Remove the second speculative node's state and blocks directories")
    Utils.rmNodeDataDir(2)

    Print ("Relaunch all cluster nodes instances.")
    # -e for resuming production, defproducera only producer at this point
    # skipGenesis=False for launch the same chain as before
    relaunchNode(producingNode, chainArg="-e --sync-fetch-span 5 ", skipGenesis=False)
    relaunchNode(speculativeNode1, chainArg="--sync-fetch-span 5 ")
    relaunchNode(speculativeNode2, chainArg="--sync-fetch-span 5 ", skipGenesis=False)

    Print("Note LIBs")
    prodLib = producingNode.getIrreversibleBlockNum()
    specLib1 = speculativeNode1.getIrreversibleBlockNum()
    specLib2 = speculativeNode2.getIrreversibleBlockNum()
    Print("prodLib {}, specLib1 {}, specLib2 {},".format(prodLib, specLib1, specLib2))

    Print("Wait for {} blocks to produce".format(numBlocksToWaitBeforeChecking))
    speculativeNode2.waitForBlock( specLib2 + numBlocksToWaitBeforeChecking, blockType=BlockType.lib)

    Print("Check whether LIBs advance or not")
    prodLibAfterWait = producingNode.getIrreversibleBlockNum()
    specLibAfterWait1 = speculativeNode1.getIrreversibleBlockNum()
    specLibAfterWait2 = speculativeNode2.getIrreversibleBlockNum()
    Print("prodLibAfterWait {}, specLibAfterWait1 {}, specLibAfterWait2 {},".format(prodLibAfterWait, specLibAfterWait1, specLibAfterWait2))

    assert prodLibAfterWait > prodLib and specLibAfterWait2 > specLib2, "Either producer ({} -> {})/ second speculative node ({} -> {}) is not advancing".format(prodLib, prodLibAfterWait, specLib2, specLibAfterWait2)

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
