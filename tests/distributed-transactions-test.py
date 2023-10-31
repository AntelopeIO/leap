#!/usr/bin/env python3

import random
import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# distributed-transactions-test
#
# Performs currency transfers between N accounts sent to http endpoints of
# N nodes and verifies, after a steady state is reached, that the accounts
# balances are correct
# if called with --nodes-file it will will load a json description of nodes
# that are already running and run distributed test against them (not
# currently testing this feature)
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
extraArgs = appArgs.add_bool(flag="--speculative", help="Run nodes in read-mode=speculative")
args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file","--seed", "--speculative"
                           ,"--dump-error-details","-v","--leave-running","--keep-logs","--unshared"}, applicationSpecificArgs=appArgs)

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = pnodes if args.n < pnodes else args.n
debug=args.v
nodesFile=args.nodes_file
dontLaunch=nodesFile is not None
seed=args.seed
dumpErrorDetails=args.dump_error_details
speculative=args.speculative

Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(unshared=args.unshared, keepRunning=True if nodesFile is not None else args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    cluster.setWalletMgr(walletMgr)

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
    else:
        Print ("producing nodes: %s, non-producing nodes: %d, topology: %s, delay between nodes launch(seconds): %d" %
               (pnodes, total_nodes-pnodes, topo, delay))

        Print("Stand up cluster")
        extraNodeosArgs = ""
        if speculative:
           extraNodeosArgs = " --read-mode speculative "

        if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay, extraNodeosArgs=extraNodeosArgs) is False:
            errorExit("Failed to stand up eos cluster.")

        Print ("Wait for Cluster stabilization")
        # wait for cluster to start producing blocks
        if not cluster.waitOnClusterBlockNumSync(3):
            errorExit("Cluster never stabilized")

    accountsCount=total_nodes
    walletName="MyWallet-%d" % (random.randrange(10000))
    Print("Creating wallet %s if one doesn't already exist." % walletName)
    walletAccounts=[cluster.defproduceraAccount,cluster.defproducerbAccount]
    if not dontLaunch:
        walletAccounts.append(cluster.eosioAccount)
    wallet=walletMgr.create(walletName, walletAccounts)
    if wallet is None:
        errorExit("Failed to create wallet %s" % (walletName))

    Print ("Populate wallet with %d accounts." % (accountsCount))
    if not cluster.populateWallet(accountsCount, wallet):
        errorExit("Wallet initialization failed.")

    defproduceraAccount=cluster.defproduceraAccount
    defproducerbAccount=cluster.defproducerbAccount
    eosioAccount=cluster.eosioAccount

    Print("Create accounts.")
    if not cluster.createAccounts(eosioAccount):
        errorExit("Accounts creation failed.")

    Print("Spread funds and validate")
    if not cluster.spreadFundsAndValidate(10):
        errorExit("Failed to spread and validate funds.")

    print("Funds spread validated")

    if not args.leave_running:
        for node in cluster.getAllNodes():
            node.kill(signal.SIGTERM)
    else:
        print("NOTE: Skip killing nodes, block log verification will be limited")

    cluster.compareBlockLogs()

    # verify only one start block per block unless interrupted
    for node in cluster.getAllNodes():
        if not node.verifyOnlyOneStartingBlock():
            errorExit("Found more than one Starting block in logs")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
