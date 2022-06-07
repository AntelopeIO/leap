#!/usr/bin/env python3

from testUtils import Utils
import testUtils
import time
from Cluster import Cluster
from WalletMgr import WalletMgr
from Node import BlockType
from Node import Node
from TestHelper import TestHelper
from testUtils import Account

import decimal
import json
import math
import re
import signal

###############################################################
# trx_finality_status_forked_test
#
#  Test to verify that transaction finality status feature is
#  working appropriately when forks occur
#
###############################################################
Print=Utils.Print
errorExit=Utils.errorExit

from core_symbol import CORE_SYMBOL


args = TestHelper.parse_args({"--prod-count","--dump-error-details","--keep-logs","-v","--leave-running","--clean-run",
                              "--wallet-port"})
Utils.Debug=args.v
totalProducerNodes=2
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes
maxActiveProducers=3
totalProducers=maxActiveProducers
cluster=Cluster(walletd=True)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
killAll=args.clean_run
walletPort=args.wallet_port

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
killEosInstances=not dontKill
killWallet=not dontKill

WalletdName=Utils.EosWalletName
ClientName="cleos"

EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    Print("Stand up cluster")
    specificExtraNodeosArgs={}
    # producer nodes will be mapped to 0 through totalProducerNodes-1, so the number totalProducerNodes will be the non-producing node
    specificExtraNodeosArgs[totalProducerNodes]="--plugin eosio::test_control_api_plugin"

    # ensure that transactions don't get cleaned up too early
    successDuration = 360
    failure_duration = 360
    extraNodeosArgs=" --transaction-finality-status-max-storage-size-gb 1 " + \
                   f"--transaction-finality-status-success-duration-sec {successDuration} --transaction-finality-status-failure-duration-sec {failure_duration}"
    extraNodeosArgs+=" --plugin eosio::trace_api_plugin --trace-no-abis"
    extraNodeosArgs+=" --http-max-response-time-ms 990000"


    # ***   setup topogrophy   ***

    # "bridge" shape connects defprocera through defproducerb (in node0) to each other and defproducerc is alone (in node01)
    # and the only connection between those 2 groups is through the bridge node
    if cluster.launch(prodCount=2, topo="bridge", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducers,
                      useBiosBootFile=False, specificExtraNodeosArgs=specificExtraNodeosArgs,
                      extraNodeosArgs=extraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")
    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    # ***   identify each node (producers and non-producing node)   ***

    nonProdNode=None
    prodNodes=[]
    producers=[]
    for node in cluster.getNodes():
        node.producers=Cluster.parseProducers(node.nodeId)
        numProducers=len(node.producers)
        Print(f"node has producers={node.producers}")
        if numProducers==0:
            if nonProdNode is None:
                nonProdNode=node
            else:
                Utils.errorExit("More than one non-producing nodes")
        else:
            prodNodes.append(node)
            producers.extend(node.producers)


    node=prodNodes[0]
    node1=prodNodes[1]

    # ***   Identify a block where production is stable   ***

    #verify nodes are in sync and advancing
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Creating account1")
    account1 = Account('account1')
    account1.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account1.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account1, cluster.eosioAccount, stakedDeposit=1000)

    Print("Creating account2")
    account2 = Account('account2')
    account2.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account2.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account2, cluster.eosioAccount, stakedDeposit=1000)

    Print("Validating accounts after bootstrap")
    cluster.validateAccounts([account1, account2])

    # ***   Killing the "bridge" node   ***
    Print("Sending command to kill \"bridge\" node to separate the 2 producer groups.")
    # block number to start expecting node killed after
    preKillBlockNum=nonProdNode.getBlockNum()
    preKillBlockProducer=nonProdNode.getBlockProducerByNum(preKillBlockNum)
    # kill at the end of the production window for defproducera, so there is still time for the fork for
    # defproducerc to grow before it would overtake the fork for defproducera and defproducerb
    killAtProducer="defproducerc"
    nonProdNode.killNodeOnProducer(producer=killAtProducer, whereInSequence=11)

    #verify that the non producing node is not alive (and populate the producer nodes with current getInfo data to report if
    #an error occurs)
    numPasses = 2
    blocksPerProducer = 12
    blocksPerRound = totalProducers * blocksPerProducer
    count = blocksPerRound * numPasses
    while nonProdNode.verifyAlive() and count > 0:
        # wait on prodNode 0 since it will continue to advance, since defproducera and defproducerb are its producers
        Print("Wait for next block")
        assert prodNodes[0].waitForNextBlock(timeout=6), "Production node 0 should continue to advance, even after bridge node is killed"
        count -= 1

    assert not nonProdNode.verifyAlive(), "Bridge node should have been killed if test was functioning correctly."

    def getState(status):
        assert status is not None, "ERROR: getTransactionStatus failed to return any status"
        assert "state" in status, \
            f"ERROR: getTransactionStatus returned a status object that didn't have a \"state\" field. state: {json.dumps(status, indent=1)}"
        return status["state"]

    transferAmount = 10
    prodNodes[1].transferFunds(cluster.eosioAccount, account1, f"{transferAmount}.0000 {CORE_SYMBOL}", "fund account")
    transId = prodNodes[1].getLastTrackedTransactionId()
    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    localState = "LOCALLY_APPLIED"
    inBlockState = "IN_BLOCK"
    irreversibleState = "IRREVERSIBLE"
    forkedOutState = "FORKED_OUT"
    unknownState = "UNKNOWN"

    assert state == localState, \
        f"ERROR: getTransactionStatus didn't return \"{localState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}"

    assert prodNodes[1].waitForNextBlock(), "Production node 1 should continue to advance, even after bridge node is killed"

    # since the Bridge node is killed when this producer is producing its last block in its window, there is plenty of time for the transfer to be
    # sent before the first block is created, but adding this to ensure it is in one of these blocks
    numTries = 2
    preInfo = prodNodes[1].getInfo()
    while numTries > 0:
        retStatus = prodNodes[1].getTransactionStatus(transId)
        state = getState(retStatus)
        if state == inBlockState:
            break
        numTries -= 1
        assert prodNodes[1].waitForNextBlock(), "Production node 1 should continue to advance, even after bridge node is killed"

    postInfo = prodNodes[1].getInfo()

    Print(f"getTransactionStatus returned status: {json.dumps(retStatus, indent=1)}")
    assert state == inBlockState, \
        f"ERROR: getTransactionStatus didn't return \"{inBlockState}\" state."

    originalInBlockState = retStatus

    Print("Relaunching the non-producing bridge node to connect the nodes")
    if not nonProdNode.relaunch():
        errorExit(f"Failure - (non-production) node {nonProdNode.nodeNum} should have restarted")

    Print("Wait for LIB to move, which indicates prodNode[1] has forked out the branch")
    assert prodNodes[1].waitForLibToAdvance(), \
        "ERROR: Network did not reach concensus after bridge node was restarted."

    Print("Wait till prodNodes[1] is reporting at least the same head block number as the forked out block")
    assert prodNodes[1].waitForBlock(originalInBlockState["block_number"]), \
        f"Production node 1 should continue to advance after LIB starts advancing. \n\originalInBlockState: {json.dumps(originalInBlockState, indent=1)}"

    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == forkedOutState, \
        f"ERROR: getTransactionStatus didn't return \"{forkedOutState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod 0 info: {json.dumps(prodNodes[0].getInfo(), indent=1)}\n\nprod 1 info: {json.dumps(prodNodes[1].getInfo(), indent=1)}"

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print(f"node info: {json.dumps(info, indent=1)}")

    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == forkedOutState, \
        f"ERROR: getTransactionStatus didn't return \"{forkedOutState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod 0 info: {json.dumps(prodNodes[0].getInfo(), indent=1)}\n\nprod 1 info: {json.dumps(prodNodes[1].getInfo(), indent=1)}"

    assert prodNodes[1].waitForProducer("defproducerc"), \
        f"Waiting for prodNode 1 to produce, but it never happened" + \
        f"\n\nprod 0 info: {json.dumps(prodNodes[0].getInfo(), indent=1)}\n\nprod 1 info: {json.dumps(prodNodes[1].getInfo(), indent=1)}"

    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == inBlockState, \
        f"ERROR: getTransactionStatus didn't return \"{inBlockState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod 0 info: {json.dumps(prodNodes[0].getInfo(), indent=1)}\n\nprod 1 info: {json.dumps(prodNodes[1].getInfo(), indent=1)}"

    afterForkInBlockState = retStatus
    assert afterForkInBlockState["block_number"] > originalInBlockState["block_number"], \
        "ERROR: The way the test is designed, the transaction should be added to a block that has a higher number than it was in originally before it was forked out." + \
       f"\n\noriginal in block state: {json.dumps(originalInBlockState, indent=1)}\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    assert prodNodes[1].waitForBlock(afterForkInBlockState["block_number"], blockType=BlockType.lib), \
        f"ERROR: Block never finalized.\n\nprod 0 info: {json.dumps(prodNodes[0].getInfo(), indent=1)}\n\nprod 1 info: {json.dumps(prodNodes[1].getInfo(), indent=1)}" + \
        f"\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == irreversibleState, \
        f"ERROR: getTransactionStatus didn't return \"{irreversibleState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod 0 info: {json.dumps(prodNodes[0].getInfo(), indent=1)}\n\nprod 1 info: {json.dumps(prodNodes[1].getInfo(), indent=1)}"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
