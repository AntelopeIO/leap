#!/usr/bin/env python3

import time
import random

from TestHarness import Account, Cluster, ReturnType, TestHelper, Utils, WalletMgr

###############################################################
# subjective-billing-test
#
# tests to verify subjective billing behavior
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

    specificArgs = {"2": "--disable-subjective-account-billing=account1",
                    "3": "--subjective-account-decay-time-minutes=1" }

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay, activateIF=True,
                      extraNodeosArgs=" --http-max-response-time-ms 990000 --disable-subjective-api-billing false ",
                      specificExtraNodeosArgs=specificArgs ) is False:
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
    cluster.validateAccounts([account1, account2], testSysAccounts=False)

    node = cluster.getNode()

    # api node configured with whitelist
    wlnode = cluster.nodes[2]

    # api node configured with decay of 30 min
    fdnode = cluster.nodes[3]

    preBalances = node.getEosBalances([account1, account2])
    Print("Starting balances:")
    Print(preBalances)

    #   Send through some failing transactions on 1 min decay node
    for x in range(5):
        memo = 'tx-{}'.format(x)
        txn = {

            "actions": [{"account": "eosio.token","name": "transfer",
                         "authorization": [{"actor": "account1","permission": "active"}],
                         "data": {"from": "account1","to": "account2","quantity": "100000.0001 SYS","memo": memo},
                         "compression": "none"}]
        }

        results = fdnode.pushTransaction(txn)
        assert(not results[0])

    # Get subjective billing.  We'll check on this later.
    decaySubjectiveBilling = fdnode.getAccountSubjectiveInfo("account1")
    Print('Subjective billing used from fast decay node {}'.format(decaySubjectiveBilling["used"]))


    #   Send through some failing transactions on a non-whitelisted account
    Print('Verifying non-whitelisted account...')
    subjectiveBilling = wlnode.getAccountSubjectiveInfo("account2")
    Print('Initial Subjective billing used: {}'.format(subjectiveBilling["used"]))

    for x in range(5):
        memo = 'tx-{}'.format(x)
        txn = {

            "actions": [{"account": "eosio.token","name": "transfer",
                         "authorization": [{"actor": "account2","permission": "active"}],
                         "data": {"from": "account2","to": "account1","quantity": "100000.0001 SYS","memo": memo},
                         "compression": "none"}]
        }

        results = wlnode.pushTransaction(txn)
        assert(not results[0])

    # Verify that subjective billing was charged
    subjectiveBilling = wlnode.getAccountSubjectiveInfo("account2")
    Print(subjectiveBilling)
    Print(subjectiveBilling["used"])
    assert(subjectiveBilling["used"] > 0)

    #   Send through some failing transactions on a whitelisted account
    Print("Verifying whitelisted account...")
    subjectiveBilling = wlnode.getAccountSubjectiveInfo("account1")
    Print('Initial Subjective billing used: {}'.format(subjectiveBilling["used"]))
    for x in range(5):
        memo = 'tx-{}'.format(x)
        txn = {

            "actions": [{"account": "eosio.token","name": "transfer",
                         "authorization": [{"actor": "account1","permission": "active"}],
                         "data": {"from": "account1","to": "account2","quantity": "100000.0001 SYS","memo": memo},
                         "compression": "none"}]
        }

        results = wlnode.pushTransaction(txn)
        assert(not results[0])

    # Verify that no subjective billing was not charged
    subjectiveBilling = wlnode.getAccountSubjectiveInfo("account1")
    Print(subjectiveBilling["used"])
    assert(subjectiveBilling["used"] == 0)


    # Sleep for 1 min
    time.sleep(60)

    # Verify subjective decay
    acct1 = fdnode.getAccountSubjectiveInfo("account1")
    originalUsed = decaySubjectiveBilling["used"]
    finalUsed = acct1["used"]

    Print('Original Fast decay node subjective billing: {}'.format(decaySubjectiveBilling["used"]))
    Print('End decay node subjective billing: {}'.format(acct1["used"]))
    assert(finalUsed == 0)

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
