#!/usr/bin/env python3

import os
import sys
import subprocess
import shutil
import signal
import time
from datetime import datetime
import glob

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
from dataclasses import dataclass, field
import log_reader
import gzip

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30
emptyBlockGoal = 5

@dataclass
class trxData():
    blockNum: int = 0
    cpuUsageUs: int = 0
    netUsageUs: int = 0
    _sentTimestamp: str = field(init=True, repr=False, default='')
    _calcdTimeEpoch: float = 0

    @property
    def sentTimestamp(self):
        return self._sentTimestamp

    @property
    def calcdTimeEpoch(self):
        return self._calcdTimeEpoch

    @sentTimestamp.setter
    def sentTimestamp(self, sentTime: str):
        self._sentTimestamp = sentTime
        self._calcdTimeEpoch = datetime.fromisoformat(sentTime).timestamp()

    @sentTimestamp.deleter
    def sentTimestamp(self):
        self._sentTimestamp = ""
        self._calcdTimeEpoch = 0

@dataclass
class blkData():
    blockId: int = 0
    producer: str = ""
    status: str = ""
    timestamp: str = ""
    timestampEpoch: float = 0

def queryBlockData(node, blockNum, trxDict, blockDict):
    block = node.processCurlCmd("trace_api", "get_block", f'{{"block_num":{blockNum}}}', silentErrors=False, exitOnError=True)

    trxDict.update(dict([(trx['id'], trxData(trx['block_num'], trx['cpu_usage_us'], trx['net_usage_words'], "")) for trx in block['transactions'] if block['transactions']]))

    #Note block timestamp formatted like: '2022-09-30T16:48:13.500Z', but 'Z' is not part of python's recognized iso format, so strip it off the end
    blockDict.update({block['number'] : blkData(block['id'], block['producer'], block['status'], block['timestamp'], datetime.fromisoformat(block['timestamp'][:-1]).timestamp())})

def scrapeTrxGenLog(trxSent, path):
    selectedopen = gzip.open if path.endswith('.gz') else open
    with selectedopen(path, 'rt') as f:
        trxSent.update(dict([(x[0], x[1]) for x in (line.rstrip('\n').split(',') for line in f)]))

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

def testDirsCleanup(scriptName):
    try:
        print(f"Checking if test artifacts dir exists: {scriptName}")
        if os.path.isdir(f"{scriptName}"):
            print(f"Cleaning up test artifacts dir and all contents of: {scriptName}")
            shutil.rmtree(f"{scriptName}")
    except OSError as error:
        print(error)

def testDirsSetup(scriptName, logDir):
    try:
        print(f"Checking if test artifacts dir exists: {scriptName}")
        if not os.path.isdir(f"{scriptName}"):
            print(f"Creating test artifacts dir: {scriptName}")
            os.mkdir(f"{scriptName}")
        print(f"Checking if logs dir exists: {logDir}")
        if not os.path.isdir(f"{logDir}"):
            print(f"Creating logs dir: {logDir}")
            os.mkdir(f"{logDir}")
    except OSError as error:
        print(error)

