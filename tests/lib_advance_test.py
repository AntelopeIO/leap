#!/usr/bin/env python3

import time
import decimal
import json
import math
import re
import signal

from TestHarness import Account, Cluster, Node, TestHelper, Utils, WalletMgr, CORE_SYMBOL
from TestHarness.Node import BlockType

###############################################################
# lib_advance_test
#
#  Setup 4 producers separated by a bridge node.
#  Kill bridge node, allow both sides to produce and
#  verify they can sync back together after they are
#  reconnected.
#
###############################################################
Print=Utils.Print
errorExit=Utils.errorExit


args = TestHelper.parse_args({"--activate-if","--dump-error-details","--keep-logs","-v","--leave-running",
                              "--wallet-port","--unshared"})
Utils.Debug=args.v
totalProducerNodes=4
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes
maxActiveProducers=3
totalProducers=maxActiveProducers
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
walletPort=args.wallet_port

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")
    specificExtraNodeosArgs={}
    # producer nodes will be mapped to 0 through totalProducerNodes-1, so the number totalProducerNodes will be the non-producing node
    specificExtraNodeosArgs[totalProducerNodes]="--plugin eosio::test_control_api_plugin"

    # ***   setup topogrophy   ***

    # "bridge" shape connects defproducera (node0) defproducerb (node1) defproducerc (node2) to each other and defproducerd (node3)
    # and the only connection between those 2 groups is through the bridge (node4)
    if cluster.launch(topo="./tests/bridge_for_fork_test_shape.json", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducerNodes, loadSystemContract=False,
                      activateIF=activateIF, biosFinalizer=False,
                      specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")
    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    # ***   identify each node (producers and non-producing node)   ***

    prodNode0 = cluster.getNode(0)
    prodNode1 = cluster.getNode(1)
    prodNode2 = cluster.getNode(2)
    prodNode3 = cluster.getNode(3) # other side of bridge
    nonProdNode = cluster.getNode(4)

    prodNodes=[ prodNode0, prodNode1, prodNode2, prodNode3 ]

    prodA=prodNode0 # defproducera
    prodD=prodNode3 # defproducerd

    # ***   Identify a block where production is stable   ***

    #verify nodes are in sync and advancing
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    libProdABeforeKill = prodA.getIrreversibleBlockNum()
    libProdDBeforeKill = prodD.getIrreversibleBlockNum()

    # ***   Killing the "bridge" node   ***
    Print('Sending command to kill "bridge" node to separate the 2 producer groups.')
    # kill at the beginning of the production window for defproducera, so there is time for the fork for
    # defproducerd to grow before it would overtake the fork for defproducera and defproducerb and defproducerc
    killAtProducer="defproducera"
    nonProdNode.killNodeOnProducer(producer=killAtProducer, whereInSequence=1)

    #verify that the non producing node is not alive (and populate the producer nodes with current getInfo data to report if
    #an error occurs)
    numPasses = 2
    blocksPerProducer = 12
    blocksPerRound = totalProducers * blocksPerProducer
    count = blocksPerRound * numPasses
    while nonProdNode.verifyAlive() and count > 0:
        # wait on prodNode 0 since it will continue to advance, since defproducera and defproducerb are its producers
        Print("Wait for next block")
        assert prodA.waitForNextBlock(timeout=6), "Production node A should continue to advance, even after bridge node is killed"
        count -= 1

    assert not nonProdNode.verifyAlive(), "Bridge node should have been killed if test was functioning correctly."

    transferAmount = 10
    # Does not use transaction retry (not needed)
    transfer = prodD.transferFunds(cluster.eosioAccount, cluster.defproduceraAccount, f"{transferAmount}.0000 {CORE_SYMBOL}", "fund account")
    transBlockNum = transfer['processed']['block_num']
    transId = prodD.getLastTrackedTransactionId()

    beforeBlockNum = prodA.getBlockNum()
    prodA.waitForProducer("defproducera")
    prodA.waitForProducer("defproducerb")
    prodA.waitForProducer("defproducera")
    prodA.waitForProducer("defproducerc") # produce enough that sync will have over 30 blocks to sync
    assert prodA.waitForLibToAdvance(), "Production node A should advance lib without D"
    assert prodD.waitForNextBlock(), "Production node D should continue to advance, even after bridge node is killed"
    afterBlockNum = prodA.getBlockNum()

    Print("Relaunching the non-producing bridge node to connect the nodes")
    if not nonProdNode.relaunch():
        errorExit(f"Failure - (non-production) node {nonProdNode.nodeNum} should have restarted")

    while prodD.getInfo()['last_irreversible_block_num'] < transBlockNum:
        Print("Wait for LIB to move, which indicates prodD may have forked out the branch")
        assert prodD.waitForLibToAdvance(60), \
            "ERROR: Network did not reach consensus after bridge node was restarted."

    assert prodD.waitForLibToAdvance()
    assert prodD.waitForProducer("defproducera")
    assert prodA.waitForProducer("defproducerd")

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print(f"node info: {json.dumps(info, indent=1)}")

    assert prodA.getIrreversibleBlockNum() > max(libProdABeforeKill, libProdDBeforeKill)
    assert prodD.getIrreversibleBlockNum() > max(libProdABeforeKill, libProdDBeforeKill)

    # instant finality does not drop late blocks, but can still get unlinkable when syncing and getting a produced block
    allowedUnlinkableBlocks = afterBlockNum-beforeBlockNum
    logFile = Utils.getNodeDataDir(prodNode3.nodeId) + "/stderr.txt"
    f = open(logFile)
    contents = f.read()
    if contents.count("3030001 unlinkable_block_exception: Unlinkable block") > (allowedUnlinkableBlocks):
        errorExit(f"Node{prodNode3.nodeId} has more than {allowedUnlinkableBlocks} unlinkable blocks: {logFile}.")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
