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

class blockData():
    def __init__(self):
        self.transactions = 0
        self.cpu = 0
        self.net = 0
        self.elapsed = 0
        self.time = 0
        self.latency = 0

class chainData():
    def __init__(self):
        self.blockLog = []
    def sum_transactions(self):
        total = 0
        for block in self.blockLog:
            total += block.transactions
        return total
    def sum_cpu(self):
        total = 0
        for block in self.blockLog:
            total += block.cpu
        return total
    def sum_net(self):
        total = 0
        for block in self.blockLog:
            total += block.net
        return total
    def sum_elapsed(self):
        total = 0
        for block in self.blockLog:
            total += block.elapsed
        return total
    def sum_time(self):
        total = 0
        for block in self.blockLog:
            total += block.time
        return total
    def sum_latency(self):
        total = 0
        for block in self.blockLog:
            total += block.latency
        return total
    def print_stats(self):
        print("Chain transactions: ", self.sum_transactions())
        print("Chain cpu: ", self.sum_cpu())
        print("Chain net: ", self.sum_net())
        print("Chain elapsed: ", self.sum_elapsed())
        print("Chain time: ", self.sum_time())
        print("Chain latency: ", self.sum_latency())

class chainsContainer():
    def __init__(self):
        self.preData = chainData()
        self.totalData = chainData()
        self.startBlock = 0
        self.ceaseBlock = 0
    def total_transactions(self):
        return self.totalData.sum_transactions() - self.preData.sum_transactions()
    def total_cpu(self):
        return self.totalData.sum_cpu() - self.preData.sum_cpu()
    def total_net(self):
        return self.totalData.sum_net() - self.preData.sum_net()
    def total_elapsed(self):
        return self.totalData.sum_elapsed() - self.preData.sum_elapsed()
    def total_time(self):
        return self.totalData.sum_time() - self.preData.sum_time()
    def total_latency(self):
        return self.totalData.sum_latency() - self.preData.sum_latency()
    def print_stats(self):
        print("Total transactions: ", self.total_transactions())
        print("Total cpu: ", self.total_cpu())
        print("Total net: ", self.total_net())
        print("Total elapsed: ", self.total_elapsed())
        print("Total time: ", self.total_time())
        print("Total latency: ", self.total_latency())
    def print_range(self):
        print("Starting block %d ending block %d" % (self.startBlock, self.ceaseBlock))


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
    i = -1
    f = open("var/lib/node_01/stderr.txt")
    trxResult = re.findall(r'trxs:\s+\d+.*cpu:\s+\d+.*', f.read())
    for value in trxResult:
        i+=1
        strResult = re.findall(r'trxs:\s+\d+', value)
        for str in strResult:
            intResult = re.findall(r'\d+', str)
            total.blockLog.append(blockData())
            total.blockLog[i].transactions = int(intResult[0])
    i = -1
    for value in trxResult:
        i+=1
        strResult = re.findall(r'cpu:\s+\d+', value)
        for str in strResult:
            intResult = re.findall(r'\d+', str)
            total.blockLog[i].cpu = int(intResult[0])
    i = -1
    for value in trxResult:
        i+=1
        strResult = re.findall(r'net:\s+\d+', value)
        for str in strResult:
            intResult = re.findall(r'\d+', str)
            total.blockLog[i].net = int(intResult[0])
    i = -1
    for value in trxResult:
        i+=1
        strResult = re.findall(r'elapsed:\s+\d+', value)
        for str in strResult:
            intResult = re.findall(r'\d+', str)
            total.blockLog[i].elapsed = int(intResult[0])
    i = -1
    for value in trxResult:
        i+=1
        strResult = re.findall(r'time:\s+\d+', value)
        for str in strResult:
            intResult = re.findall(r'\d+', str)
            total.blockLog[i].time = int(intResult[0])
    i = -1
    for value in trxResult:
        i+=1
        strResult = re.findall(r'latency:\s+.*\d+', value)
        for str in strResult:
            intResult = re.findall(r'-*\d+', str)
            total.blockLog[i].latency = int(intResult[0])
    f.close()

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
    cont = chainsContainer()

    # Get stats prior to transaction generation
    cont.startBlock = waitForEmptyBlocks(node1)
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
    cont.ceaseBlock = waitForEmptyBlocks(node1) - emptyBlockGoal
    fetchStats(cont.totalData)

    cont.preData.print_stats()
    cont.totalData.print_stats()
    cont.print_stats()
    cont.print_range()
    assert transactionsSent == cont.total_transactions() , "Error: Transactions received: %d did not match expected total: %d" % (cont.total_transactions(), transactionsSent)

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
