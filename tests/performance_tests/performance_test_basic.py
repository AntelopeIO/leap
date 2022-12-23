#!/usr/bin/env python3

import argparse
import dataclasses
import os
import re
import sys
import shutil
import signal
import log_reader
import launch_transaction_generators as ltg

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

from NodeosPluginArgs import ChainPluginArgs, HttpClientPluginArgs, HttpPluginArgs, NetPluginArgs, ProducerPluginArgs, ResourceMonitorPluginArgs, SignatureProviderPluginArgs, StateHistoryPluginArgs, TraceApiPluginArgs
from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from dataclasses import dataclass, asdict, field
from datetime import datetime

class PerformanceTestBasic:
    @dataclass
    class PtbTpsTestResult:
        completedRun: bool = False
        numGeneratorsUsed: int = 0
        targetTpsPerGenList: list = field(default_factory=list)
        trxGenExitCodes: list = field(default_factory=list)

    @dataclass
    class TestHelperConfig:
        killAll: bool = True # clean_run
        dontKill: bool = False # leave_running
        keepLogs: bool = True
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
        @dataclass
        class ExtraNodeosArgs:

            chainPluginArgs: ChainPluginArgs = ChainPluginArgs()
            httpClientPluginArgs: HttpClientPluginArgs = HttpClientPluginArgs()
            httpPluginArgs: HttpPluginArgs = HttpPluginArgs()
            netPluginArgs: NetPluginArgs = NetPluginArgs()
            producerPluginArgs: ProducerPluginArgs = ProducerPluginArgs()
            resourceMonitorPluginArgs: ResourceMonitorPluginArgs = ResourceMonitorPluginArgs()
            signatureProviderPluginArgs: SignatureProviderPluginArgs = SignatureProviderPluginArgs()
            stateHistoryPluginArgs: StateHistoryPluginArgs = StateHistoryPluginArgs()
            traceApiPluginArgs: TraceApiPluginArgs = TraceApiPluginArgs()

            def __str__(self) -> str:
                args = []
                for field in dataclasses.fields(self):
                    match = re.search("\w*PluginArgs", field.name)
                    if match is not None:
                        args.append(f"{getattr(self, field.name)}")
                return " ".join(args)

        pnodes: int = 1
        totalNodes: int = 2
        topo: str = "mesh"
        extraNodeosArgs: ExtraNodeosArgs = ExtraNodeosArgs()
        useBiosBootFile: bool = False
        genesisPath: Path = Path("tests")/"performance_tests"/"genesis.json"
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

    @dataclass
    class PtbConfig:
        targetTps: int=8000
        testTrxGenDurationSec: int=30
        tpsLimitPerGenerator: int=4000
        numAddlBlocksToPrune: int=2
        logDirRoot: Path=Path(".")
        delReport: bool=False
        quiet: bool=False
        delPerfLogs: bool=False
        expectedTransactionsSent: int = field(default_factory=int, init=False)

        def __post_init__(self):
            self.expectedTransactionsSent = self.testTrxGenDurationSec * self.targetTps

    @dataclass
    class LoggingConfig:
        logDirBase: Path = Path(".")/PurePath(PurePath(__file__).name).stem[0]
        logDirTimestamp: str = f"{datetime.utcnow().strftime('%Y-%m-%d_%H-%M-%S')}"
        logDirTimestampedOptSuffix: str = ""
        logDirPath: Path = field(default_factory=Path, init=False)

        def __post_init__(self):
            self.logDirPath = self.logDirBase/Path(f"{self.logDirTimestamp}{self.logDirTimestampedOptSuffix}")

    def __init__(self, testHelperConfig: TestHelperConfig=TestHelperConfig(), clusterConfig: ClusterConfig=ClusterConfig(), ptbConfig=PtbConfig()):
        self.testHelperConfig = testHelperConfig
        self.clusterConfig = clusterConfig
        self.ptbConfig = ptbConfig

        self.testHelperConfig.keepLogs = not self.ptbConfig.delPerfLogs

        Utils.Debug = self.testHelperConfig.verbose
        self.errorExit = Utils.errorExit
        self.emptyBlockGoal = 1

        self.testStart = datetime.utcnow()

        self.loggingConfig = PerformanceTestBasic.LoggingConfig(logDirBase=Path(self.ptbConfig.logDirRoot)/PurePath(PurePath(__file__).name).stem[0],
                                                                logDirTimestamp=f"{self.testStart.strftime('%Y-%m-%d_%H-%M-%S')}",
                                                                logDirTimestampedOptSuffix = f"-{self.ptbConfig.targetTps}")

        self.trxGenLogDirPath = self.loggingConfig.logDirPath/Path("trxGenLogs")
        self.varLogsDirPath = self.loggingConfig.logDirPath/Path("var")
        self.etcLogsDirPath = self.loggingConfig.logDirPath/Path("etc")
        self.etcEosioLogsDirPath = self.etcLogsDirPath/Path("eosio")
        self.blockDataLogDirPath = self.loggingConfig.logDirPath/Path("blockDataLogs")
        self.blockDataPath = self.blockDataLogDirPath/Path("blockData.txt")
        self.blockTrxDataPath = self.blockDataLogDirPath/Path("blockTrxData.txt")
        self.reportPath = self.loggingConfig.logDirPath/Path("data.json")

        # Setup Expectations for Producer and Validation Node IDs
        # Producer Nodes are index [0, pnodes) and validation nodes/non-producer nodes [pnodes, _totalNodes)
        # Use first producer node and first non-producer node
        self.producerNodeId = 0
        self.validationNodeId = self.clusterConfig.pnodes
        self.nodeosLogPath = Path("var")/"lib"/f"node_{str(self.validationNodeId).zfill(2)}"/"stderr.txt"

        # Setup cluster and its wallet manager
        self.walletMgr=WalletMgr(True)
        self.cluster=Cluster(walletd=True, loggingLevel="info", loggingLevelDict=self.clusterConfig.loggingDict)
        self.cluster.setWalletMgr(self.walletMgr)

    def cleanupOldClusters(self):
        self.cluster.killall(allInstances=self.testHelperConfig.killAll)
        self.cluster.cleanup()

    def testDirsCleanup(self, delReport: bool=False):
        try:
            def removeArtifacts(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if Path(path).is_dir():
                    print(f"Cleaning up test artifacts dir and all contents of: {path}")
                    shutil.rmtree(f"{path}")

            def removeAllArtifactsExceptFinalReport():
                removeArtifacts(self.trxGenLogDirPath)
                removeArtifacts(self.varLogsDirPath)
                removeArtifacts(self.etcEosioLogsDirPath)
                removeArtifacts(self.etcLogsDirPath)
                removeArtifacts(self.blockDataLogDirPath)

            if not delReport:
                removeAllArtifactsExceptFinalReport()
            else:
                removeArtifacts(self.loggingConfig.logDirPath)
        except OSError as error:
            print(error)

    def testDirsSetup(self):
        try:
            def createArtifactsDir(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if not Path(path).is_dir():
                    print(f"Creating test artifacts dir: {path}")
                    os.mkdir(f"{path}")

            createArtifactsDir(self.ptbConfig.logDirRoot)
            createArtifactsDir(self.loggingConfig.logDirBase)
            createArtifactsDir(self.loggingConfig.logDirPath)
            createArtifactsDir(self.trxGenLogDirPath)
            createArtifactsDir(self.varLogsDirPath)
            createArtifactsDir(self.etcLogsDirPath)
            createArtifactsDir(self.etcEosioLogsDirPath)
            createArtifactsDir(self.blockDataLogDirPath)

        except OSError as error:
            print(error)

    def fileOpenMode(self, filePath) -> str:
        if filePath.exists():
            append_write = 'a'
        else:
            append_write = 'w'
        return append_write

    def queryBlockTrxData(self, node, blockDataPath, blockTrxDataPath, startBlockNum, endBlockNum):
        for blockNum in range(startBlockNum, endBlockNum):
            block = node.processUrllibRequest("trace_api", "get_block", {"block_num":blockNum}, silentErrors=False, exitOnError=True)
            btdf_append_write = self.fileOpenMode(blockTrxDataPath)
            with open(blockTrxDataPath, btdf_append_write) as trxDataFile:
                [trxDataFile.write(f"{trx['id']},{trx['block_num']},{trx['cpu_usage_us']},{trx['net_usage_words']}\n") for trx in block['payload']['transactions'] if block['payload']['transactions']]

            bdf_append_write = self.fileOpenMode(blockDataPath)
            with open(blockDataPath, bdf_append_write) as blockDataFile:
                blockDataFile.write(f"{block['payload']['number']},{block['payload']['id']},{block['payload']['producer']},{block['payload']['status']},{block['payload']['timestamp']}\n")

    def waitForEmptyBlocks(self, node, numEmptyToWaitOn):
        emptyBlocks = 0
        while emptyBlocks < numEmptyToWaitOn:
            headBlock = node.getHeadBlockNum()
            block = node.processUrllibRequest("chain", "get_block_info", {"block_num":headBlock}, silentErrors=False, exitOnError=True)
            node.waitForHeadToAdvance()
            if block['payload']['transaction_mroot'] == "0000000000000000000000000000000000000000000000000000000000000000":
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
            extraNodeosArgs=str(self.clusterConfig.extraNodeosArgs),
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

    def runTpsTest(self) -> PtbTpsTestResult:
        completedRun = False
        self.producerNode = self.cluster.getNode(self.producerNodeId)
        self.validationNode = self.cluster.getNode(self.validationNodeId)
        info = self.producerNode.getInfo()
        chainId = info['chain_id']
        lib_id = info['last_irreversible_block_id']
        self.data = log_reader.chainData()

        self.cluster.biosNode.kill(signal.SIGTERM)

        self.data.startBlock = self.waitForEmptyBlocks(self.validationNode, self.emptyBlockGoal)
        tpsTrxGensConfig = ltg.TpsTrxGensConfig(targetTps=self.ptbConfig.targetTps, tpsLimitPerGenerator=self.ptbConfig.tpsLimitPerGenerator)
        trxGenLauncher = ltg.TransactionGeneratorsLauncher(chainId=chainId, lastIrreversibleBlockId=lib_id,
                                                           handlerAcct=self.cluster.eosioAccount.name, accts=f"{self.account1Name},{self.account2Name}",
                                                           privateKeys=f"{self.account1PrivKey},{self.account2PrivKey}", trxGenDurationSec=self.ptbConfig.testTrxGenDurationSec,
                                                           logDir=self.trxGenLogDirPath, tpsTrxGensConfig=tpsTrxGensConfig)

        trxGenExitCodes = trxGenLauncher.launch()
        print(f"Transaction Generator exit codes: {trxGenExitCodes}")
        for exitCode in trxGenExitCodes:
            if exitCode != 0:
                completedRun = False
                break
        else:
            completedRun = True

        # Get stats after transaction generation stops
        trxSent = {}
        log_reader.scrapeTrxGenTrxSentDataLogs(trxSent, self.trxGenLogDirPath, self.ptbConfig.quiet)
        blocksToWait = 2 * self.ptbConfig.testTrxGenDurationSec + 10
        trxSent = self.validationNode.waitForTransactionsInBlockRange(trxSent, self.data.startBlock, blocksToWait)
        self.data.ceaseBlock = self.validationNode.getHeadBlockNum()

        return PerformanceTestBasic.PtbTpsTestResult(completedRun=completedRun, numGeneratorsUsed=tpsTrxGensConfig.numGenerators,
                                                     targetTpsPerGenList=tpsTrxGensConfig.targetTpsPerGenList, trxGenExitCodes=trxGenExitCodes)

    def prepArgs(self) -> dict:
        args = {}
        args.update(asdict(self.testHelperConfig))
        args.update(asdict(self.clusterConfig))
        args.update(asdict(self.ptbConfig))
        args.update(asdict(self.loggingConfig))
        return args

    def captureLowLevelArtifacts(self):
        try:
            shutil.move(f"var", f"{self.varLogsDirPath}")
        except Exception as e:
            print(f"Failed to move 'var' to '{self.varLogsDirPath}': {type(e)}: {e}")

        etcEosioDir = Path("etc")/"eosio"
        for path in os.listdir(etcEosioDir):
            if path == "launcher":
                try:
                    # Need to copy here since testnet.template is only generated at compile time then reused, therefore
                    # it needs to remain in etc/eosio/launcher for subsequent tests.
                    shutil.copytree(etcEosioDir/Path(path), self.etcEosioLogsDirPath/Path(path))
                except Exception as e:
                    print(f"Failed to copy '{etcEosioDir}/{path}' to '{self.etcEosioLogsDirPath}/{path}': {type(e)}: {e}")
            else:
                try:
                    shutil.move(etcEosioDir/Path(path), self.etcEosioLogsDirPath/Path(path))
                except Exception as e:
                    print(f"Failed to move '{etcEosioDir}/{path}' to '{self.etcEosioLogsDirPath}/{path}': {type(e)}: {e}")


    def analyzeResultsAndReport(self, testResult: PtbTpsTestResult):
        args = self.prepArgs()
        artifactsLocate = log_reader.ArtifactPaths(nodeosLogPath=self.nodeosLogPath, trxGenLogDirPath=self.trxGenLogDirPath, blockTrxDataPath=self.blockTrxDataPath,
                                                   blockDataPath=self.blockDataPath)
        tpsTestConfig = log_reader.TpsTestConfig(targetTps=self.ptbConfig.targetTps, testDurationSec=self.ptbConfig.testTrxGenDurationSec, tpsLimitPerGenerator=self.ptbConfig.tpsLimitPerGenerator,
                                                 numBlocksToPrune=self.ptbConfig.numAddlBlocksToPrune, numTrxGensUsed=testResult.numGeneratorsUsed,
                                                 targetTpsPerGenList=testResult.targetTpsPerGenList, quiet=self.ptbConfig.quiet)
        self.report = log_reader.calcAndReport(data=self.data, tpsTestConfig=tpsTestConfig, artifacts=artifactsLocate, argsDict=args, testStart=self.testStart,
                                               completedRun=testResult.completedRun)

        jsonReport = None
        if not self.ptbConfig.quiet or not self.ptbConfig.delReport:
            jsonReport = log_reader.reportAsJSON(self.report)

        if not self.ptbConfig.quiet:
            print(self.data)

            print(f"Report:\n{jsonReport}")

        if not self.ptbConfig.delReport:
            log_reader.exportReportAsJSON(jsonReport, self.reportPath)

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

            self.ptbTestResult = self.runTpsTest()

            self.postTpsTestSteps()

            self.analyzeResultsAndReport(self.ptbTestResult)

            testSuccessful = self.ptbTestResult.completedRun

            if not self.PtbTpsTestResult.completedRun:
                for exitCode in self.ptbTestResult.trxGenExitCodes:
                    if exitCode != 0:
                        print(f"Error: Transaction Generator exited with error {exitCode}")

            if testSuccessful and self.ptbConfig.expectedTransactionsSent != self.data.totalTransactions:
                testSuccessful = False
                print(f"Error: Transactions received: {self.data.totalTransactions} did not match expected total: {self.ptbConfig.expectedTransactionsSent}")

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

            if not self.ptbConfig.delPerfLogs:
                self.captureLowLevelArtifacts()

            if not completedRun:
                os.system("pkill trx_generator")
                print("Test run cancelled early via SIGINT")

            if self.ptbConfig.delPerfLogs:
                print(f"Cleaning up logs directory: {self.loggingConfig.logDirPath}")
                self.testDirsCleanup(self.ptbConfig.delReport)

            return testSuccessful

class PtbArgumentsHandler(object):
    @staticmethod
    def createBaseArgumentParser():
        testHelperArgParser=TestHelper.createArgumentParser(includeArgs={"-p","-n","-d","-s","--nodes-file"
                                                        ,"--dump-error-details","-v","--leave-running"
                                                        ,"--clean-run"})
        ptbBaseParser = argparse.ArgumentParser(parents=[testHelperArgParser], add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptbBaseGrpTitle="Performance Test Basic Base"
        ptbBaseGrpDescription="Performance Test Basic base configuration items."
        ptbBaseParserGroup = ptbBaseParser.add_argument_group(title=ptbBaseGrpTitle, description=ptbBaseGrpDescription)

        ptbBaseParserGroup.add_argument("--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
        ptbBaseParserGroup.add_argument("--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
        ptbBaseParserGroup.add_argument("--num-blocks-to-prune", type=int, help=("The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, "
                                                                "to prune from the beginning and end of the range of blocks of interest for evaluation."), default=2)
        ptbBaseParserGroup.add_argument("--signature-cpu-billable-pct", type=int, help="Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50%%", default=0)
        ptbBaseParserGroup.add_argument("--chain-state-db-size-mb", type=int, help="Maximum size (in MiB) of the chain state database", default=10*1024)
        ptbBaseParserGroup.add_argument("--chain-threads", type=int, help="Number of worker threads in controller thread pool", default=2)
        ptbBaseParserGroup.add_argument("--database-map-mode", type=str, help="Database map mode (\"mapped\", \"heap\", or \"locked\"). \
                                                                In \"mapped\" mode database is memory mapped as a file. \
                                                                In \"heap\" mode database is preloaded in to swappable memory and will use huge pages if available. \
                                                                In \"locked\" mode database is preloaded, locked in to memory, and will use huge pages if available.",
                                                                choices=["mapped", "heap", "locked"], default="mapped")
        ptbBaseParserGroup.add_argument("--net-threads", type=int, help="Number of worker threads in net_plugin thread pool", default=2)
        ptbBaseParserGroup.add_argument("--disable-subjective-billing", type=bool, help="Disable subjective CPU billing for API/P2P transactions", default=True)
        ptbBaseParserGroup.add_argument("--last-block-time-offset-us", type=int, help="Offset of last block producing time in microseconds. Valid range 0 .. -block_time_interval.", default=0)
        ptbBaseParserGroup.add_argument("--produce-time-offset-us", type=int, help="Offset of non last block producing time in microseconds. Valid range 0 .. -block_time_interval.", default=0)
        ptbBaseParserGroup.add_argument("--cpu-effort-percent", type=int, help="Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%%", default=100)
        ptbBaseParserGroup.add_argument("--last-block-cpu-effort-percent", type=int, help="Percentage of cpu block production time used to produce last block. Whole number percentages, e.g. 80 for 80%%", default=100)
        ptbBaseParserGroup.add_argument("--producer-threads", type=int, help="Number of worker threads in producer thread pool", default=2)
        ptbBaseParserGroup.add_argument("--http-max-response-time-ms", type=int, help="Maximum time for processing a request, -1 for unlimited", default=990000)
        ptbBaseParserGroup.add_argument("--del-perf-logs", help="Whether to delete performance test specific logs.", action='store_true')
        ptbBaseParserGroup.add_argument("--del-report", help="Whether to delete overarching performance run report.", action='store_true')
        ptbBaseParserGroup.add_argument("--quiet", help="Whether to quiet printing intermediate results and reports to stdout", action='store_true')
        ptbBaseParserGroup.add_argument("--prods-enable-trace-api", help="Determines whether producer nodes should have eosio::trace_api_plugin enabled", action='store_true')
        return ptbBaseParser

    @staticmethod
    def createArgumentParser():
        ptbBaseParser = PtbArgumentsHandler.createBaseArgumentParser()

        ptbParser = argparse.ArgumentParser(parents=[ptbBaseParser], add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptbGrpTitle="Performance Test Basic Single Test"
        ptbGrpDescription="Performance Test Basic single test configuration items. Useful for running a single test directly. \
                           These items may not be directly configurable from higher level scripts as the scripts themselves may configure these internally."
        ptbParserGroup = ptbParser.add_argument_group(title=ptbGrpTitle, description=ptbGrpDescription)

        ptbParserGroup.add_argument("--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
        ptbParserGroup.add_argument("--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=90)
        return ptbParser

    @staticmethod
    def parseArgs():
        ptbParser=PtbArgumentsHandler.createArgumentParser()
        args=ptbParser.parse_args()
        return args

def main():

    args = PtbArgumentsHandler.parseArgs()
    Utils.Debug = args.v

    testHelperConfig = PerformanceTestBasic.TestHelperConfig(killAll=args.clean_run, dontKill=args.leave_running, keepLogs=not args.del_perf_logs,
                                                             dumpErrorDetails=args.dump_error_details, delay=args.d, nodesFile=args.nodes_file, verbose=args.v)

    chainPluginArgs = ChainPluginArgs(signatureCpuBillablePct=args.signature_cpu_billable_pct, chainStateDbSizeMb=args.chain_state_db_size_mb,
                                      chainThreads=args.chain_threads, databaseMapMode=args.database_map_mode)
    producerPluginArgs = ProducerPluginArgs(disableSubjectiveBilling=args.disable_subjective_billing,
                                            lastBlockTimeOffsetUs=args.last_block_time_offset_us, produceTimeOffsetUs=args.produce_time_offset_us,
                                            cpuEffortPercent=args.cpu_effort_percent, lastBlockCpuEffortPercent=args.last_block_cpu_effort_percent,
                                            producerThreads=args.producer_threads)
    httpPluginArgs = HttpPluginArgs(httpMaxResponseTimeMs=args.http_max_response_time_ms)
    netPluginArgs = NetPluginArgs(netThreads=args.net_threads)
    ENA = PerformanceTestBasic.ClusterConfig.ExtraNodeosArgs
    extraNodeosArgs = ENA(chainPluginArgs=chainPluginArgs, httpPluginArgs=httpPluginArgs, producerPluginArgs=producerPluginArgs, netPluginArgs=netPluginArgs)
    testClusterConfig = PerformanceTestBasic.ClusterConfig(pnodes=args.p, totalNodes=args.n, topo=args.s, genesisPath=args.genesis,
                                                           prodsEnableTraceApi=args.prods_enable_trace_api, extraNodeosArgs=extraNodeosArgs)
    ptbConfig = PerformanceTestBasic.PtbConfig(targetTps=args.target_tps, testTrxGenDurationSec=args.test_duration_sec, tpsLimitPerGenerator=args.tps_limit_per_generator,
                                  numAddlBlocksToPrune=args.num_blocks_to_prune, logDirRoot=".", delReport=args.del_report, quiet=args.quiet, delPerfLogs=args.del_perf_logs)
    myTest = PerformanceTestBasic(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, ptbConfig=ptbConfig)
    testSuccessful = myTest.runTest()

    exitCode = 0 if testSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
