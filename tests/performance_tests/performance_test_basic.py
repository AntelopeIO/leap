#!/usr/bin/env python3

import os
import sys
import subprocess
import shutil
import signal
import log_reader

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
from dataclasses import dataclass, asdict
from datetime import datetime

class PerformanceBasicTest():
    @dataclass
    class TestHelperConfig():
        killAll: bool = True # clean_run
        dontKill: bool = False # leave_running
        keepLogs: bool = False
        dumpErrorDetails: bool = False
        delay: int = 1
        nodesFile: str = None
        verbose: bool = False
        _killEosInstances: bool = True
        _killWallet: bool = True

        def __post_init__(self):
            self._killEosInstances = not self.dontKill
            self._killWallet = not self.dontKill

    @dataclass
    class ClusterConfig():
        pnodes: int = 1
        totalNodes: int = 2
        topo: str = "mesh"
        extraNodeosArgs: str = ' --http-max-response-time-ms 990000 --disable-subjective-api-billing true '
        useBiosBootFile: bool = False
        genesisPath: str = "tests/performance_tests/genesis.json"
        maximumP2pPerHost: int = 5000
        maximumClients: int = 0
        loggingDict = { "bios": "off" }
        _totalNodes: int = 2

        def __post_init__(self):
            self._totalNodes = max(2, self.pnodes if self.totalNodes < self.pnodes else self.totalNodes)

    def __init__(self, testHelperConfig: TestHelperConfig=TestHelperConfig(), clusterConfig: ClusterConfig=ClusterConfig(), targetTps: int=8000,
                 testTrxGenDurationSec: int=30, tpsLimitPerGenerator: int=4000, numAddlBlocksToPrune: int=2,
                 rootLogDir: str=".", saveJsonReport: bool=False):
        self.testHelperConfig = testHelperConfig
        self.clusterConfig = clusterConfig
        self.targetTps = targetTps
        self.testTrxGenDurationSec = testTrxGenDurationSec
        self.tpsLimitPerGenerator = tpsLimitPerGenerator
        self.expectedTransactionsSent = self.testTrxGenDurationSec * self.targetTps
        self.saveJsonReport = saveJsonReport
        self.numAddlBlocksToPrune = numAddlBlocksToPrune
        self.saveJsonReport = saveJsonReport

        Utils.Debug = self.testHelperConfig.verbose
        self.errorExit = Utils.errorExit
        self.emptyBlockGoal = 5

        self.rootLogDir = rootLogDir
        self.ptbLogDir = f"{self.rootLogDir}/{os.path.splitext(os.path.basename(__file__))[0]}"
        self.testTimeStampDirPath = f"{self.ptbLogDir}/{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}"
        self.trxGenLogDirPath = f"{self.testTimeStampDirPath}/trxGenLogs"
        self.blockDataLogDirPath = f"{self.testTimeStampDirPath}/blockDataLogs"
        self.blockDataPath = f"{self.blockDataLogDirPath}/blockData.txt"
        self.blockTrxDataPath = f"{self.blockDataLogDirPath}/blockTrxData.txt"
        self.reportPath = f"{self.testTimeStampDirPath}/data.json"
        self.nodeosLogPath = "var/lib/node_01/stderr.txt"

        # Setup cluster and its wallet manager
        self.walletMgr=WalletMgr(True)
        self.cluster=Cluster(walletd=True, loggingLevel="info", loggingLevelDict=self.clusterConfig.loggingDict)
        self.cluster.setWalletMgr(self.walletMgr)

    def cleanupOldClusters(self):
        self.cluster.killall(allInstances=self.testHelperConfig.killAll)
        self.cluster.cleanup()

    def testDirsCleanup(self, saveJsonReport: bool=False):
        try:
            if saveJsonReport:
                print(f"Checking if test artifacts dir exists: {self.trxGenLogDirPath}")
                if os.path.isdir(f"{self.trxGenLogDirPath}"):
                    print(f"Cleaning up test artifacts dir and all contents of: {self.trxGenLogDirPath}")
                    shutil.rmtree(f"{self.trxGenLogDirPath}")

                print(f"Checking if test artifacts dir exists: {self.blockDataLogDirPath}")
                if os.path.isdir(f"{self.blockDataLogDirPath}"):
                    print(f"Cleaning up test artifacts dir and all contents of: {self.blockDataLogDirPath}")
                    shutil.rmtree(f"{self.blockDataLogDirPath}")
            else:
                print(f"Checking if test artifacts dir exists: {self.testTimeStampDirPath}")
                if os.path.isdir(f"{self.testTimeStampDirPath}"):
                    print(f"Cleaning up test artifacts dir and all contents of: {self.testTimeStampDirPath}")
                    shutil.rmtree(f"{self.testTimeStampDirPath}")
        except OSError as error:
            print(error)

    def testDirsSetup(self):
        try:
            print(f"Checking if root log dir exists: {self.rootLogDir}")
            if not os.path.isdir(f"{self.rootLogDir}"):
                print(f"Creating root log dir: {self.rootLogDir}")
                os.mkdir(f"{self.rootLogDir}")

            print(f"Checking if test artifacts dir exists: {self.ptbLogDir}")
            if not os.path.isdir(f"{self.ptbLogDir}"):
                print(f"Creating test artifacts dir: {self.ptbLogDir}")
                os.mkdir(f"{self.ptbLogDir}")

            print(f"Checking if logs dir exists: {self.testTimeStampDirPath}")
            if not os.path.isdir(f"{self.testTimeStampDirPath}"):
                print(f"Creating logs dir: {self.testTimeStampDirPath}")
                os.mkdir(f"{self.testTimeStampDirPath}")

            print(f"Checking if logs dir exists: {self.trxGenLogDirPath}")
            if not os.path.isdir(f"{self.trxGenLogDirPath}"):
                print(f"Creating logs dir: {self.trxGenLogDirPath}")
                os.mkdir(f"{self.trxGenLogDirPath}")

            print(f"Checking if logs dir exists: {self.blockDataLogDirPath}")
            if not os.path.isdir(f"{self.blockDataLogDirPath}"):
                print(f"Creating logs dir: {self.blockDataLogDirPath}")
                os.mkdir(f"{self.blockDataLogDirPath}")
        except OSError as error:
            print(error)

    def fileOpenMode(self, filePath) -> str:
        if os.path.exists(filePath):
            append_write = 'a'
        else:
            append_write = 'w'
        return append_write

    def queryBlockTrxData(self, node, blockDataPath, blockTrxDataPath, startBlockNum, endBlockNum):
        for blockNum in range(startBlockNum, endBlockNum):
            block = node.processCurlCmd("trace_api", "get_block", f'{{"block_num":{blockNum}}}', silentErrors=False, exitOnError=True)

            btdf_append_write = self.fileOpenMode(blockTrxDataPath)
            with open(blockTrxDataPath, btdf_append_write) as trxDataFile:
                [trxDataFile.write(f"{trx['id']},{trx['block_num']},{trx['cpu_usage_us']},{trx['net_usage_words']}\n") for trx in block['transactions'] if block['transactions']]
            trxDataFile.close()

            bdf_append_write = self.fileOpenMode(blockDataPath)
            with open(blockDataPath, bdf_append_write) as blockDataFile:
                blockDataFile.write(f"{block['number']},{block['id']},{block['producer']},{block['status']},{block['timestamp']}\n")
            blockDataFile.close()

    def waitForEmptyBlocks(self, node, numEmptyToWaitOn):
        emptyBlocks = 0
        while emptyBlocks < numEmptyToWaitOn:
            headBlock = node.getHeadBlockNum()
            block = node.processCurlCmd("chain", "get_block_info", f'{{"block_num":{headBlock}}}', silentErrors=False, exitOnError=True)
            node.waitForHeadToAdvance()
            if block['transaction_mroot'] == "0000000000000000000000000000000000000000000000000000000000000000":
                emptyBlocks += 1
            else:
                emptyBlocks = 0
        return node.getHeadBlockNum()

    def launchCluster(self):
        return self.cluster.launch(
            pnodes=self.clusterConfig.pnodes,
            totalNodes=self.clusterConfig._totalNodes,
            useBiosBootFile=self.clusterConfig.useBiosBootFile,
            topo=self.clusterConfig.topo,
            genesisPath=self.clusterConfig.genesisPath,
            maximumP2pPerHost=self.clusterConfig.maximumP2pPerHost,
            maximumClients=self.clusterConfig.maximumClients,
            extraNodeosArgs=self.clusterConfig.extraNodeosArgs
            )

    def setupWalletAndAccounts(self):
        self.wallet = self.walletMgr.create('default')
        self.cluster.populateWallet(2, self.wallet)
        self.cluster.createAccounts(self.cluster.eosioAccount, stakedDeposit=0)

        self.account1Name = self.cluster.accounts[0].name
        self.account2Name = self.cluster.accounts[1].name

        self.account1PrivKey = self.cluster.accounts[0].activePrivateKey
        self.account2PrivKey = self.cluster.accounts[1].activePrivateKey

    def runTpsTest(self) -> bool:
        self.producerNode = self.cluster.getNode(0)
        self.validationNode = self.cluster.getNode(1)
        info = self.producerNode.getInfo()
        chainId = info['chain_id']
        lib_id = info['last_irreversible_block_id']
        self.data = log_reader.chainData()

        self.cluster.biosNode.kill(signal.SIGTERM)

        self.data.startBlock = self.waitForEmptyBlocks(self.validationNode, self.emptyBlockGoal)

        subprocess.run([
            f"./tests/performance_tests/launch_transaction_generators.py",
            f"{chainId}", f"{lib_id}", f"{self.cluster.eosioAccount.name}",
            f"{self.account1Name}", f"{self.account2Name}", f"{self.account1PrivKey}", f"{self.account2PrivKey}",
            f"{self.testTrxGenDurationSec}", f"{self.targetTps}", f"{self.tpsLimitPerGenerator}", f"{self.trxGenLogDirPath}"
            ])

        # Get stats after transaction generation stops
        self.data.ceaseBlock = self.waitForEmptyBlocks(self.validationNode, self.emptyBlockGoal) - self.emptyBlockGoal + 1

        return True

    def prepArgs(self) -> dict:
        args = {}
        args.update(asdict(self.testHelperConfig))
        args.update(asdict(self.clusterConfig))
        args["targetTps"] = self.targetTps
        args["testTrxGenDurationSec"] = self.testTrxGenDurationSec
        args["tpsLimitPerGenerator"] = self.tpsLimitPerGenerator
        args["expectedTransactionsSent"] = self.expectedTransactionsSent
        args["saveJsonReport"] = self.saveJsonReport
        args["numAddlBlocksToPrune"] = self.numAddlBlocksToPrune
        return args

    def analyzeResultsAndReport(self, completedRun):
        args = self.prepArgs()
        self.report = log_reader.calcAndReport(data=self.data, targetTps=self.targetTps, testDurationSec=self.testTrxGenDurationSec, tpsLimitPerGenerator=self.tpsLimitPerGenerator,
                                               nodeosLogPath=self.nodeosLogPath, trxGenLogDirPath=self.trxGenLogDirPath, blockTrxDataPath=self.blockTrxDataPath,
                                               blockDataPath=self.blockDataPath, numBlocksToPrune=self.numAddlBlocksToPrune, argsDict=args, completedRun=completedRun)

        print(self.data)

        print("Report:")
        print(self.report)

        if self.saveJsonReport:
            log_reader.exportReportAsJSON(self.report, self.reportPath)

    def preTestSpinup(self):
        self.cleanupOldClusters()
        self.testDirsCleanup()
        self.testDirsSetup()

        if self.launchCluster() == False:
            self.errorExit('Failed to stand up cluster.')

        self.setupWalletAndAccounts()

    def postTpsTestSteps(self):
        self.queryBlockTrxData(self.validationNode, self.blockDataPath, self.blockTrxDataPath, self.data.startBlock, self.data.ceaseBlock)

    def runTest(self) -> bool:
        testSuccessful = False
        completedRun = False

        try:
            # Kill any existing instances and launch cluster
            TestHelper.printSystemInfo("BEGIN")
            self.preTestSpinup()

            completedRun = self.runTpsTest()
            self.postTpsTestSteps()

            testSuccessful = True

            self.analyzeResultsAndReport(completedRun)

        except subprocess.CalledProcessError as err:
            print(f"trx_generator return error code: {err.returncode}.  Test aborted.")
        finally:
            TestHelper.shutdown(
                self.cluster,
                self.walletMgr,
                testSuccessful,
                self.testHelperConfig._killEosInstances,
                self.testHelperConfig._killWallet,
                self.testHelperConfig.keepLogs,
                self.testHelperConfig.killAll,
                self.testHelperConfig.dumpErrorDetails
                )

            if not completedRun:
                os.system("pkill trx_generator")
                print("Test run cancelled early via SIGINT")

            if not self.testHelperConfig.keepLogs:
                print(f"Cleaning up logs directory: {self.testTimeStampDirPath}")
                self.testDirsCleanup(self.saveJsonReport)

            return testSuccessful

