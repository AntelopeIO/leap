#!/usr/bin/env python3

import json

from TestHarness import Account, Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# light_validation_sync_test
#
# Test message sync for light validation mode.
# 
# This test creates a producer node and a light validation node.
# Pushes a transaction with context free data to the producer
# node and check the pushed transaction can be synched to
# the validation node.
#
###############################################################

# Parse command line arguments
args = TestHelper.parse_args({"-v","--activate-if","--dump-error-details","--leave-running","--keep-logs","--unshared"})
Utils.Debug = args.v
dumpErrorDetails=args.dump_error_details
activateIF=args.activate_if
dontKill=args.leave_running
keepLogs=args.keep_logs

walletMgr=WalletMgr(True)
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
try:
    TestHelper.printSystemInfo("BEGIN")
    assert cluster.launch(
        pnodes=1,
        prodCount=1,
        totalProducers=1,
        totalNodes=2,
        loadSystemContract=False,
        activateIF=activateIF,
        specificExtraNodeosArgs={
            1:"--validation-mode light"})

    producerNode = cluster.getNode(0)
    validationNode = cluster.getNode(1)

    # Create a transaction to create an account
    Utils.Print("create a new account payloadless from the producer node")
    payloadlessAcc = Account("payloadless")
    payloadlessAcc.ownerPublicKey = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
    payloadlessAcc.activePublicKey = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
    producerNode.createAccount(payloadlessAcc, cluster.eosioAccount)


    contractDir="unittests/test-contracts/payloadless"
    wasmFile="payloadless.wasm"
    abiFile="payloadless.abi"
    Utils.Print("Publish payloadless contract")
    trans = producerNode.publishContract(payloadlessAcc, contractDir, wasmFile, abiFile, waitForTransBlock=True)

    trx = {
        "actions": [{"account": "payloadless", "name": "doit", "authorization": [{
          "actor": "payloadless", "permission": "active"}], "data": ""}],
        "context_free_actions": [{"account": "payloadless", "name": "doit", "data": ""}],
        "context_free_data": ["a1b2c3", "1a2b3c"],
    } 

    cmd = "push transaction '{}' -p payloadless".format(json.dumps(trx))
    trans = producerNode.processCleosCmd(cmd, cmd, silentErrors=False)
    assert trans, "Failed to push transaction with context free data"
    
    cfTrxBlockNum = int(trans["processed"]["block_num"])
    cfTrxId = trans["transaction_id"]

    # Wait until the block where create account is executed to become irreversible 
    def isBlockNumIrr():
        return validationNode.getIrreversibleBlockNum() >= cfTrxBlockNum
    Utils.waitForBool(isBlockNumIrr, timeout=30, sleepTime=0.1)
    
    Utils.Print("verify the account payloadless from validation node")
    cmd = "get account -j payloadless"
    trans = validationNode.processCleosCmd(cmd, cmd, silentErrors=False)
    assert trans["account_name"], "Failed to get the account payloadless"

    Utils.Print("verify the context free transaction from validation node")
    cmd = "get transaction_trace " + cfTrxId
    trans = validationNode.processCleosCmd(cmd, cmd, silentErrors=False)
    assert trans, "Failed to get the transaction with context free data from the light validation node"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
