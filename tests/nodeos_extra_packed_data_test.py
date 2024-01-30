#!/usr/bin/env python3

import json
import copy

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys
from TestHarness.Cluster import PFSetupPolicy
from TestHarness.TestHelper import AppArgs

###############################################################
# nodeos_extra_packed_data_test
#
# Tests nodeos accepts trx with extra data packed at the end.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit
cmdError=Utils.cmdError

args = TestHelper.parse_args({"--host","--port","-p","--defproducera_prvt_key","--defproducerb_prvt_key"
                              ,"--dump-error-details","--dont-launch","--keep-logs","-v","--leave-running"
                              ,"--sanity-test","--wallet-port","--unshared"})
server=args.host
port=args.port
debug=args.v
defproduceraPrvtKey=args.defproducera_prvt_key
defproducerbPrvtKey=args.defproducerb_prvt_key
dumpErrorDetails=args.dump_error_details
dontLaunch=args.dont_launch
pnodes=args.p
totalNodes=pnodes+1
sanityTest=args.sanity_test
walletPort=args.wallet_port

Utils.Debug=debug
localTest=True if server == TestHelper.LOCAL_HOST else False
cluster=Cluster(host=server, 
                port=port, 
                defproduceraPrvtKey=defproduceraPrvtKey, 
                defproducerbPrvtKey=defproducerbPrvtKey,
                unshared=args.unshared,
                keepRunning=args.leave_running,
                keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
dontBootstrap=sanityTest # intent is to limit the scope of the sanity test to just verifying that nodes can be started

WalletdName=Utils.EosWalletName
ClientName="cleos"
timeout = .5 * 12 * 2 + 60 # time for finalization with 1 producer + 60 seconds padding
Utils.setIrreversibleTimeout(timeout)

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)
    Print("SERVER: %s" % (server))
    Print("PORT: %d" % (port))

    if localTest and not dontLaunch:
        Print("Stand up cluster")
        specificExtraNodeosArgs = {}
        associatedNodeLabels = {}
        if pnodes > 1:
            specificExtraNodeosArgs[pnodes - 1] = ""
        if pnodes > 3:
            specificExtraNodeosArgs[pnodes - 2] = ""

        if cluster.launch(totalNodes=totalNodes, 
                          pnodes=pnodes,
                          dontBootstrap=dontBootstrap,
                          activateIF=True,
                          specificExtraNodeosArgs=specificExtraNodeosArgs,
                          associatedNodeLabels=associatedNodeLabels) is False:
            cmdError("launcher")
            errorExit("Failed to stand up eos cluster.")
    else:
        Print("Collecting cluster info.")
        cluster.initializeNodes(defproduceraPrvtKey=defproduceraPrvtKey, defproducerbPrvtKey=defproducerbPrvtKey)
        Print("Stand up %s" % (WalletdName))
        print("Stand up walletd")
        if walletMgr.launch() is False:
            cmdError("%s" % (WalletdName))
            errorExit("Failed to stand up eos walletd.")
    
    if sanityTest:
        testSuccessful=True
        exit(0)

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    accounts=createAccountKeys(2)
    if accounts is None:
        errorExit("FAILURE - create keys")
    testeraAccount=accounts[0]
    testeraAccount.name="testerxxxxxa"
    testerbAccount=accounts[1]
    testerbAccount.name="testerxxxxxb"

    testWalletName="test"
    Print("Creating wallet \"%s\"" % (testWalletName))
    walletAccounts=copy.deepcopy(cluster.defProducerAccounts)
    if dontLaunch:
        del walletAccounts["eosio"]
    testWallet = walletMgr.create(testWalletName, walletAccounts.values())

    Print("Wallet \"%s\" password=%s." % (testWalletName, testWallet.password.encode("utf-8")))

    all_acc = accounts + list( cluster.defProducerAccounts.values() )
    for account in all_acc:
        Print("Importing keys for account %s into wallet %s." % (account.name, testWallet.name))
        if not walletMgr.importKey(account, testWallet):
            cmdError("%s wallet import" % (ClientName))
            errorExit("Failed to import key for account %s" % (account.name))
    
    node=cluster.getNode(0)
    nonProdNode=cluster.getAllNodes()[-1]

    Print("Create new account %s via %s" % (testeraAccount.name, cluster.defproduceraAccount.name))
    transId=node.createInitializeAccount(testeraAccount, cluster.defproduceraAccount, stakedDeposit=0, waitForTransBlock=False, exitOnError=True)

    Print("Create new account %s via %s" % (testerbAccount.name, cluster.defproduceraAccount.name))
    transId=nonProdNode.createInitializeAccount(testerbAccount, cluster.defproduceraAccount, stakedDeposit=0, waitForTransBlock=True, exitOnError=True)

    Print("Validating accounts after user accounts creation")
    accounts=[testeraAccount, testerbAccount]
    cluster.validateAccounts(accounts)

    trxNumber = 2
    postedTrxs = []
    for i in range(trxNumber):
        if i == 1:
            node = cluster.getNode(pnodes - 1)
        
        transferAmount="{0}.0 {1}".format(i + 1, CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.defproduceraAccount.name, testeraAccount.name))
        trx = node.transferFunds(cluster.defproduceraAccount, testeraAccount, transferAmount, "test transfer", dontSend=True)

        cmdDesc = "convert pack_transaction"
        cmd     = "%s --pack-action-data '%s'" % (cmdDesc, json.dumps(trx))
        exitMsg = "failed to pack transaction: %s" % (trx)
        packedTrx = node.processCleosCmd(cmd, cmdDesc, silentErrors=False, exitOnError=True, exitMsg=exitMsg)

        packed_trx_param = packedTrx["packed_trx"]
        if packed_trx_param is None:
            cmdError("packed_trx is None. Json: %s" % (packedTrx))
            errorExit("Can't find packed_trx in packed json")
        
        #adding random trailing data
        packedTrx["packed_trx"] = packed_trx_param + "00000000"

        exitMsg = "failed to send packed transaction: %s" % (packedTrx)
        sentTrx = node.processUrllibRequest("chain", "send_transaction", packedTrx, silentErrors=False, exitOnError=True, exitMsg=exitMsg)
        Print("sent transaction json: %s" % (sentTrx))
        trx_id = sentTrx["payload"]["transaction_id"]
        postedTrxs.append(trx_id)

    assert len(postedTrxs) == trxNumber, Print("posted transactions number %d doesn't match %d" % (len(postedTrxs), trxNumber))
    
    for trxId in postedTrxs:        
        attemptCnt = 10
        trxBlock = None
        while trxBlock is None and attemptCnt > 0:
            trxBlock = node.getBlockNumByTransId(trx_id)
            attemptCnt = attemptCnt - 1
        
        assert trxBlock, Print("Transaction %s wasn't posted" % (trx_id))

        for cur_node in cluster.getNodes():

            timeout = (12 * pnodes) * 1.3
            passed = cur_node.waitForBlock(trxBlock + 12 * pnodes, timeout)
            assert passed, Print("Node %d not advanced head block within timeout")
    
    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
