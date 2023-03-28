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
# trx_finality_status_forked_test
#
#  Test to verify that transaction finality status feature is
#  working appropriately when forks occur
#
###############################################################
Print=Utils.Print
errorExit=Utils.errorExit


args = TestHelper.parse_args({"--prod-count","--dump-error-details","--keep-logs","-v","--leave-running","--clean-run",
                              "--wallet-port","--unshared"})
Utils.Debug=args.v
totalProducerNodes=2
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes
maxActiveProducers=3
totalProducers=maxActiveProducers
cluster=Cluster(walletd=True,unshared=args.unshared)
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
    extraNodeosArgs+=" --http-max-response-time-ms 990000"


    # ***   setup topogrophy   ***

    # "bridge" shape connects defprocera through defproducerb (in node0) to each other and defproducerc is alone (in node01)
    # and the only connection between those 2 groups is through the bridge node
    if cluster.launch(prodCount=2, topo="bridge", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducers,
                      specificExtraNodeosArgs=specificExtraNodeosArgs,
                      extraNodeosArgs=extraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")
    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    # ***   identify each node (producers and non-producing node)   ***

    nonProdNode=None
    prodNodes=[]
    producers=[]
    for i, node in enumerate(cluster.getNodes()):
        node.producers=Cluster.parseProducers(node.nodeId)
        numProducers=len(node.producers)
        Print(f"node {i} has producers={node.producers}")
        if numProducers==0:
            if nonProdNode is None:
                nonProdNode=node
            else:
                Utils.errorExit("More than one non-producing nodes")
        else:
            prodNodes.append(node)
            producers.extend(node.producers)

    prodAB=prodNodes[0]  # defproducera, defproducerb
    prodC=prodNodes[1]   # defproducerc

    # ***   Identify a block where production is stable   ***

    #verify nodes are in sync and advancing
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Creating account1")
    account1 = Account('account1')
    account1.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account1.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account1, cluster.eosioAccount, stakedDeposit=1000)

    Print("Validating accounts after bootstrap")
    cluster.validateAccounts([account1])

    # ***   Killing the "bridge" node   ***
    Print("Sending command to kill \"bridge\" node to separate the 2 producer groups.")
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
        assert prodAB.waitForNextBlock(timeout=6), "Production node AB should continue to advance, even after bridge node is killed"
        count -= 1

    assert not nonProdNode.verifyAlive(), "Bridge node should have been killed if test was functioning correctly."

    def getState(status):
        assert status is not None, "ERROR: getTransactionStatus failed to return any status"
        assert "state" in status, \
            f"ERROR: getTransactionStatus returned a status object that didn't have a \"state\" field. state: {json.dumps(status, indent=1)}"
        return status["state"]

    transferAmount = 10
    prodC.transferFunds(cluster.eosioAccount, account1, f"{transferAmount}.0000 {CORE_SYMBOL}", "fund account")
    transId = prodC.getLastTrackedTransactionId()
    retStatus = prodC.getTransactionStatus(transId)
    state = getState(retStatus)

    localState = "LOCALLY_APPLIED"
    inBlockState = "IN_BLOCK"
    irreversibleState = "IRREVERSIBLE"
    forkedOutState = "FORKED_OUT"
    unknownState = "UNKNOWN"

    assert state == localState, \
        f"ERROR: getTransactionStatus didn't return \"{localState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}"

    assert prodC.waitForNextBlock(), "Production node C should continue to advance, even after bridge node is killed"

    # since the Bridge node is killed when this producer is producing its last block in its window, there is plenty of time for the transfer to be
    # sent before the first block is created, but adding this to ensure it is in one of these blocks
    numTries = 2
    preInfo = prodC.getInfo()
    while numTries > 0:
        retStatus = prodC.getTransactionStatus(transId)
        state = getState(retStatus)
        if state == inBlockState:
            break
        numTries -= 1
        assert prodC.waitForNextBlock(), "Production node C should continue to advance, even after bridge node is killed"

    postInfo = prodC.getInfo()

    Print(f"getTransactionStatus returned status: {json.dumps(retStatus, indent=1)}")
    assert state == inBlockState, \
        f"ERROR: getTransactionStatus didn't return \"{inBlockState}\" state."

    originalInBlockState = retStatus

    Print("Relaunching the non-producing bridge node to connect the nodes")
    if not nonProdNode.relaunch():
        errorExit(f"Failure - (non-production) node {nonProdNode.nodeNum} should have restarted")

    Print("Wait for LIB to move, which indicates prodC has forked out the branch")
    assert prodC.waitForLibToAdvance(60), \
        "ERROR: Network did not reach consensus after bridge node was restarted."

    retStatus = prodC.getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == forkedOutState, \
        f"ERROR: getTransactionStatus didn't return \"{forkedOutState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod AB info: {json.dumps(prodAB.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodC.getInfo(), indent=1)}"

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print(f"node info: {json.dumps(info, indent=1)}")

    assert prodC.waitForProducer("defproducerc"), \
        f"Waiting for prodC to produce, but it never happened" + \
        f"\n\nprod AB info: {json.dumps(prodAB.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodC.getInfo(), indent=1)}"

    retStatus = prodC.getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == inBlockState, \
        f"ERROR: getTransactionStatus didn't return \"{inBlockState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod AB info: {json.dumps(prodAB.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodC.getInfo(), indent=1)}"

    afterForkInBlockState = retStatus
    afterForkBlockId = retStatus["block_id"]
    assert afterForkInBlockState["block_number"] > originalInBlockState["block_number"], \
        "ERROR: The way the test is designed, the transaction should be added to a block that has a higher number than it was in originally before it was forked out." + \
       f"\n\noriginal in block state: {json.dumps(originalInBlockState, indent=1)}\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    assert prodC.waitForBlock(afterForkInBlockState["block_number"], timeout=120, blockType=BlockType.lib), \
        f"ERROR: Block never finalized.\n\nprod AB info: {json.dumps(prodAB.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodC.getInfo(), indent=1)}" + \
        f"\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    retStatus = prodC.getTransactionStatus(transId)
    if afterForkBlockId != retStatus["block_id"]: # might have been forked out, if so wait for new block to become LIB
        assert prodC.waitForBlock(retStatus["block_number"], timeout=120, blockType=BlockType.lib), \
            f"ERROR: Block never finalized.\n\nprod AB info: {json.dumps(prodAB.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodC.getInfo(), indent=1)}" + \
            f"\n\nafter fork in block state: {json.dumps(afterForkInBlockState, indent=1)}"

    retStatus = prodC.getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == irreversibleState, \
        f"ERROR: getTransactionStatus didn't return \"{irreversibleState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}" + \
        f"\n\nprod AB info: {json.dumps(prodAB.getInfo(), indent=1)}\n\nprod C info: {json.dumps(prodC.getInfo(), indent=1)}"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
