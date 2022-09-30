#!/usr/bin/env python3

import os
import sys
import subprocess
import signal

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

appArgs=AppArgs()
appArgs.add(flag="--target-tps", type=int, help="The target transfers per second to send during test", default=7000)
appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=3500)
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
       f"{testGenerationDurationSec}", f"{targetTps}", f"{tpsLimitPerGenerator}"
    ])
    # Get stats after transaction generation stops
    data.ceaseBlock = waitForEmptyBlocks(validationNode) - emptyBlockGoal + 1
    completedRun = True

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

    # Define number of potentially non-empty blocks to prune from the beginning and end of the range
    # of blocks of interest for evaluation to zero in on steady state operation.
    # All leading and trailing 0 size blocks will be pruned as well prior
    # to evaluating and applying the numBlocksToPrune
    numAddlBlocksToPrune = 2

    guide = log_reader.calcChainGuide(data, numAddlBlocksToPrune)
    tpsStats = log_reader.scoreTransfersPerSecond(data, guide)
    blkSizeStats = log_reader.calcBlockSizeStats(data, guide)
    print(f"Blocks Guide: {guide}\nTPS: {tpsStats}\nBlock Size: {blkSizeStats}")
    report = log_reader.createJSONReport(guide, tpsStats, blkSizeStats, args, completedRun)
    print(report)
    if args.save_json:
        log_reader.exportAsJSON(report, args)

    if completedRun:
        assert transactionsSent == data.totalTransactions , f"Error: Transactions received: {data.totalTransactions} did not match expected total: {transactionsSent}"
    else:
        os.system("pkill trx_generator")
        print("Test run cancelled early via SIGINT")

    testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)
