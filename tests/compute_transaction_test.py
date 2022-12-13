#!/usr/bin/env python3

import random

from TestHarness import Account, Cluster, ReturnType, TestHelper, Utils, WalletMgr
from core_symbol import CORE_SYMBOL

###############################################################
# compute_transaction_tests
#
# tests to exercise the compute_transaction functionality
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file","--seed"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs"})

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
cluster=Cluster(walletd=True)

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

    # non-producing node
    npnode = cluster.nodes[-1]


    transferAmount="1000.0000 {0}".format(CORE_SYMBOL)

    node.transferFunds(cluster.eosioAccount, account1, transferAmount, "fund account")
    preBalances = node.getEosBalances([account1, account2])
    Print("Starting balances:")
    Print(preBalances)

    Print("Sending read-only transfer")
    trx = {
        "actions": [{"account": "eosio.token","name": "transfer",
        "authorization": [{"actor": "account1","permission": "active"}],
        "data": {"from": "account1","to": "account2","quantity": "1.0001 SYS","memo": "tx1"},
        "compression": "none"}]
    }

    results = node.pushTransaction(trx, opts='--read-only', permissions=account1.name)
    assert(results[0])
    results = node.pushTransaction(trx, opts='--dry-run', permissions=account1.name)
    assert(results[0])
    node.waitForLibToAdvance(30)

    postBalances = node.getEosBalances([account1, account2])
    assert(postBalances == preBalances)

    results = node.pushTransaction(trx, opts='--read-only --skip-sign')
    assert(results[0])
    results = node.pushTransaction(trx, opts='--dry-run --skip-sign')
    assert(results[0])
    node.waitForLibToAdvance(30)

    postBalances = node.getEosBalances([account1, account2])

    assert(postBalances == preBalances)

#   Send through some failing *read only* transactions on a non-producer node
    for x in range(5):
        memo = 'tx-{}'.format(x)
        trx2 = {

            "actions": [{"account": "eosio.token","name": "transfer",
                         "authorization": [{"actor": "account2","permission": "active"}],
                         "data": {"from": "account2","to": "account1","quantity": "10.0001 SYS","memo": memo},
                         "compression": "none"}]
        }

        results = npnode.pushTransaction(trx2, opts="--read-only")
        assert(not results[0])
        results = npnode.pushTransaction(trx2, opts="--dry-run")
        assert(not results[0])

# Verify that no subjective billing was charged
    acct2 = npnode.getAccountSubjectiveInfo("account2")
    assert(acct2["used"] == 0)

    #   Send through some failing *non read-only* transactions on a non-producer node
    for x in range(5):
        memo = 'tx-{}'.format(x)
        trx2 = {

            "actions": [{"account": "eosio.token","name": "transfer",
                         "authorization": [{"actor": "account2","permission": "active"}],
                         "data": {"from": "account2","to": "account1","quantity": "10.0001 SYS","memo": memo},
                         "compression": "none"}]
        }

        results = npnode.pushTransaction(trx2)
        assert(not results[0])

    # Verify that subjective billing *was* charged
    acct2 = npnode.getAccountSubjectiveInfo("account2")
    assert(acct2["used"] > 0)

    # Test that irrelavent signature doesn't break read-only txn
    trx3 = {

        "actions": [{"account": "eosio.token","name": "transfer",
                     "authorization": [{"actor": "account1","permission": "active"},{"actor": "account2","permission": "active"}],
                     "data": {"from": "account1","to": "account2","quantity": "10.0001 SYS","memo": memo},
                     "compression": "none"}]
    }
    results = npnode.pushTransaction(trx3, opts="--read-only")
    assert(results[0])
    results = npnode.pushTransaction(trx3, opts="--dry-run")
    assert(results[0])

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, killEosInstances, killWallet, keepLogs, killAll, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
