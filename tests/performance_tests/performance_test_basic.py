#!/usr/bin/env python3

import os
import sys
import subprocess
import shutil
import signal
import time
from datetime import datetime

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
import log_reader

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30
emptyBlockGoal = 5

def fileOpenMode(filePath) -> str:
        if os.path.exists(filePath):
            append_write = 'a'
        else:
            append_write = 'w'
        return append_write

def queryBlockTrxData(node, blockDataPath, blockTrxDataPath, startBlockNum, endBlockNum):
    for blockNum in range(startBlockNum, endBlockNum):
        block = node.processCurlCmd("trace_api", "get_block", f'{{"block_num":{blockNum}}}', silentErrors=False, exitOnError=True)

        btdf_append_write = fileOpenMode(blockTrxDataPath)
        with open(blockTrxDataPath, btdf_append_write) as trxDataFile:
            [trxDataFile.write(f"{trx['id']},{trx['block_num']},{trx['cpu_usage_us']},{trx['net_usage_words']}\n") for trx in block['transactions'] if block['transactions']]
        trxDataFile.close()

        bdf_append_write = fileOpenMode(blockDataPath)
        with open(blockDataPath, bdf_append_write) as blockDataFile:
            blockDataFile.write(f"{block['number']},{block['id']},{block['producer']},{block['status']},{block['timestamp']}\n")
        blockDataFile.close()

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

def testDirsCleanup(rootDir):
    try:
        print(f"Checking if test artifacts dir exists: {rootDir}")
        if os.path.isdir(f"{rootDir}"):
            print(f"Cleaning up test artifacts dir and all contents of: {rootDir}")
            shutil.rmtree(f"{rootDir}")
    except OSError as error:
        print(error)

def testDirsSetup(scriptName, testRunTimestamp, trxGenLogDir, blockDataLogDir):
    try:
        print(f"Checking if test artifacts dir exists: {scriptName}")
        if not os.path.isdir(f"{scriptName}"):
            print(f"Creating test artifacts dir: {scriptName}")
            os.mkdir(f"{scriptName}")

        print(f"Checking if logs dir exists: {testRunTimestamp}")
        if not os.path.isdir(f"{testRunTimestamp}"):
            print(f"Creating logs dir: {testRunTimestamp}")
            os.mkdir(f"{testRunTimestamp}")

        print(f"Checking if logs dir exists: {trxGenLogDir}")
        if not os.path.isdir(f"{trxGenLogDir}"):
            print(f"Creating logs dir: {trxGenLogDir}")
            os.mkdir(f"{trxGenLogDir}")

        print(f"Checking if logs dir exists: {blockDataLogDir}")
        if not os.path.isdir(f"{blockDataLogDir}"):
            print(f"Creating logs dir: {blockDataLogDir}")
            os.mkdir(f"{blockDataLogDir}")
    except OSError as error:
        print(error)

appArgs=AppArgs()
appArgs.add(flag="--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
appArgs.add(flag="--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=30)
appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
appArgs.add(flag="--num-blocks-to-prune", type=int, help="The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end of the range of blocks of interest for evaluation.", default=2)
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
numAddlBlocksToPrune = args.num_blocks_to_prune
logging_dict = {
    "bios": "off"
}

# Setup cluster and its wallet manager
walletMgr=WalletMgr(True)
cluster=Cluster(walletd=True, loggingLevel="info", loggingLevelDict=logging_dict)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
completedRun = False

try:
    # Kill any existing instances and launch cluster
    TestHelper.printSystemInfo("BEGIN")
    cluster.killall(allInstances=killAll)
    cluster.cleanup()

    scriptName  = os.path.splitext(os.path.basename(__file__))[0]
    testTimeStampDirPath = f"{scriptName}/{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}"
    trxGenLogDirPath = f"{testTimeStampDirPath}/trxGenLogs"
    blockDataLogDirPath = f"{testTimeStampDirPath}/blockDataLogs"

    testDirsCleanup(testTimeStampDirPath)

    testDirsSetup(scriptName, testTimeStampDirPath, trxGenLogDirPath, blockDataLogDirPath)

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
       f"{testGenerationDurationSec}", f"{targetTps}", f"{tpsLimitPerGenerator}", f"{trxGenLogDirPath}"
    ])
    # Get stats after transaction generation stops
    data.ceaseBlock = waitForEmptyBlocks(validationNode) - emptyBlockGoal + 1
    completedRun = True

    blockDataPath = f"{blockDataLogDirPath}/blockData.txt"
    blockTrxDataPath = f"{blockDataLogDirPath}/blockTrxData.txt"

    queryBlockTrxData(validationNode, blockDataPath, blockTrxDataPath, data.startBlock, data.ceaseBlock)

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

    report = log_reader.calcAndReport(data, "var/lib/node_01/stderr.txt", trxGenLogDirPath, blockTrxDataPath, blockDataPath, args, completedRun)

    print(data)

    print("Report:")
    print(report)

    if args.save_json:
        log_reader.exportAsJSON(report, args)

    if completedRun:
        assert transactionsSent == data.totalTransactions , f"Error: Transactions received: {data.totalTransactions} did not match expected total: {transactionsSent}"
    else:
        os.system("pkill trx_generator")
        print("Test run cancelled early via SIGINT")

    if not keepLogs:
        print(f"Cleaning up logs directory: {testTimeStampDirPath}")
        testDirsCleanup(testTimeStampDirPath)

    testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)
