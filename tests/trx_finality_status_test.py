#!/usr/bin/env python3

import copy
import time
import json
import os
import signal
import subprocess

from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType
from TestHarness.TestHelper import AppArgs

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
    successDuration = 60
    failure_duration = 40
    extraNodeosArgs=" --transaction-finality-status-max-storage-size-gb 1 " + \
                   f"--transaction-finality-status-success-duration-sec {successDuration} --transaction-finality-status-failure-duration-sec {failure_duration}"
    extraNodeosArgs+=" --http-max-response-time-ms 990000"
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
    Print(f"prod0 info={json.dumps(prod0Info, indent=1)}")
    Print(f"test node info={json.dumps(nodeInfo, indent=1)}")

    def getState(status):
        assert status is not None, "ERROR: getTransactionStatus failed to return any status"
        assert "state" in status, \
            f"ERROR: getTransactionStatus returned a status object that didn't have a \"state\" field. state: {json.dumps(status, indent=1)}"
        return status["state"]

    def isState(state, expectedState, allowedState=None, notAllowedState=None):
        if state == expectedState:
            return True
        assert allowedState is None or state == allowedState, \
               f"ERROR: getTransactionStatus should have indicated a \"state\" of \"{expectedState}\" or \"{allowedState}\" but it was \"{state}\""
        assert notAllowedState is None or state != notAllowedState, \
               f"ERROR: getTransactionStatus should have indicated a \"state\" of \"{expectedState}\" and not \"{notAllowedState}\" but it was \"{state}\""
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

    # Ensuring that prod0's producer is active, which will give more time to identify the transaction as "LOCALLY_APPLIED" before it travels
    # through the chain of nodes to node0 to be added to a block. It is still possible to hit the end of a block and state be IN_BLOCK here.
    # defproducera -> defproducerb -> defproducerc -> NPN
    prod0.waitForProducer("defproducera", exitOnError=True)
    testNode.transferFunds(cluster.eosioAccount, account1, f"{transferAmount}.0000 {CORE_SYMBOL}", "fund account")
    transId=testNode.getLastTrackedTransactionId()
    retStatus=testNode.getTransactionStatus(transId)
    state = getState(retStatus)

    assert (state == localState or state == inBlockState), \
        f"ERROR: getTransactionStatus didn't return \"{localState}\" or \"{inBlockState}\" state.\n\nstatus: {json.dumps(retStatus, indent=1)}"
    status.append(copy.copy(retStatus))
    startingBlockNum=testNode.getInfo()["head_block_num"]

    def validateTrxState(status, present):
        bnPresent = "block_number" in status
        biPresent = "block_id" in status
        btPresent = "block_timestamp" in status
        desc = "" if present else " not"
        group = bnPresent and biPresent and btPresent if present else not bnPresent and not biPresent and not btPresent
        assert group, \
            f"ERROR: getTransactionStatus should{desc} contain \"block_number\", \"block_id\", or \"block_timestamp\" since state was \"{getState(status)}\".\nstatus: {json.dumps(status, indent=1)}"

    present = True if state == inBlockState else False
    validateTrxState(status[0], present)

    def validate(status, knownTrx=True):
        assert "head_number" in status and "head_id" in status and "head_timestamp" in status, \
            f"ERROR: getTransactionStatus is missing \"head_number\", \"head_id\", and \"head_timestamp\".\nstatus: {json.dumps(status, indent=1)}"

        assert "irreversible_number" in status and "irreversible_id" in status and "irreversible_timestamp" in status, \
            f"ERROR: getTransactionStatus is missing \"irreversible_number\", \"irreversible_id\", and \"irreversible_timestamp\".\nstatus: {json.dumps(status, indent=1)}"

        assert not knownTrx or "expiration" in status, \
            f"ERROR: getTransactionStatus is missing \"expiration\".\nstatus: {json.dumps(status, indent=1)}"

        assert "earliest_tracked_block_id" in status and "earliest_tracked_block_number" in status, \
            f"ERROR: getTransactionStatus is missing \"earliest_tracked_block_id\" and \"earliest_tracked_block_number\".\nstatus: {json.dumps(status, indent=1)}"

    validate(status[0])

    # since the transaction is traveling opposite to the flow of blocks (prodNodes[0] then prodNodes[1] etc) once the flow of blocks has progressed to testNode's
    # nearest neighbor, it will at least be in its block if not one of the earlier ones
    testNode.waitForProducer("defproducerb")
    testNode.waitForProducer("defproducerc")

    status.append(testNode.getTransactionStatus(transId))
    state = getState(status[1])
    assert state == inBlockState, f"ERROR: getTransactionStatus never returned a \"{inBlockState}\" state"

    validateTrxState(status[1], present=True)

    validate(status[1])

    assert status[0]["head_number"] < status[1]["head_number"], \
        f"ERROR: Successive calls to getTransactionStatus should return increasing values for \"head_number\".\n1st status: {json.dumps(status[0], indent=1)}\n\n2nd status: {json.dumps(status[1], indent=1)}"

    block_number = status[1]["block_number"]
    assert testNode.waitForIrreversibleBlock(block_number, timeout=180), \
        f"ERROR: Failed to advance irreversible block to {block_number}. \nAPI Node info: {json.dumps(prod0.getInfo(), indent=1)}\n\nProducer info: {json.dumps(testNode.getInfo(), indent=1)}"

    retStatus=testNode.getTransactionStatus(transId)
    state = getState(retStatus)
    assert state == irreversibleState, \
        f"ERROR: Successive calls to getTransactionStatus should have resulted in eventual \"{irreversibleState}\" state." + \
        f"\n1st status: {json.dumps(status[0], indent=1)}\n\n2nd status: {json.dumps(status[1], indent=1)}" + \
        f"\n\nfinal status: {json.dumps(retStatus, indent=1)}"
    validate(retStatus)

    leeway=4
    assert testNode.waitForBlock(blockNum=startingBlockNum+(successDuration*2),timeout=successDuration+leeway)

    recentBlockNum=testNode.getBlockNum()
    retStatus=testNode.getTransactionStatus(transId)
    state = getState(retStatus)
    assert state == unknownState, \
        f"ERROR: Calling getTransactionStatus after the success_duration should have resulted in an \"{irreversibleState}\" state.\nstatus: {json.dumps(retStatus, indent=1)}"
    validateTrxState(retStatus, present=False)
    validate(retStatus, knownTrx=False)
    assert recentBlockNum <= retStatus["head_number"], \
        "ERROR: Expected call to getTransactionStatus to return increasing values for \"head_number\" beyond previous known recent block number."

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
