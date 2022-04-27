#!/usr/bin/env python3

from testUtils import Utils
import copy
import time
from Cluster import Cluster
from WalletMgr import WalletMgr
from Node import BlockType
import json
import os
import signal
import subprocess
from TestHelper import AppArgs
from TestHelper import TestHelper
from testUtils import Account

###############################################################
# trx_finality_status_test
#
#  Test to verify that transaction finality status feature is
#  working appropriately
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

from core_symbol import CORE_SYMBOL


appArgs=AppArgs()
args = TestHelper.parse_args({"-n", "--prod-count", "--dump-error-details","--keep-logs","-v","--leave-running","--clean-run"})
Utils.Debug=args.v
pnodes=2
totalNodes=args.n
if totalNodes<=pnodes:
    totalNodes=pnodes+2
totalNonProducerNodes=totalNodes-pnodes
cluster=Cluster(walletd=True)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
prodCount=args.prod_count
killAll=args.clean_run
walletPort=TestHelper.DEFAULT_WALLET_PORT
totalNodes=pnodes+1

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
    successDuration = 25
    failure_duration = 40
    extraNodeosArgs=" --transaction-finality-status-max-storage-size-gb 1 " + \
                    "--transaction-finality-status-success-duration-sec {} --transaction-finality-status-failure-duration-sec {}". \
                    format(successDuration, failure_duration)
    if cluster.launch(prodCount=prodCount, onlyBios=False, pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes*prodCount,
                      useBiosBootFile=False, topo="line", extraNodeosArgs=extraNodeosArgs) is False:
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    biosNode=cluster.biosNode
    prod0=cluster.getNode(0)
    prod1=cluster.getNode(1)
    testNode=cluster.getNode(totalNodes-1)

    Print("Kill the bios node")
    biosNode.kill(signal.SIGTERM)

    Print("Wait for node0's head block to become irreversible")
    cluster.waitOnClusterSync()

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

    prod0Info=prod0.getInfo(exitOnError=True)
    nodeInfo=testNode.getInfo(exitOnError=True)
    Print("prod0 info={}".format(json.dumps(prod0Info, indent=1)))
    Print("test node info={}".format(json.dumps(nodeInfo, indent=1)))

    def getState(status):
        assert status is not None, Print("ERROR: getTransactionStatus failed to return any status")
        assert "state" in status, \
            Print("ERROR: getTransactionStatus returned a status object that didn't have a \"state\" field. state: {}".
                  format(json.dumps(status, indent=1)))
        return status["state"]

    def isState(state, expectedState, allowedState):
        if state == expectedState:
            return True
        assert state in allowedState, \
               Print("ERROR: getTransactionStatus should have indicated a \"state\" of \"{}\" or \"{}\" but it was \"{}\"".
                     format(expectedState, allowedState, state))
        return False

    transferAmount=10
    localState = "LOCALLY_APPLIED"
    inBlockState = "IN_BLOCK"
    irreversibleState = "IRREVERSIBLE"
    unknownState = "UNKNOWN"
    REMOVEinfo = testNode.getInfo()
    testNode.REMOVEstatus = {
        "state" : inBlockState,
        "head_number" : REMOVEinfo["head_block_num"],
        "head_id": REMOVEinfo["head_block_id"],
        "head_timestamp": REMOVEinfo["head_block_time"],
        "irreversible_number" : REMOVEinfo["last_irreversible_block_num"],
        "irreversible_id": REMOVEinfo["last_irreversible_block_id"],
        "irreversible_timestamp": "",
        "expiration": "",
        "last_tracked_block_id": REMOVEinfo["last_irreversible_block_num"] - 12
    }  #REMOVE
    numTries = 3
    status = []
    state = None
    i = 0
    preInfo = None
    postInfo = None
    for i in range(0, numTries):
        preInfo = testNode.getInfo()
        testNode.transferFunds(cluster.eosioAccount, account1, "{}.0000 {}".format(transferAmount, CORE_SYMBOL), "fund account")
        postInfo = testNode.getInfo()
        transId=testNode.getLastSentTransactionId()
        retStatus=testNode.getTransactionStatus(transId)
        state = getState(retStatus)

        if isState(state, expectedState=localState, allowedState=inBlockState):
            status.append(copy.copy(retStatus))
            break

        transferAmount+=1
        Print("Failed to catch status as \"{}\" after {} of {}".format(localState, i + 1, numTries))
        testNode.REMOVEstatus["state"] = localState  #REMOVE

    assert state == localState, Print("ERROR: getTransactionStatus never returned a \"{}\" state, even after {} tries".format(localState, numTries))
    startingBlockNum=postInfo["head_block_num"]

    def validateTrxState(status, present):
        bnPresent = "block_number" in status
        biPresent = "block_id" in status
        btPresent = "block_timestamp" in status
        desc = "" if present else "not "
        group = bnPresent and biPresent and btPresent if present else not bnPresent and not biPresent and not btPresent
        assert group, \
            Print("ERROR: getTransactionStatus should{} contain \"block_number\", \"block_id\", or \"block_timestamp\" since state was \"{}\".\nstatus: {}".
                  format(desc, getState(status), json.dumps(status, indent=1)))

    validateTrxState(status[0], present=False)

    def validate(status, knownTrx=True):
        assert "head_number" in status and "head_id" in status and "head_timestamp" in status, \
            Print("ERROR: getTransactionStatus should contain \"head_number\", \"head_id\", and \"head_timestamp\" always.\nstatus: {}".
                  format(json.dumps(status, indent=1)))

        assert "irreversible_number" in status and "irreversible_id" in status and "irreversible_timestamp" in status, \
            Print("ERROR: getTransactionStatus should contain \"irreversible_number\", \"irreversible_id\", and \"irreversible_timestamp\" always.\nstatus: {}".
                  format(json.dumps(status, indent=1)))

        assert not knownTrx or "expiration" in status, \
            Print("ERROR: getTransactionStatus should contain \"expiration\" always.\nstatus: {}".
                  format(json.dumps(status, indent=1)))

        assert "last_tracked_block_id" in status, \
            Print("ERROR: getTransactionStatus should contain \"last_tracked_block_id\" always.\nstatus: {}".
                  format(json.dumps(status, indent=1)))

    validate(status[0])

    testNode.waitForBlock(REMOVEinfo["head_block_num"] + 1)
    REMOVEinfo2 = testNode.getInfo()
    testNode.REMOVEstatus.update({
        "block_number": REMOVEinfo["head_block_num"],
        "block_id": REMOVEinfo["head_block_id"],
        "block_timestamp": REMOVEinfo["head_block_time"],
        "head_number" : REMOVEinfo2["head_block_num"],
        "head_id": REMOVEinfo2["head_block_id"],
        "head_timestamp": REMOVEinfo2["head_block_time"],
    })     #REMOVE
    numTries = 5
    for i in range(0, numTries):
        retStatus=testNode.getTransactionStatus(transId)
        state = getState(retStatus)

        if isState(state, expectedState=inBlockState, allowedState=localState):
            status.append(copy.copy(retStatus))
            break

        Print("Failed to catch status as \"{}\" after {} of {}".format(localState, i + 1, numTries))
        testNode.waitForNextBlock()
        testNode.REMOVEstatus["state"] = inBlockState  #REMOVE

    assert state == inBlockState, Print("ERROR: getTransactionStatus never returned a \"{}\" state, even after {} tries".format(localState, numTries))

    validateTrxState(status[1], present=True)

    validate(status[1])

    assert status[0]["head_number"] < status[1]["head_number"], \
        Print("ERROR: Successive calls to getTransactionStatus should return increasing values for \"head_number\".\n1st status: {}\n\n2nd status: {}".
              format(json.dumps(status[0], indent=1), json.dumps(status[1], indent=1)))

    block_number = status[1]["block_number"]
    assert testNode.waitForIrreversibleBlock(block_number, timeout=180), \
        Print("ERROR: Failed to advance irreversible block to {}. \nAPI Node info: {}\n\nProducer info: {}".
              format(block_number, json.dumps(prod0.getInfo(), indent=1), json.dumps(testNode.getInfo(), indent=1)))
    testNode.REMOVEstatus["state"] = irreversibleState  #REMOVE

    retStatus=testNode.getTransactionStatus(transId)
    state = getState(retStatus)
    assert state == irreversibleState, \
        Print("ERROR: Successive calls to getTransactionStatus should have resulted in eventual \"{}\" state.\n1st status: {}\n\n2nd status: {}\n\nfinal status: {}".
              format(irreversibleState, json.dumps(status[0], indent=1), json.dumps(status[1], indent=1), json.dumps(retStatus, indent=1)))
    validate(retStatus)

    leeway=4
    assert testNode.waitForBlock(blockNum=startingBlockNum+(successDuration*2),timeout=successDuration+leeway)

    testNode.REMOVEstatus["state"] = unknownState    # REMOVE
    del testNode.REMOVEstatus["block_number"]        # REMOVE
    del testNode.REMOVEstatus["block_id"]            # REMOVE
    del testNode.REMOVEstatus["block_timestamp"]     # REMOVE
    retStatus=testNode.getTransactionStatus(transId)
    state = getState(retStatus)
    assert state == unknownState, \
        Print("ERROR: Calling getTransactionStatus after the success_duration should have resulted in an \"{}\" state.\nstatus: {}".
              format(irreversibleState, json.dumps(retStatus, indent=1)))
    validateTrxState(retStatus, present=False)
    validate(retStatus, knownTrx=False)

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exit(0)
