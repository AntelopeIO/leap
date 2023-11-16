#!/usr/bin/env python3

import random
from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr

###############################################################
# cleos_set_code_test
#
# Test Cleos "set code" and "set abi" commands
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"--seed"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--keep-logs","--unshared"})

pnodes=1
topo="mesh"
delay=1
total_nodes=1

debug=args.v
seed=args.seed
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(loggingLevel="all", unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)

walletMgr=WalletMgr(True)
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    Print ("producing nodes: %d, non-producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d" % (pnodes, total_nodes-pnodes, topo, delay))

    Print("Stand up cluster")

    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    Utils.Print("Create a test account")
    account = Account("payloadless")
    account.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account, cluster.eosioAccount, buyRAM=100000)

    wasmFile="unittests/test-contracts/payloadless/payloadless.wasm"
    abiFile="unittests/test-contracts/payloadless/payloadless.abi"

    producerNode = cluster.getNode()

    # test "set code" and "set abi"
    assert(producerNode.setCodeOrAbi(account, "code", wasmFile))
    assert(producerNode.setCodeOrAbi(account, "abi", abiFile))

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
