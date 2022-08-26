#!/usr/bin/env python3

import os
import sys
import re

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
emptyBlockGoal = 5

def waitForEmptyBlocks(node1):
    emptyBlocks = 0
    while emptyBlocks < emptyBlockGoal:
        headBlock = node1.getHeadBlockNum()
        block = node1.processCurlCmd("chain", "get_block_info", f'{{"block_num":{headBlock}}}', silentErrors=False, exitOnError=True)
        node1.waitForHeadToAdvance()
        if block['transaction_mroot'] == "0000000000000000000000000000000000000000000000000000000000000000":
            emptyBlocks += 1
        else:
            emptyBlocks = 0

def checkTotalTrx():
    total = 0
    f = open("var/lib/node_00/stderr.txt")
    stringResult = re.findall(r'trxs:\s+\d+', f.read())
    for value in stringResult:
        intResult = re.findall(r'\d+', value)
        total += int(intResult[0])
    f.close()
    return total

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs"})

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = max(2, pnodes if args.n < pnodes else args.n)
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
    extraNodeosArgs=' --http-max-response-time-ms 990000 --disable-subjective-api-billing false '
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

    account1PrivKey = cluster.accounts[0].activePrivateKey
    account2PrivKey = cluster.accounts[1].activePrivateKey

    node0 = cluster.getNode(0)
    node1 = cluster.getNode(1)
    info = node0.getInfo()
    chainId = info['chain_id']
    lib_id = info['last_irreversible_block_id']

    testGenerationDurationSec = 60
    targetTps = 1
    transactionsSent = testGenerationDurationSec * targetTps

    waitForEmptyBlocks(node1)
    setupTotal = checkTotalTrx()

    if Utils.Debug: Print(
                            f'Running trx_generator: ./tests/trx_generator/trx_generator  '
                            f'--chain-id {chainId} '
                            f'--last-irreversible-block-id {lib_id} '
                            f'--handler-account {cluster.eosioAccount.name} '
                            f'--accounts {account1Name},{account2Name} '
                            f'--priv-keys {account1PrivKey},{account2PrivKey} '
                            f'--trx-gen-duration {testGenerationDurationSec} '
                            f'--target-tps {targetTps}'
                         )
    Utils.runCmdReturnStr(
                            f'./tests/trx_generator/trx_generator '
                            f'--chain-id {chainId} '
                            f'--last-irreversible-block-id {lib_id} '
                            f'--handler-account {cluster.eosioAccount.name} '
                            f'--accounts {account1Name},{account2Name} '
                            f'--priv-keys {account1PrivKey},{account2PrivKey} '
                            f'--trx-gen-duration {testGenerationDurationSec} '
                            f'--target-tps {targetTps}'
                         )

    waitForEmptyBlocks(node1)
    total = checkTotalTrx()
    assert transactionsSent == total - setupTotal , "Error: Transactions received: %d did not match expected total: %d" % (total - setupTotal, transactionsSent)

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
