#!/usr/bin/env python3

from testUtils import Utils
from Cluster import Cluster
from WalletMgr import WalletMgr
from Node import Node
from Node import BlockType
from TestHelper import TestHelper

import random
import signal
import time

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
                            ,"--dump-error-details","-v","--leave-running","--clean-run"
                            })
pnodes=1
total_nodes=3 # first one is producer, and last two are speculative nodes
debug=args.v
killEosInstances=not args.leave_running
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
killAll=args.clean_run
relaunchTimeout=10
# Don't want to set too big, trying to reduce test time, but needs to be large enough for test to finish before
# restart re-creates this many blocks.
numBlocksToProduceBeforeRelaunch=80
numBlocksToWaitBeforeChecking=20

Utils.Debug=debug
testSuccessful=False

seed=1
random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(walletd=True)
walletMgr=WalletMgr(True)
cluster.setWalletMgr(walletMgr)

def relaunchNode(node: Node, nodeId, chainArg="", skipGenesis=True, relaunchAssertMessage="Fail to relaunch"):
   isRelaunchSuccess=node.relaunch(nodeId, chainArg=chainArg, timeout=relaunchTimeout, skipGenesis=skipGenesis, cachePopen=True)
   time.sleep(1) # Give a second to replay or resync if needed
   assert isRelaunchSuccess, relaunchAssertMessage
   return isRelaunchSuccess

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    walletMgr.killall(allInstances=killAll)
    walletMgr.cleanup()

    # set the last two nodes as speculative
    specificExtraNodeosArgs={}
    specificExtraNodeosArgs[1]="--read-mode speculative "
    specificExtraNodeosArgs[2]="--read-mode speculative "

    Print("Stand up cluster")
    if cluster.launch(
            pnodes=pnodes,
            totalNodes=total_nodes,
            totalProducers=1,
            useBiosBootFile=False,
            topo="mesh",
            specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
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
    # -e -p eosio for resuming production, skipGenesis=False for launch the same chain as before
    relaunchNode(producingNode, 0, chainArg="-e -p eosio --sync-fetch-span 5 ", skipGenesis=False)
    relaunchNode(speculativeNode1, 1, chainArg="--sync-fetch-span 5 ")
    relaunchNode(speculativeNode2, 2, chainArg="--sync-fetch-span 5 ", skipGenesis=False)

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
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killEosInstances, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
