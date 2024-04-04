#!/usr/bin/env python3
import sys
import time
import decimal
import json
import math
import re
import signal

from TestHarness import Account, Cluster, Node, TestHelper, Utils, WalletMgr, CORE_SYMBOL
from TestHarness.Node import BlockType

###############################################################
# trx_finality_status_forked_test
#
#  Test to verify that transaction finality status feature is
#  working appropriately when forks occur.
#  Note this test does not use transaction retry as forked out
#  transactions should always make it into a block unless they
#  expire.
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

    # ensure that transactions don't get cleaned up too early
    successDuration = 360
    failure_duration = 360
    extraNodeosArgs=" --transaction-finality-status-max-storage-size-gb 1 " + \
                   f"--transaction-finality-status-success-duration-sec {successDuration} --transaction-finality-status-failure-duration-sec {failure_duration}"
    extraNodeosArgs+=" --http-max-response-time-ms 990000"


    # ***   setup topogrophy   ***

    # "bridge" shape connects defproducera (node0) defproducerb (node1) defproducerc (node2) to each other and defproducerd (node3)
    # and the only connection between those 2 groups is through the bridge (node4)
    if cluster.launch(topo="./tests/bridge_for_fork_test_shape.json", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducerNodes, loadSystemContract=False,
                      activateIF=activateIF, biosFinalizer=False,
                      specificExtraNodeosArgs=specificExtraNodeosArgs,
                      extraNodeosArgs=extraNodeosArgs) is False:
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

    # ***   Killing the "bridge" node   ***
    Print('Sending command to kill "bridge" node to separate the 2 producer groups.')
    # kill at the beginning of the production window for defproducera, so there is time for the fork for
    # defproducerc to grow before it would overtake the fork for defproducera and defproducerb
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

    assert prodD.waitForNextBlock(), "Prod node D should continue to advance, even after bridge node is killed"

    def getState(status):
        assert status is not None, "ERROR: getTransactionStatus failed to return any status"
        assert "state" in status, \
            f"ERROR: getTransactionStatus returned a status object that didn't have a \"state\" field. status: {json.dumps(status, indent=1)}"
        return status["state"]

    def getBlockNum(status):
        assert status is not None, "ERROR: getTransactionStatus failed to return any status"
        if "block_number" in status:
            return status["block_number"]
        assert "head_number" in status, \
            f"ERROR: getTransactionStatus returned a status object that didn't have a \"head_number\" field. status: {json.dumps(status, indent=1)}"
        return status["head_number"]

    def getBlockID(status):
        assert status is not None, "ERROR: getTransactionStatus failed to return any status"
        if "block_id" in status:
            return status["block_id"]
        assert "head_id" in status, \
            f"ERROR: getTransactionStatus returned a status object that didn't have a \"head_id\" field. status: {json.dumps(status, indent=1)}"
        return status["head_id"]

    transferAmount = 10
    # Does not use transaction retry (not needed)
    transfer = prodD.transferFunds(cluster.eosioAccount, cluster.defproduceraAccount, f"{transferAmount}.0000 {CORE_SYMBOL}", "fund account")
    transBlockNum = transfer['processed']['block_num']
    transId = prodD.getLastTrackedTransactionId()
    retStatus = prodD.getTransactionStatus(transId)
    state = getState(retStatus)

    localState = "LOCALLY_APPLIED"
    inBlockState = "IN_BLOCK"
    irreversibleState = "IRREVERSIBLE"
    forkedOutState = "FORKED_OUT"
    unknownState = "UNKNOWN"

    assert state == localState or state == inBlockState, \
        f"ERROR: getTransactionStatus didn't return \"{localState}\" or \"{inBlockState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}"

    assert prodD.waitForNextBlock(), "Production node D should continue to advance, even after bridge node is killed"

    # since the Bridge node is killed when this producer is producing its last block in its window, there is plenty of time for the transfer to be
    # sent before the first block is created, but adding this to ensure it is in one of these blocks
    numTries = 2
    while numTries > 0:
        retStatus = prodD.getTransactionStatus(transId)
        state = getState(retStatus)
        if state == inBlockState:
            break
        numTries -= 1
        assert prodD.waitForNextBlock(), "Production node D should continue to advance, even after bridge node is killed"

    Print(f"getTransactionStatus returned status: {json.dumps(retStatus, indent=1)}")
    assert state == inBlockState, \
        f"ERROR: getTransactionStatus didn't return \"{inBlockState}\" state."

    originalInBlockState = retStatus

    Print("Relaunching the non-producing bridge node to connect the nodes")
    if not nonProdNode.relaunch():
        errorExit(f"Failure - (non-production) node {nonProdNode.nodeNum} should have restarted")

    Print("Repeatedly check status looking for forked out state until after LIB moves and defproducerd")
    while True:
        info = prodD.getInfo()
        retStatus = prodD.getTransactionStatus(transId)
        state = getState(retStatus)
        blockNum = getBlockNum(retStatus)
        if state == forkedOutState or ( info['head_block_producer'] == 'defproducerd' and info['last_irreversible_block_num'] > blockNum ):
            break

    if state == irreversibleState:
        Print(f"Transaction became irreversible before it could be found forked out: {json.dumps(retStatus, indent=1)}")
        testSuccessful = True
        sys.exit(0)

    assert state == forkedOutState, \
        f"ERROR: getTransactionStatus didn't return \"{forkedOutState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod A info: {json.dumps(prodA.getInfo(), indent=1)}\n\nprod D info: {json.dumps(prodD.getInfo(), indent=1)}"

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print(f"node info: {json.dumps(info, indent=1)}")

    assert prodD.waitForProducer("defproducerd"), \
        f"Waiting for prodD to produce, but it never happened" + \
        f"\n\nprod A info: {json.dumps(prodA.getInfo(), indent=1)}\n\nprod D info: {json.dumps(prodD.getInfo(), indent=1)}"

    retStatus = prodD.getTransactionStatus(transId)
    state = getState(retStatus)

    # it is possible for another fork switch to cause the trx to be forked out again
    if state == forkedOutState or state == localState:
        while True:
            info = prodD.getInfo()
            retStatus = prodD.getTransactionStatus(transId)
            state = getState(retStatus)
            blockNum = getBlockNum(retStatus) + 2 # Add 2 to give time to move from locally applied to in-block
            if (state == inBlockState or state == irreversibleState) or ( info['head_block_producer'] == 'defproducerd' and info['last_irreversible_block_num'] > blockNum ):
                break

    assert state == inBlockState or state == irreversibleState, \
        f"ERROR: getTransactionStatus didn't return \"{inBlockState}\" or \"{irreversibleState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod A info: {json.dumps(prodA.getInfo(), indent=1)}\n\nprod D info: {json.dumps(prodD.getInfo(), indent=1)}"

    afterForkInBlockState = retStatus
    afterForkBlockId = getBlockID(retStatus)
    assert getBlockNum(afterForkInBlockState) > getBlockNum(originalInBlockState), \
        "ERROR: The way the test is designed, the transaction should be added to a block that has a higher number than it was in originally before it was forked out." + \
       f"\n\noriginal in block state: {json.dumps(originalInBlockState, indent=1)}\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    assert prodD.waitForBlock(getBlockNum(afterForkInBlockState), timeout=120, blockType=BlockType.lib), \
        f"ERROR: Block never finalized.\n\nprod A info: {json.dumps(prodA.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodD.getInfo(), indent=1)}" + \
        f"\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    retStatus = prodD.getTransactionStatus(transId)
    if afterForkBlockId != getBlockID(retStatus): # might have been forked out, if so wait for new block to become LIB
        assert prodD.waitForBlock(getBlockNum(retStatus), timeout=120, blockType=BlockType.lib), \
            f"ERROR: Block never finalized.\n\nprod A info: {json.dumps(prodA.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodD.getInfo(), indent=1)}" + \
            f"\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    retStatus = prodD.getTransactionStatus(transId)
    state = getState(retStatus)

    # it is possible for another fork switch to cause the trx to be forked out again
    if state == forkedOutState:
        while True:
            info = prodD.getInfo()
            retStatus = prodD.getTransactionStatus(transId)
            state = getState(retStatus)
            blockNum = getBlockNum(retStatus)
            if state == irreversibleState or ( info['head_block_producer'] == 'defproducerd' and info['last_irreversible_block_num'] > blockNum ):
                break

    assert state == irreversibleState, \
        f"ERROR: getTransactionStatus didn't return \"{irreversibleState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod A info: {json.dumps(prodA.getInfo(), indent=1)}\n\nprod D info: {json.dumps(prodD.getInfo(), indent=1)}"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
