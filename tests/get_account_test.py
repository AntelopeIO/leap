#!/usr/bin/env python3

import random

from TestHarness import Account, Cluster, ReturnType, TestHelper, Utils, WalletMgr

###############################################################
# get_account_test
#
# integration test for cleos get account
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file","--seed"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--keep-logs","--unshared"})

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = pnodes if args.n < pnodes else args.n
debug=args.v
nodesFile=args.nodes_file
dontLaunch=nodesFile is not None
seed=args.seed
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=True if nodesFile is not None else args.leave_running, keepLogs=args.keep_logs)

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

        print("Stand up walletd")
        if walletMgr.launch() is False:
            errorExit("Failed to stand up keosd.")

    Print ("producing nodes: %s, non-producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d" % (pnodes, total_nodes-pnodes, topo, delay))

    Print("Stand up cluster")
    extraNodeosArgs=" --http-max-response-time-ms 990000 --disable-subjective-api-billing false "
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, activateIF=True, topo=topo, delay=delay,extraNodeosArgs=extraNodeosArgs ) is False:
       errorExit("Failed to stand up eos cluster.")

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
     errorExit("Cluster never stabilized")

    Print("Creating account1")
    account1 = Account('account1')
    account1.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account1.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account1, cluster.eosioAccount, stakedDeposit=1000)

    Print("Creating account2")
    account2 = Account('account2')
    account2.ownerPublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    account2.activePublicKey = EOSIO_ACCT_PUBLIC_DEFAULT_KEY
    cluster.createAccountAndVerify(account2, cluster.eosioAccount, stakedDeposit=1000, stakeCPU=1)

    Print("Validating accounts after bootstrap")
    cluster.validateAccounts([account1, account2])

    node = cluster.getNode()

    Print(node.getEosAccount("account1"))

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