def parseArgs():
    appArgs=AppArgs()
    appArgs.add(flag="--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
    appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    appArgs.add(flag="--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=30)
    appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
    appArgs.add(flag="--num-blocks-to-prune", type=int, help=("The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, "
                "to prune from the beginning and end of the range of blocks of interest for evaluation."), default=2)
    appArgs.add(flag="--save-json", type=bool, help="Whether to save json output of stats", default=False)
    args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                                ,"--dump-error-details","-v","--leave-running"
                                ,"--clean-run","--keep-logs"}, applicationSpecificArgs=appArgs)
    return args

def main():

    args = parseArgs()
    Utils.Debug = args.v

    testHelperConfig = PerformanceBasicTest.TestHelperConfig(killAll=args.clean_run, dontKill=args.leave_running, keepLogs=args.keep_logs,
                                                             dumpErrorDetails=args.dump_error_details, delay=args.d, nodesFile=args.nodes_file, verbose=args.v)
    testClusterConfig = PerformanceBasicTest.ClusterConfig(pnodes=args.p, totalNodes=args.n, topo=args.s, genesisPath=args.genesis)

    myTest = PerformanceBasicTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, targetTps=args.target_tps,
                                  testTrxGenDurationSec=args.test_duration_sec, tpsLimitPerGenerator=args.tps_limit_per_generator,
                                  numAddlBlocksToPrune=args.num_blocks_to_prune, saveJsonReport=args.save_json)
    testSuccessful = myTest.runTest()

    if testSuccessful:
        assert myTest.expectedTransactionsSent == myTest.data.totalTransactions , \
        f"Error: Transactions received: {myTest.data.totalTransactions} did not match expected total: {myTest.expectedTransactionsSent}"

    exitCode = 0 if testSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
