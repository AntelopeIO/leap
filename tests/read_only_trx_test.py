#!/usr/bin/env python3

import random

from TestHarness import Account, Cluster, ReturnType, TestHelper, Utils, WalletMgr
from core_symbol import CORE_SYMBOL

###############################################################
# send_read_only_transaction_tests
#
# Tests to exercise the send_read_only_transaction RPC endpoint functionality.
# More internal tests are in unittest/read_only_trx_tests.cpp
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file","--seed"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs","--unshared"}, applicationSpecificArgs=appArgs)

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = pnodes if args.n < pnodes else args.n
debug=args.v
nodesFile=args.nodes_file
dontLaunch=nodesFile is not None
seed=args.seed
dontKill=args.leave_running
dumpErrorDetails=args.dump_error_details
killAll=args.clean_run
keepLogs=args.keep_logs

killWallet=not dontKill
killEosInstances=not dontKill
if nodesFile is not None:
    killEosInstances=False

Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(walletd=True,unshared=args.unshared)

walletMgr=WalletMgr(True)
EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

try:
    if dontLaunch: # run test against remote cluster
        jsonStr=None
        with open(nodesFile, "r") as f:
            jsonStr=f.read()
        if not cluster.initializeNodesFromJson(jsonStr):
            errorExit("Failed to initilize nodes from Json string.")
        total_nodes=len(cluster.getNodes())

        walletMgr.killall(allInstances=killAll)
        walletMgr.cleanup()
        print("Stand up walletd")
        if walletMgr.launch() is False:
            errorExit("Failed to stand up keosd.")
        else:
            cluster.killall(allInstances=killAll)
            cluster.cleanup()

    Print ("producing nodes: %s, non-producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d" % (pnodes, total_nodes-pnodes, topo, delay))

    Print("Stand up cluster")
    extraNodeosArgs=" --http-max-response-time-ms 990000 --disable-subjective-api-billing false "
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay,extraNodeosArgs=extraNodeosArgs ) is False:
        errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")

    producerNode = cluster.getNode()
    apiNode = cluster.nodes[-1]

    Utils.Print("create test accounts")
    testAccountName = "test"
    testAccount = Account(testAccountName)
    testAccount.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    testAccount.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(testAccount, cluster.eosioAccount, buyRAM=500000) # 95632 bytes required for test contract

    userAccountName = "user"
    userAccount = Account(userAccountName)
    userAccount.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    userAccount.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(userAccount, cluster.eosioAccount)

    noAuthTableContractDir="unittests/test-contracts/no_auth_table"
    noAuthTableWasmFile="no_auth_table.wasm"
    noAuthTableAbiFile="no_auth_table.abi"
    Utils.Print("Publish no_auth_table contract")
    producerNode.publishContract(testAccount, noAuthTableContractDir,noAuthTableWasmFile, noAuthTableAbiFile, waitForTransBlock=True)

    def sendTransaction(action, data, auth=[], opts=None):
        trx = {
           "actions": [{
              "account": testAccountName,
              "name": action,
              "authorization": auth,
              "data": data
          }]
        }
        return apiNode.pushTransaction(trx, opts)

    Print("Insert a user")
    results = sendTransaction('insert', {"user": userAccountName, "id": 1, "age": 10}, auth=[{"actor": userAccountName, "permission":"active"}])
    assert(results[0])
    apiNode.waitForTransactionInBlock(results[1]['transaction_id'])

    # verify the return value (age) from read-only is the same as created.
    Print("Send a read-only Get transaction to verify previous Insert")
    results = sendTransaction('getage', {"user": userAccountName}, opts='--read')
    assert(results[0])
    assert(results[1]['processed']['action_traces'][0]['return_value_data'] == 10)

    # verify non-read-only modification works
    Print("Send a non-read-only Modify transaction")
    results = sendTransaction('modify', {"user": userAccountName, "age": 25},        auth=[{"actor": userAccountName, "permission": "active"}])
    assert(results[0])
    apiNode.waitForTransactionInBlock(results[1]['transaction_id'])

    # verify 'cleos push action getage "user": "user" --read' works
    Print("Send a read-only Get action")
    results = apiNode.pushMessage(testAccountName, 'getage', "{{\"user\": \"{}\"}}".format(userAccountName), opts='--read');
    assert(results[0])
    assert(results[1]['processed']['action_traces'][0]['return_value_data'] == 25)

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, killEosInstances, killWallet, keepLogs, killAll, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
