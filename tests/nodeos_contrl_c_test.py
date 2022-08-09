#!/usr/bin/env python3

from core_symbol import CORE_SYMBOL
from testUtils import Utils
from datetime import datetime
from datetime import timedelta
import time
from Cluster import Cluster
import json
from WalletMgr import WalletMgr
from Node import Node
from TestHelper import TestHelper

import signal

###############################################################
# nodeos_contrl_c_lr_test
#
# This test sets up one producing nodes and one "bridge" node using test_control_api_plugin. We send
# transactions to the bridge node. After the accounts are initialized and allocated with symbols, we
# kill the producing node. Then we flood hundreds transactions to the bridge node. At the end, we
# kill the bridge. It tests that no crashes happen when the nodes are killed.
#
###############################################################

Print = Utils.Print
errorExit=Utils.errorExit

args = TestHelper.parse_args({"--wallet-port", "-v"})

cluster=Cluster(walletd=True)
killAll=True
totalProducerNodes=2
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes
maxActiveProducers=2
totalProducers=maxActiveProducers
walletPort=args.wallet_port
walletMgr=WalletMgr(True, port=walletPort)
producerEndpoint = '127.0.0.1:8888'
httpServerAddress = '127.0.0.1:8889'

testSuccessful=False

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)
    cluster.killall(allInstances=killAll)
    cluster.cleanup()

    specificExtraNodeosArgs = {}
    # producer nodes will be mapped to 0 through totalProducerNodes-1, so the number totalProducerNodes will be the non-producing node
    specificExtraNodeosArgs[totalProducerNodes] = "--plugin eosio::producer_plugin --plugin eosio::chain_api_plugin --plugin eosio::http_plugin "
    "--plugin eosio::txn_test_gen_plugin --plugin eosio::producer_api_plugin "
    extraNodeosArgs = " --plugin eosio::trace_api_plugin --trace-no-abis "
    extraNodeosArgs+= " --http-max-response-time-ms 990000"

    # ***   setup topogrophy   ***

    # "bridge" shape connects defprocera through defproducerk (in node0) to each other and defproducerl through defproduceru (in node01)
    # and the only connection between those 2 groups is through the bridge node

    if cluster.launch(prodCount=1, topo="bridge", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducers,
                      useBiosBootFile=False, specificExtraNodeosArgs=specificExtraNodeosArgs,
                      extraNodeosArgs=extraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")
    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    prodNode = cluster.getNode(0)
    nonProdNode = cluster.getNode(1)

    accounts=cluster.createAccountKeys(2)
    if accounts is None:
        Utils.errorExit("FAILURE - create keys")

    accounts[0].name="tester111111"
    accounts[1].name="tester222222"

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, [cluster.eosioAccount,accounts[0],accounts[1]])

    # create accounts via eosio as otherwise a bid is needed
    for account in accounts:
        Print("Create new account %s via %s" % (account.name, cluster.eosioAccount.name))
        trans=nonProdNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=True, stakeNet=1000, stakeCPU=1000, buyRAM=1000, exitOnError=True)
        transferAmount="100000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        nonProdNode.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=True)
        trans=nonProdNode.delegatebw(account, 20000000.0000, 20000000.0000, waitForTransBlock=True, exitOnError=True)

    testSuccessful = prodNode.kill(signal.SIGTERM)
    if not testSuccessful:
        TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=True, killWallet=True, keepLogs=True, cleanRun=True, dumpErrorDetails=True)
        errorExit("Failed to kill the producer node")

    #Reset test success flag for next check
    testSuccessful=False

    for amt in range(1, 500, 1):
        xferAmount = Node.currencyIntToStr(amt, CORE_SYMBOL)
        nonProdNode.transferFundsAsync(accounts[0], accounts[1], xferAmount, "test transfer", exitOnError=False)

    testSuccessful = nonProdNode.kill(signal.SIGTERM)

    if not testSuccessful:
        TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=True, killWallet=True, keepLogs=True, cleanRun=True, dumpErrorDetails=True)
        errorExit("Failed to kill the seed node")

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=True, killEosInstances=True, killWallet=True, keepLogs=True, cleanRun=True, dumpErrorDetails=True)

errorCode = 0 if testSuccessful else 1
exit(errorCode)