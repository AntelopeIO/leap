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
from dataclasses import dataclass
from typing import List

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30
emptyBlockGoal = 5

@dataclass
class blockData():
    blockId: str = ""
    blockNum: int = 0
    transactions: int = 0
    net: int = 0
    cpu: int = 0
    elapsed: int = 0
    time: int = 0
    latency: int = 0

class chainData():
    def __init__(self):
        self.blockLog = []
    def total(self, attrname):
        total = 0
        for n in self.blockLog:
            total += getattr(n, attrname)
        return total
    def __str__(self):
        return "Chain transactions: %d\n Chain cpu: %d\n Chain net: %d\n Chain elapsed: %d\n Chain time: %d\n Chain latency: %d" %\
         (self.total("transactions"), self.total("net"), self.total("cpu"), self.total("elapsed"), self.total("time"), self.total("latency"))

class chainsContainer():
    def __init__(self):
        self.preData = chainData()
        self.totalData = chainData()
        self.startBlock = 0
        self.ceaseBlock = 0
    def total(self, attrname):
        return self.totalData.total(attrname) - self.preData.total(attrname)
    def __str__(self):
        return "Starting block %d ending block %d\n Total transactions: %d\n Total cpu: %d\nTotal net: %d\nTotal elapsed: %d\nTotal time: %d\nTotal latency: %d" %\
         (self.startBlock, self.ceaseBlock, self.total("transactions"), self.total("net"), self.total("cpu"), self.total("elapsed"), self.total("time"), self.total("latency"))

def waitForEmptyBlocks(node):
    emptyBlocks = 0
    while emptyBlocks < emptyBlockGoal:
        headBlock = node.getHeadBlockNum()
        block = node.processCurlCmd("chain", "get_block_info", f'{{"block_num":{headBlock}}}', silentErrors=False, exitOnError=True)
        node.waitForHeadToAdvance()
        if block['transaction_mroot'] == "0000000000000000000000000000000000000000000000000000000000000000":
            emptyBlocks += 1
        else:
            emptyBlocks = 0
    return node.getHeadBlockNum()

def fetchStats(total):
    with open("var/lib/node_01/stderr.txt") as f:
        trxResult = re.findall(r'Received block ([0-9a-fA-F]*).* #(\d+) .*trxs: (\d+),.*, net: (\d+), cpu: (\d+), elapsed: (\d+), time: (\d+), latency: (-?\d+) ms', f.read())
        for value in trxResult:
            # print("Creating block data using ", value.group(1), value.group(2), value.group(3), value.group(4), value.group(5), value.group(6), value.group(7), value.group(8))
            total.blockLog.append(blockData(value[0], int(value[1]), int(value[2]), int(value[3]), int(value[4]), int(value[5]), int(value[6]), int(value[7])))

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

    producerNode = cluster.getNode(0)
    validationNode = cluster.getNode(1)
    info = producerNode.getInfo()
    chainId = info['chain_id']
    lib_id = info['last_irreversible_block_id']

    testGenerationDurationSec = 60
    targetTps = 1
    transactionsSent = testGenerationDurationSec * targetTps
    cont = chainsContainer()

    # Get stats prior to transaction generation
    cont.startBlock = waitForEmptyBlocks(validationNode)
    fetchStats(cont.preData)

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
    # Get stats after transaction generation stops
    cont.ceaseBlock = waitForEmptyBlocks(validationNode) - emptyBlockGoal
    fetchStats(cont.totalData)

    print(cont.preData)
    print(cont.totalData)
    print(cont)
    assert transactionsSent == cont.total("transactions") , "Error: Transactions received: %d did not match expected total: %d" % (cont.total("transactions"), transactionsSent)

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
