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

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30
emptyBlockGoal = 5

@dataclass
class blockData():
    partialBlockId: str = ""
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
        self.startBlock = 0
        self.ceaseBlock = 0
        self.totalTransactions = 0
        self.totalNet = 0
        self.totalCpu = 0
        self.totalElapsed = 0
        self.totalTime = 0
        self.totalLatency = 0
    def updateTotal(self, transactions, net, cpu, elapsed, time, latency):
        self.totalTransactions += transactions
        self.totalNet += net
        self.totalCpu += cpu
        self.totalElapsed += elapsed
        self.totalTime += time
        self.totalLatency += latency
    def __str__(self):
        return (f"Starting block: {self.startBlock}\nEnding block:{self.ceaseBlock}\nChain transactions: {self.totalTransactions}\n"
         f"Chain cpu: {self.totalNet}\nChain net: {self.totalCpu}\nChain elapsed: {self.totalElapsed}\nChain time: {self.totalTime}\nChain latency: {self.totalLatency}")

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
        blockResult = re.findall(r'Received block ([0-9a-fA-F]*).* #(\d+) .*trxs: (\d+),.*, net: (\d+), cpu: (\d+), elapsed: (\d+), time: (\d+), latency: (-?\d+) ms', f.read())
        for value in blockResult:
            total.blockLog.append(blockData(value[0], int(value[1]), int(value[2]), int(value[3]), int(value[4]), int(value[5]), int(value[6]), int(value[7])))
            if int(value[1]) in range(total.startBlock, total.ceaseBlock):
                total.updateTotal(int(value[2]), int(value[3]), int(value[4]), int(value[5]), int(value[6]), int(value[7]))

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
    data = chainData()

    data.startBlock = waitForEmptyBlocks(validationNode)

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
    data.ceaseBlock = waitForEmptyBlocks(validationNode) - emptyBlockGoal
    fetchStats(data)

    print(data)
    assert transactionsSent == data.totalTransactions , f"Error: Transactions received: {data.totalTransactions} did not match expected total: {transactionsSent}"

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
