#!/usr/bin/env python3

import os
import sys
import subprocess
import shutil
import signal
import log_reader
import inspect

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
from dataclasses import dataclass, asdict, field
from datetime import datetime

class PerformanceBasicTest:
    @dataclass
    class TestHelperConfig:
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
    class ClusterConfig:
        pnodes: int = 1
        totalNodes: int = 2
        topo: str = "mesh"
        extraNodeosArgs: str = ' --http-max-response-time-ms 990000 --disable-subjective-api-billing true '
        useBiosBootFile: bool = False
        genesisPath: str = "tests/performance_tests/genesis.json"
        maximumP2pPerHost: int = 5000
        maximumClients: int = 0
        loggingDict: dict = field(default_factory=lambda: { "bios": "off" })
        prodsEnableTraceApi: bool = False
        specificExtraNodeosArgs: dict = field(default_factory=dict)
        _totalNodes: int = 2

        def __post_init__(self):
            self._totalNodes = self.pnodes + 1 if self.totalNodes <= self.pnodes else self.totalNodes
            if not self.prodsEnableTraceApi:
                self.specificExtraNodeosArgs.update({f"{node}" : "--plugin eosio::trace_api_plugin" for node in range(self.pnodes, self._totalNodes)})

    def __init__(self, testHelperConfig: TestHelperConfig=TestHelperConfig(), clusterConfig: ClusterConfig=ClusterConfig(), targetTps: int=8000,
                 testTrxGenDurationSec: int=30, tpsLimitPerGenerator: int=4000, numAddlBlocksToPrune: int=2,
                 rootLogDir: str=".", saveJsonReport: bool=False, quiet: bool=False, delPerfLogs: bool=False):
        self.testHelperConfig = testHelperConfig
        self.clusterConfig = clusterConfig
        self.targetTps = targetTps
        self.testTrxGenDurationSec = testTrxGenDurationSec
        self.tpsLimitPerGenerator = tpsLimitPerGenerator
        self.expectedTransactionsSent = self.testTrxGenDurationSec * self.targetTps
        self.saveJsonReport = saveJsonReport
        self.numAddlBlocksToPrune = numAddlBlocksToPrune
        self.saveJsonReport = saveJsonReport
        self.quiet = quiet
        self.delPerfLogs=delPerfLogs

        Utils.Debug = self.testHelperConfig.verbose
        self.errorExit = Utils.errorExit
        self.emptyBlockGoal = 5

        self.testStart = datetime.utcnow()

        self.rootLogDir = rootLogDir
        self.ptbLogDir = f"{self.rootLogDir}/{os.path.splitext(os.path.basename(__file__))[0]}"
        self.testTimeStampDirPath = f"{self.ptbLogDir}/{self.testStart.strftime('%Y-%m-%d_%H-%M-%S')}"
        self.trxGenLogDirPath = f"{self.testTimeStampDirPath}/trxGenLogs"
        self.blockDataLogDirPath = f"{self.testTimeStampDirPath}/blockDataLogs"
        self.blockDataPath = f"{self.blockDataLogDirPath}/blockData.txt"
        self.blockTrxDataPath = f"{self.blockDataLogDirPath}/blockTrxData.txt"
        self.reportPath = f"{self.testTimeStampDirPath}/data.json"
        self.nodeosLogPath = "var/lib/node_01/stderr.txt"

        # Setup Expectations for Producer and Validation Node IDs
        # Producer Nodes are index [0, pnodes) and validation nodes/non-producer nodes [pnodes, _totalNodes)
        # Use first producer node and first non-producer node
        self.producerNodeId = 0
        self.validationNodeId = self.clusterConfig.pnodes

        # Setup cluster and its wallet manager
        self.walletMgr=WalletMgr(True)
        self.cluster=Cluster(walletd=True, loggingLevel="info", loggingLevelDict=self.clusterConfig.loggingDict)
        self.cluster.setWalletMgr(self.walletMgr)

    def cleanupOldClusters(self):
        self.cluster.killall(allInstances=self.testHelperConfig.killAll)
        self.cluster.cleanup()

    def testDirsCleanup(self, saveJsonReport: bool=False):
        try:
            def removeArtifacts(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if os.path.isdir(f"{path}"):
                    print(f"Cleaning up test artifacts dir and all contents of: {path}")
                    shutil.rmtree(f"{path}")

            if saveJsonReport:
                removeArtifacts(self.trxGenLogDirPath)
                removeArtifacts(self.blockDataLogDirPath)
            else:
                removeArtifacts(self.testTimeStampDirPath)
        except OSError as error:
            print(error)

    def testDirsSetup(self):
        try:
            def createArtifactsDir(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if not os.path.isdir(f"{path}"):
                    print(f"Creating test artifacts dir: {path}")
                    os.mkdir(f"{path}")

            createArtifactsDir(self.rootLogDir)
            createArtifactsDir(self.ptbLogDir)
            createArtifactsDir(self.testTimeStampDirPath)
            createArtifactsDir(self.trxGenLogDirPath)
            createArtifactsDir(self.blockDataLogDirPath)

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
            extraNodeosArgs=self.clusterConfig.extraNodeosArgs,
            prodsEnableTraceApi=self.clusterConfig.prodsEnableTraceApi,
            specificExtraNodeosArgs=self.clusterConfig.specificExtraNodeosArgs
            )

    def setupWalletAndAccounts(self):
        self.wallet = self.walletMgr.create('default')
        self.cluster.populateWallet(2, self.wallet)
        self.cluster.createAccounts(self.cluster.eosioAccount, stakedDeposit=0, validationNodeIndex=self.validationNodeId)

        self.account1Name = self.cluster.accounts[0].name
        self.account2Name = self.cluster.accounts[1].name

        self.account1PrivKey = self.cluster.accounts[0].activePrivateKey
        self.account2PrivKey = self.cluster.accounts[1].activePrivateKey

    def runTpsTest(self) -> bool:
        self.producerNode = self.cluster.getNode(self.producerNodeId)
        self.validationNode = self.cluster.getNode(self.validationNodeId)
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
        args.update({key:val for key, val in inspect.getmembers(self) if key in set(['targetTps', 'testTrxGenDurationSec', 'tpsLimitPerGenerator',
                                                                                     'expectedTransactionsSent', 'saveJsonReport', 'numAddlBlocksToPrune', 'quiet', 'delPerfLogs'])})
        return args


    def analyzeResultsAndReport(self, completedRun):
        args = self.prepArgs()
        self.report = log_reader.calcAndReport(data=self.data, targetTps=self.targetTps, testDurationSec=self.testTrxGenDurationSec, tpsLimitPerGenerator=self.tpsLimitPerGenerator,
                                               nodeosLogPath=self.nodeosLogPath, trxGenLogDirPath=self.trxGenLogDirPath, blockTrxDataPath=self.blockTrxDataPath,
                                               blockDataPath=self.blockDataPath, numBlocksToPrune=self.numAddlBlocksToPrune, argsDict=args, testStart=self.testStart,
                                               completedRun=completedRun, quiet=self.quiet)

        if not self.quiet:
            print(self.data)

            print("Report:")
            print(log_reader.reportAsJSON(self.report))

        if self.saveJsonReport:
            log_reader.exportReportAsJSON(log_reader.reportAsJSON(self.report), self.reportPath)

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

            if self.delPerfLogs:
                print(f"Cleaning up logs directory: {self.testTimeStampDirPath}")
                self.testDirsCleanup(self.saveJsonReport)

            return testSuccessful

def parseArgs():
    appArgs=AppArgs()
    appArgs.add(flag="--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
    appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    appArgs.add(flag="--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=90)
    appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
    appArgs.add(flag="--num-blocks-to-prune", type=int, help=("The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, "
                "to prune from the beginning and end of the range of blocks of interest for evaluation."), default=2)
    appArgs.add_bool(flag="--del-perf-logs", help="Whether to delete performance test specific logs.")
    appArgs.add_bool(flag="--save-json", help="Whether to save json output of stats")
    appArgs.add_bool(flag="--quiet", help="Whether to quiet printing intermediate results and reports to stdout")
    appArgs.add_bool(flag="--prods-enable-trace-api", help="Determines whether producer nodes should have eosio::trace_api_plugin enabled")
    args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                                ,"--dump-error-details","-v","--leave-running"
                                ,"--clean-run","--keep-logs"}, applicationSpecificArgs=appArgs)
    return args

def main():

    args = parseArgs()
    Utils.Debug = args.v

    testHelperConfig = PerformanceBasicTest.TestHelperConfig(killAll=args.clean_run, dontKill=args.leave_running, keepLogs=args.keep_logs,
                                                             dumpErrorDetails=args.dump_error_details, delay=args.d, nodesFile=args.nodes_file, verbose=args.v)
    testClusterConfig = PerformanceBasicTest.ClusterConfig(pnodes=args.p, totalNodes=args.n, topo=args.s, genesisPath=args.genesis, prodsEnableTraceApi=args.prods_enable_trace_api)

    myTest = PerformanceBasicTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, targetTps=args.target_tps,
                                  testTrxGenDurationSec=args.test_duration_sec, tpsLimitPerGenerator=args.tps_limit_per_generator,
                                  numAddlBlocksToPrune=args.num_blocks_to_prune, saveJsonReport=args.save_json, quiet=args.quiet,
                                  delPerfLogs=args.del_perf_logs)
    testSuccessful = myTest.runTest()

    if testSuccessful:
        assert myTest.expectedTransactionsSent == myTest.data.totalTransactions , \
        f"Error: Transactions received: {myTest.data.totalTransactions} did not match expected total: {myTest.expectedTransactionsSent}"

    exitCode = 0 if testSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