appArgs=AppArgs()
appArgs.add(flag="--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
appArgs.add(flag="--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=30)
appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
appArgs.add(flag="--save-json", type=bool, help="Whether to save json output of stats", default=False)
appArgs.add(flag="--json-path", type=str, help="Path to save json output", default="data.json")
args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs"}, applicationSpecificArgs=appArgs)

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
testGenerationDurationSec = args.test_duration_sec
targetTps = args.target_tps
genesisJsonFile = args.genesis
tpsLimitPerGenerator = args.tps_limit_per_generator

# Setup cluster and its wallet manager
walletMgr=WalletMgr(True)
cluster=Cluster(walletd=True, loggingLevel="info")
cluster.setWalletMgr(walletMgr)

testSuccessful = False
completedRun = False

try:
    # Kill any existing instances and launch cluster
    TestHelper.printSystemInfo("BEGIN")
    cluster.killall(allInstances=killAll)
    cluster.cleanup()

    scriptName  = os.path.splitext(os.path.basename(__file__))[0]
    logDir = f'{scriptName}/logs'

    testDirsCleanup(scriptName)

    testDirsSetup(scriptName, logDir)

    extraNodeosArgs=' --http-max-response-time-ms 990000 --disable-subjective-api-billing true '
    if cluster.launch(
       pnodes=pnodes,
       totalNodes=total_nodes,
       useBiosBootFile=False,
       topo=topo,
       genesisPath=genesisJsonFile,
       maximumP2pPerHost=5000,
       maximumClients=0,
       extraNodeosArgs=extraNodeosArgs
    ) == False:
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
    cluster.biosNode.kill(signal.SIGTERM)

    transactionsSent = testGenerationDurationSec * targetTps
    data = log_reader.chainData()

    data.startBlock = waitForEmptyBlocks(validationNode)

    subprocess.run([
       f"./tests/performance_tests/launch_transaction_generators.py",
       f"{chainId}", f"{lib_id}", f"{cluster.eosioAccount.name}",
       f"{account1Name}", f"{account2Name}", f"{account1PrivKey}", f"{account2PrivKey}",
       f"{testGenerationDurationSec}", f"{targetTps}", f"{tpsLimitPerGenerator}", f"{logDir}"
    ])
    # Get stats after transaction generation stops
    data.ceaseBlock = waitForEmptyBlocks(validationNode) - emptyBlockGoal + 1
    completedRun = True

    trxDict = {}
    blockDict = {}
    for query in range(data.startBlock, data.ceaseBlock):
        queryBlockData(validationNode, query, trxDict, blockDict)

    time.sleep(5)

except subprocess.CalledProcessError as err:
    print(f"trx_generator return error code: {err.returncode}.  Test aborted.")
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
    log_reader.scrapeLog(data, "var/lib/node_01/stderr.txt")

    print(data)

    trxSent = {}
    filesScraped = []
    for fileName in glob.glob(f"{logDir}/trx_data_output_*.txt"):
        filesScraped.append(fileName)
        scrapeTrxGenLog(trxSent, fileName)
        os.rename(fileName, f"{fileName}.prev")

    print("Transaction Log Files Scraped:")
    print(filesScraped)

    notFound = []
    for sentTrxId in trxSent.keys():
        if sentTrxId in trxDict.keys():
            trxDict[sentTrxId].sentTimestamp = trxSent[sentTrxId]
        else:
            notFound.append(sentTrxId)

    if len(notFound) > 0:
        print(f"Transactions logged as sent but NOT FOUND in block!! count {len(notFound)} :")
        # print(notFound)

    # Define number of potentially non-empty blocks to prune from the beginning and end of the range
    # of blocks of interest for evaluation to zero in on steady state operation.
    # All leading and trailing 0 size blocks will be pruned as well prior
    # to evaluating and applying the numBlocksToPrune
    numAddlBlocksToPrune = 2

    guide = log_reader.calcChainGuide(data, numAddlBlocksToPrune)
    trxLatencyStats = log_reader.calcTrxLatencyStats(trxDict, blockDict)
    tpsStats = log_reader.scoreTransfersPerSecond(data, guide)
    blkSizeStats = log_reader.calcBlockSizeStats(data, guide)

    print(f"Blocks Guide: {guide}\nTPS: {tpsStats}\nBlock Size: {blkSizeStats}\nTrx Latency: {trxLatencyStats}")

    report = log_reader.createJSONReport(guide, tpsStats, blkSizeStats, trxLatencyStats, args, completedRun)
    print(report)

    if args.save_json:
        log_reader.exportAsJSON(report, args)

    if completedRun:
        assert transactionsSent == data.totalTransactions , f"Error: Transactions received: {data.totalTransactions} did not match expected total: {transactionsSent}"
    else:
        os.system("pkill trx_generator")
        print("Test run cancelled early via SIGINT")

    testDirsCleanup(scriptName)

    testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)
