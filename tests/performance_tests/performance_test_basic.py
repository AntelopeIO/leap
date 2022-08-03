#!/usr/bin/env python3

import os
import sys

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from testUtils import Account
from testUtils import Utils
from Cluster import Cluster
from WalletMgr import WalletMgr
from Node import Node
from Node import ReturnType
from TestHelper import TestHelper

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs"})

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = pnodes if args.n < pnodes else args.n
Utils.Debug = args.v
killAll=args.clean_run
dumpErrorDetails=args.dump_error_details
dontKill=args.leave_running
killEosInstances = not dontKill
killWallet=not dontKill
keepLogs=args.keep_logs

# Setup cluster and its wallet manager
walletMgr=WalletMgr(True)
cluster=Cluster(walletd=True)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
try:
    # Kill any existing instances and launch cluster
    TestHelper.printSystemInfo("BEGIN")
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    extraNodeosArgs=' --http-max-response-time-ms 990000 --disable-subjective-api-billing false --plugin eosio::trace_api_plugin --trace-no-abis '
    if cluster.launch(
       pnodes=pnodes,
       totalNodes=total_nodes,
       useBiosBootFile=False,
       topo=topo,
       extraNodeosArgs=extraNodeosArgs) == False:
        errorExit('Failed to stand up cluster.')

    wallet = walletMgr.create('default')
    cluster.populateWallet(2, wallet)
    cluster.createAccounts(cluster.eosioAccount, stakedDeposit=0)
    
    account1Name = cluster.accounts[0].name
    account2Name = cluster.accounts[1].name

    if Utils.Debug: Print(f'Running txn_generator with accounts {account1Name} {account2Name}')
    #Utils.runCmdReturnStr(f'../txn_generator/txn_generator {account1Name} {account2Name}')

    testSuccessful = True
finally:
    TestHelper.shutdown(
        cluster,
        walletMgr,
        testSuccessful,
        killEosInstances,
        killWallet,
        keepLogs,
        killAll,
        dumpErrorDetails
    )

exitCode = 0 if testSuccessful else 1
exit(exitCode)
