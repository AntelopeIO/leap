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

########################################################################
# trx_finality_status_test
#
#  Test to verify that transaction finality status feature is working
#  appropriately.
# 
#  It sets up a "line" of block producers and non-producing nodes (NPN)
#  so that a transaction added to the last NPN will have to travel along
#  the "line" of nodes till it gets in a block which will also have to
#  be sent back along that "line" till it gets to the last NPN.
#
########################################################################

Print=Utils.Print
errorExit=Utils.errorExit

from core_symbol import CORE_SYMBOL


appArgs=AppArgs()
args = TestHelper.parse_args({"-n", "--dump-error-details","--keep-logs","-v","--leave-running","--clean-run"})
Utils.Debug=args.v
pnodes=3
totalNodes=args.n
if totalNodes<=pnodes+2:
    totalNodes=pnodes+2
cluster=Cluster(walletd=True)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
prodCount=1
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

    def isState(state, expectedState, allowedState=None, notAllowedState=None):
        if state == expectedState:
            return True
        assert allowedState is None or state == allowedState, \
               Print("ERROR: getTransactionStatus should have indicated a \"state\" of \"{}\" or \"{}\" but it was \"{}\"".
                     format(expectedState, allowedState, state))
        assert notAllowedState is None or state != notAllowedState, \
               Print("ERROR: getTransactionStatus should have indicated a \"state\" of \"{}\" and not \"{}\" but it was \"{}\"".
                     format(expectedState, notAllowedState, state))
        return False

    localState = "LOCALLY_APPLIED"
    inBlockState = "IN_BLOCK"
    irreversibleState = "IRREVERSIBLE"
    unknownState = "UNKNOWN"
    status = []
    state = None
    i = 0
    preInfo = None
    postInfo = None
    transferAmount=10

    # ensuring that prod0's producer is active, which will give sufficient time to identify the transaction as "LOCALLY_APPLIED" before it travels
    # through the chain of nodes to nod0 to be added to a block
    # defproducera -> defproducerb -> defproducerc -> NPN
    prod0.waitForProducer("defproducera", exitOnError=True)
    testNode.transferFunds(cluster.eosioAccount, account1, "{}.0000 {}".format(transferAmount, CORE_SYMBOL), "fund account")
    transId=testNode.getLastSentTransactionId()
    retStatus=testNode.getTransactionStatus(transId)
    state = getState(retStatus)

    assert state == localState, \
        Print("ERROR: getTransactionStatus didn't return \"{}\" state.\n\nstatus: {}".format(localState, json.dumps(retStatus, indent=1)))
    status.append(copy.copy(retStatus))
    startingBlockNum=testNode.getInfo()["head_block_num"]

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

    numTries = 5
    for i in range(0, numTries):
        retStatus=testNode.getTransactionStatus(transId)
        Print("retStatus: {}".format(retStatus))
        state = getState(retStatus)

        if isState(state, inBlockState, allowedState=localState):
            status.append(copy.copy(retStatus))
            break

        Print("Failed to catch status as \"{}\" after {} of {}".format(localState, i + 1, numTries))
        testNode.waitForNextBlock()

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

    retStatus=testNode.getTransactionStatus(transId)
    Print("retStatus: {}".format(retStatus))
    state = getState(retStatus)
    assert state == irreversibleState, \
        Print("ERROR: Successive calls to getTransactionStatus should have resulted in eventual \"{}\" state.\n1st status: {}\n\n2nd status: {}\n\nfinal status: {}".
              format(irreversibleState, json.dumps(status[0], indent=1), json.dumps(status[1], indent=1), json.dumps(retStatus, indent=1)))
    validate(retStatus)

    leeway=4
    assert testNode.waitForBlock(blockNum=startingBlockNum+(successDuration*2),timeout=successDuration+leeway)

    retStatus=testNode.getTransactionStatus(transId)
    Print("retStatus: {}".format(retStatus))
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
