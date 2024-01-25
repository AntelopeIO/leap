#!/usr/bin/env python3

import time
import decimal
import math
import re

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr, createAccountKeys

###############################################################
# nodeos_producer_watermark_test
# --dump-error-details <Upon error print etc/eosio/node_*/config.ini and nodeos_producer_watermark_test<pid>/node_*/stderr.log to stdout>
# --keep-logs <Don't delete TestLogs/nodeos_producer_watermark_test<pid>/node_* folders upon test completion>
###############################################################
def isValidBlockProducer(prodsActive, blockNum, node):
    blockProducer=node.getBlockProducerByNum(blockNum)
    if blockProducer not in prodsActive:
        return False
    return prodsActive[blockProducer]

def validBlockProducer(prodsActive, prodsSeen, blockNum, node):
    blockProducer=node.getBlockProducerByNum(blockNum)
    if blockProducer not in prodsActive:
        Utils.cmdError("unexpected block producer %s at blockNum=%s" % (blockProducer,blockNum))
        Utils.errorExit("Failed because of invalid block producer")
    if not prodsActive[blockProducer]:
        Utils.cmdError("block producer %s for blockNum=%s not elected, belongs to node %s" % (blockProducer, blockNum, ProducerToNode.map[blockProducer]))
        Utils.errorExit("Failed because of incorrect block producer")
    prodsSeen[blockProducer]=True
    return blockProducer

def setProds(sharedProdKey):
    setProdsStr='{"schedule": ['
    firstTime=True
    for name in ["defproducera", "shrproducera", "defproducerb", "defproducerc"]:
        if firstTime:
            firstTime = False
        else:
            setProdsStr += ','
        key = cluster.defProducerAccounts[name].activePublicKey
        if name == "shrproducera":
            key = sharedProdKey
        setProdsStr += '{"producer_name":' + name + ',"authority": ["block_signing_authority_v0", {"threshold":1, "keys":[{"key":' + key + ', "weight":1}]}]}'

    setProdsStr += ' ] }'
    Utils.Print("setprods: %s" % (setProdsStr))
    opts="--permission eosio@active"
    # pylint: disable=redefined-variable-type
    trans=cluster.biosNode.pushMessage("eosio", "setprods", setProdsStr, opts)
    if trans is None or not trans[0]:
        Utils.Print("ERROR: Failed to set producer with cmd %s" % (setProdsStr))

def verifyProductionRounds(trans, node, prodsActive, rounds):
    blockNum=node.getNextCleanProductionCycle(trans)
    Utils.Print("Validating blockNum=%s" % (blockNum))

    temp=Utils.Debug
    Utils.Debug=False
    Utils.Print("FIND VALID BLOCK PRODUCER")
    blockProducer=node.getBlockProducerByNum(blockNum)
    lastBlockProducer=blockProducer
    adjust=False
    while not isValidBlockProducer(prodsActive, blockNum, node):
        adjust=True
        blockProducer=node.getBlockProducerByNum(blockNum)
        if lastBlockProducer!=blockProducer:
            Utils.Print("blockProducer=%s for blockNum=%s is for node=%s" % (blockProducer, blockNum, ProducerToNode.map[blockProducer]))
        lastBlockProducer=blockProducer
        blockNum+=1

    Utils.Print("VALID BLOCK PRODUCER")
    saw=0
    sawHigh=0
    startingFrom=blockNum
    doPrint=0
    invalidCount=0
    while adjust:
        invalidCount+=1
        if lastBlockProducer==blockProducer:
            saw+=1
        else:
            if saw>=12:
                startingFrom=blockNum
                if saw>12:
                    Utils.Print("ERROR!!!!!!!!!!!!!!      saw=%s, blockProducer=%s, blockNum=%s" % (saw,blockProducer,blockNum))
                break
            else:
                if saw > sawHigh:
                    sawHigh = saw
                    Utils.Print("sawHigh=%s" % (sawHigh))
                if doPrint < 5:
                    doPrint+=1
                    Utils.Print("saw=%s, blockProducer=%s, blockNum=%s" % (saw,blockProducer,blockNum))
                lastBlockProducer=blockProducer
                saw=1
        blockProducer=node.getBlockProducerByNum(blockNum)
        blockNum+=1

    if adjust:
        blockNum-=1

    Utils.Print("ADJUSTED %s blocks" % (invalidCount-1))

    prodsSeen=None
    reportFirstMissedBlock=False
    Utils.Print("Verify %s complete rounds of all producers producing" % (rounds))

    prodsSize = len(prodsActive)
    for i in range(0, rounds):
        prodsSeen={}
        lastBlockProducer=None
        for j in range(0, prodsSize):
            # each new set of 12 blocks should have a different blockProducer 
            if lastBlockProducer is not None and lastBlockProducer==node.getBlockProducerByNum(blockNum):
                Utils.cmdError("expected blockNum %s to be produced by any of the valid producers except %s" % (blockNum, lastBlockProducer))
                Utils.errorExit("Failed because of incorrect block producer order")

            # make sure that the next set of 12 blocks all have the same blockProducer
            lastBlockProducer=node.getBlockProducerByNum(blockNum)
            for k in range(0, 12):
                blockProducer = validBlockProducer(prodsActive, prodsSeen, blockNum, node1)
                if lastBlockProducer!=blockProducer:
                    if not reportFirstMissedBlock:
                        printStr=""
                        newBlockNum=blockNum-18
                        for l in range(0,36):
                            printStr+="%s" % (newBlockNum)
                            printStr+=":"
                            newBlockProducer=node.getBlockProducerByNum(newBlockNum)
                            printStr+="%s" % (newBlockProducer)
                            printStr+="  "
                            newBlockNum+=1
                        Utils.Print("NOTE: expected blockNum %s (started from %s) to be produced by %s, but produded by %s: round=%s, prod slot=%s, prod num=%s - %s" % (blockNum, startingFrom, lastBlockProducer, blockProducer, i, j, k, printStr))
                    reportFirstMissedBlock=True
                    break
                blockNum+=1

    # make sure that we have seen all 21 producers
    prodsSeenKeys=prodsSeen.keys()
    if len(prodsSeenKeys) != prodsSize:
        Utils.cmdError("only saw %s producers of expected %d. At blockNum %s only the following producers were seen: %s" % (len(prodsSeenKeys), prodsSize, blockNum, ",".join(prodsSeenKeys)))
        Utils.errorExit("Failed because of missing block producers")

    Utils.Debug=temp


