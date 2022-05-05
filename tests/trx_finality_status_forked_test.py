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

from core_symbol import CORE_SYMBOL

def analyzeBPs(bps0, bps1, expectDivergence):
    start=0
    index=None
    length=len(bps0)
    firstDivergence=None
    errorInDivergence=False
    analysysPass=0
    bpsStr=None
    bpsStr0=None
    bpsStr1=None
    while start < length:
        analysysPass+=1
        bpsStr=None
        for i in range(start,length):
            bp0=bps0[i]
            bp1=bps1[i]
            if bpsStr is None:
                bpsStr=""
            else:
                bpsStr+=", "
            blockNum0=bp0["blockNum"]
            prod0=bp0["prod"]
            blockNum1=bp1["blockNum"]
            prod1=bp1["prod"]
            numDiff=True if blockNum0!=blockNum1 else False
            prodDiff=True if prod0!=prod1 else False
            if numDiff or prodDiff:
                index=i
                if firstDivergence is None:
                    firstDivergence=min(blockNum0, blockNum1)
                if not expectDivergence:
                    errorInDivergence=True
                break
            bpsStr+=str(blockNum0)+"->"+prod0

        if index is None:
            if expectDivergence:
                errorInDivergence=True
                break
            return None

        bpsStr0=None
        bpsStr2=None
        start=length
        for i in range(index,length):
            if bpsStr0 is None:
                bpsStr0=""
                bpsStr1=""
            else:
                bpsStr0+=", "
                bpsStr1+=", "
            bp0=bps0[i]
            bp1=bps1[i]
            blockNum0=bp0["blockNum"]
            prod0=bp0["prod"]
            blockNum1=bp1["blockNum"]
            prod1=bp1["prod"]
            numDiff="*" if blockNum0!=blockNum1 else ""
            prodDiff="*" if prod0!=prod1 else ""
            if not numDiff and not prodDiff:
                start=i
                index=None
                if expectDivergence:
                    errorInDivergence=True
                break
            bpsStr0+=str(blockNum0)+numDiff+"->"+prod0+prodDiff
            bpsStr1+=str(blockNum1)+numDiff+"->"+prod1+prodDiff
        if errorInDivergence:
            break

    if errorInDivergence:
        msg="Failed analyzing block producers - "
        if expectDivergence:
            msg+="nodes do not indicate different block producers for the same blocks, but they are expected to diverge at some point."
        else:
            msg+="did not expect nodes to indicate different block producers for the same blocks."
        msg+="\n  Matching Blocks= %s \n  Diverging branch node0= %s \n  Diverging branch node1= %s" % (bpsStr,bpsStr0,bpsStr1)
        Utils.errorExit(msg)

    return firstDivergence

def getMinHeadAndLib(prodNodes):
    info0=prodNodes[0].getInfo(exitOnError=True)
    info1=prodNodes[1].getInfo(exitOnError=True)
    headBlockNum=min(int(info0["head_block_num"]),int(info1["head_block_num"]))
    libNum=min(int(info0["last_irreversible_block_num"]), int(info1["last_irreversible_block_num"]))
    return (headBlockNum, libNum)



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
                    "--transaction-finality-status-success-duration-sec {} --transaction-finality-status-failure-duration-sec {}". \
                    format(successDuration, failure_duration)

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
    for i in range(0, totalNodes):
        node=cluster.getNode(i)
        node.producers=Cluster.parseProducers(i)
        numProducers=len(node.producers)
        Print("node has producers=%s" % (node.producers))
        if numProducers==0:
            if nonProdNode is None:
                nonProdNode=node
                nonProdNode.nodeNum=i
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
        assert prodNodes[0].waitForNextBlock(timeout=6), Print("Production node 0 should continue to advance, even after bridge node is killed")
        count -= 1

    assert not nonProdNode.verifyAlive(), Print("Bridge node should have been killed if test was functioning correctly.")

    def getState(status):
        assert status is not None, Print("ERROR: getTransactionStatus failed to return any status")
        assert "state" in status, \
            Print("ERROR: getTransactionStatus returned a status object that didn't have a \"state\" field. state: {}".
                  format(json.dumps(status, indent=1)))
        return status["state"]

    transferAmount = 10
    prodNodes[1].transferFunds(cluster.eosioAccount, account1, "{}.0000 {}".format(transferAmount, CORE_SYMBOL), "fund account")
    transId = prodNodes[1].getLastSentTransactionId()
    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    localState = "LOCALLY_APPLIED"
    inBlockState = "IN_BLOCK"
    irreversibleState = "IRREVERSIBLE"
    forkedOutState = "FORKED_OUT"
    unknownState = "UNKNOWN"

    assert state == localState, \
        Print("ERROR: getTransactionStatus didn't return \"{}\" state.\n\nstatus: {}".format(localState, json.dumps(retStatus, indent=1)))

    assert prodNodes[1].waitForNextBlock(), Print("Production node 1 should continue to advance, even after bridge node is killed")

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
        assert prodNodes[1].waitForNextBlock(), Print("Production node 1 should continue to advance, even after bridge node is killed")

    postInfo = prodNodes[1].getInfo()
    Print("preInfo: {}\n\npostInfo: {}".format(json.dumps(preInfo, indent=1), json.dumps(postInfo, indent=1)))

    assert state == inBlockState, \
        Print("ERROR: getTransactionStatus didn't return \"{}\" state.\n\nstatus: {}".format(inBlockState, json.dumps(retStatus, indent=1)))

    Print("Relaunching the non-producing bridge node to connect the nodes")
    if not nonProdNode.relaunch(nonProdNode.nodeNum):
        errorExit("Failure - (non-production) node %d should have restarted" % (nonProdNode.nodeNum))

    Print("Wait for LIB to move, which indicates prodNode[1] has forked out the branch")
    assert cluster.waitOnClusterSync(blockAdvancing=1, blockType=BlockType.lib), \
        Print("ERROR: Network did not reach concensus after bridge node was restarted.")

    Print("Wait till prodNodes[1] is reporting at least the same head block number as the forked out block")
    assert prodNodes[1].waitForBlock(retStatus["block_number"]), \
        Print("Production node 1 should continue to advance, even after bridge node is killed. \n\nretStatus: {}".
            format(json.dumps(retStatus, indent=1)))

    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == forkedOutState, \
        Print("ERROR: getTransactionStatus didn't return \"{}\" state.\n\nstatus: {}\n\nprod 0 info: {}\n\nprod 1 info: {}".format(forkedOutState, json.dumps(retStatus, indent=1), json.dumps(prodNodes[0].getInfo(), indent=1), json.dumps(prodNodes[1].getInfo(), indent=1)))

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print("node info: %s" % (info))

    time.sleep(60)

    retStatus = prodNodes[1].getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == irreversibleState, \
        Print("ERROR: getTransactionStatus didn't return \"{}\" state.\n\nstatus: {}".format(inBlockState, json.dumps(retStatus, indent=1)))

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

    if not testSuccessful:
        Print(Utils.FileDivider)
        Print("Compare Blocklog")
        cluster.compareBlockLogs()
        Print(Utils.FileDivider)
        Print("Print Blocklog")
        cluster.printBlockLog()
        Print(Utils.FileDivider)

exit(0)