Print=Utils.Print
errorExit=Utils.errorExit

args = TestHelper.parse_args({"--prod-count","--activate-if","--dump-error-details","--keep-logs","-v","--leave-running",
                              "--wallet-port","--unshared"})
Utils.Debug=args.v
totalNodes=3
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
prodCount=args.prod_count
walletPort=args.wallet_port

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
ClientName="cleos"

try:
    assert(totalNodes == 3)

    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    Print("Stand up cluster")
    if cluster.launch(prodCount=prodCount, onlyBios=False, pnodes=totalNodes, totalNodes=totalNodes, totalProducers=totalNodes,
                      onlySetProds=True, sharedProducers=1, activateIF=activateIF) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    node0=cluster.getNode(0)
    node1=cluster.getNode(1)
    node2=cluster.getNode(2)

    node=node0
    numprod = totalNodes + 1

    trans=None
    prodsActive={}
    prodsActive["shrproducera"] = True
    prodsActive["defproducera"] = True
    prodsActive["defproducerb"] = True
    prodsActive["defproducerc"] = True

    Print("Wait for initial schedule: defproducera(node 0) shrproducera(node 2) defproducerb(node 1) defproducerc(node 2)")
    
    tries=10
    while tries > 0:
        node.infoValid = False
        info = node.getInfo()
        if node.infoValid and node.lastRetrievedHeadBlockProducer != "eosio":
            break
        time.sleep(1)
        tries = tries-1
    if tries == 0:
        Utils.errorExit("failed to wait for initial schedule")

    # try to change signing key of shrproducera, shrproducera will produced by node1 instead of node2
    Print("change producer signing key, shrproducera will be produced by node1 instead of node2")
    shracc_node1 = cluster.defProducerAccounts["shrproducera"]
    shracc_node1.activePublicKey = cluster.defProducerAccounts["defproducerb"].activePublicKey
    setProds(shracc_node1.activePublicKey)
    Print("sleep for 4/3 rounds...")
    time.sleep(numprod * 6 * 4 / 3)
    verifyProductionRounds(trans, node0, prodsActive, 1)

    # change signing key of shrproducera that no one can sign
    accounts = createAccountKeys(1)
    Print("change producer signing key of shrproducera that none of the node has")
    shracc_node1.activePublicKey = accounts[0].activePublicKey
    del prodsActive["shrproducera"]
    setProds(shracc_node1.activePublicKey)
    Print("sleep for 4/3 rounds...")
    time.sleep(numprod * 6 * 4 / 3)
    verifyProductionRounds(trans, node0, prodsActive, 1)

    # change signing key back to node1
    Print("change producer signing key of shrproducera so that node1 can produce again")
    shracc_node1.activePublicKey = cluster.defProducerAccounts["defproducerb"].activePublicKey
    prodsActive["shrproducera"] = True
    setProds(shracc_node1.activePublicKey)
    tries=numprod * 6 * 4 # give 4 rounds
    while tries > 0:
        node.infoValid = False
        info = node.getInfo()
        if node.infoValid and node.lastRetrievedHeadBlockProducer == "shrproducera":
            break
        time.sleep(1)
        tries = tries-1
    if tries == 0:
        Utils.errorExit("shrproducera failed to produce")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
