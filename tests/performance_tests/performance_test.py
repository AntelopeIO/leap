#!/usr/bin/env python3

import argparse
import copy
import math
import os
import sys
import json
import shutil

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

from NodeosPluginArgs import ChainPluginArgs, HttpPluginArgs, NetPluginArgs, ProducerPluginArgs, ResourceMonitorPluginArgs
from TestHarness import TestHelper, Utils, Account
from performance_test_basic import PerformanceTestBasic, PtbArgumentsHandler
from platform import release, system
from dataclasses import dataclass, asdict, field
from datetime import datetime
from enum import Enum
from log_reader import LogReaderEncoder

class PerformanceTest:

    @dataclass
    class PerfTestSearchIndivResult:
        @dataclass
        class PerfTestBasicResult:
            targetTPS: int = 0
            resultAvgTps: float = 0
            expectedTxns: int = 0
            resultTxns: int = 0
            tpsExpectMet: bool = False
            trxExpectMet: bool = False
            basicTestSuccess: bool = False
            testAnalysisBlockCnt: int = 0
            logsDir: Path = Path("")
            testStart: datetime = None
            testEnd: datetime = None

        success: bool = False
        searchTarget: int = 0
        searchFloor: int = 0
        searchCeiling: int = 0
        basicTestResult: PerfTestBasicResult = field(default_factory=PerfTestBasicResult)

    @dataclass
    class PtConfig:
        testDurationSec: int=150
        finalDurationSec: int=300
        delPerfLogs: bool=False
        maxTpsToTest: int=50000
        testIterationMinStep: int=500
        tpsLimitPerGenerator: int=4000
        delReport: bool=False
        delTestReport: bool=False
        numAddlBlocksToPrune: int=2
        quiet: bool=False
        logDirRoot: Path=Path(".")
        skipTpsTests: bool=False
        calcProducerThreads: str="none"
        calcChainThreads: str="none"
        calcNetThreads: str="none"
        userTrxDataFile: Path=None


    @dataclass
    class TpsTestResult:
        @dataclass
        class PerfTestSearchResults:
            maxTpsAchieved: int = 0
            searchResults: list = field(default_factory=list) #PerfTestSearchIndivResult list
            maxTpsReport: dict = field(default_factory=dict)

        binSearchResults: PerfTestSearchResults = field(default_factory=PerfTestSearchResults)
        longRunningSearchResults: PerfTestSearchResults= field(default_factory=PerfTestSearchResults)
        tpsTestStart: datetime=datetime.utcnow()
        tpsTestFinish: datetime=datetime.utcnow()
        perfRunSuccessful: bool=False

    @dataclass
    class LoggingConfig:
        logDirBase: Path = Path(".")/PurePath(PurePath(__file__).name).stem
        logDirTimestamp: str = f"{datetime.utcnow().strftime('%Y-%m-%d_%H-%M-%S')}"
        logDirPath: Path = field(default_factory=Path, init=False)
        ptbLogsDirPath: Path = field(default_factory=Path, init=False)
        pluginThreadOptLogsDirPath: Path = field(default_factory=Path, init=False)

        def __post_init__(self):
            self.logDirPath = self.logDirBase/Path(self.logDirTimestamp)
            self.ptbLogsDirPath = self.logDirPath/Path("testRunLogs")
            self.pluginThreadOptLogsDirPath = self.logDirPath/Path("pluginThreadOptRunLogs")

    def __init__(self, testHelperConfig: PerformanceTestBasic.TestHelperConfig=PerformanceTestBasic.TestHelperConfig(),
                 clusterConfig: PerformanceTestBasic.ClusterConfig=PerformanceTestBasic.ClusterConfig(), ptConfig=PtConfig()):
        self.testHelperConfig = testHelperConfig
        self.clusterConfig = clusterConfig
        self.ptConfig = ptConfig

        self.testsStart = datetime.utcnow()

        self.loggingConfig = PerformanceTest.LoggingConfig(logDirBase=Path(self.ptConfig.logDirRoot)/PurePath(PurePath(__file__).name).stem,
                                                           logDirTimestamp=f"{self.testsStart.strftime('%Y-%m-%d_%H-%M-%S')}")

    def performPtbBinarySearch(self, clusterConfig: PerformanceTestBasic.ClusterConfig, logDirRoot: Path, delReport: bool, quiet: bool, delPerfLogs: bool) -> TpsTestResult.PerfTestSearchResults:
        floor = 1
        ceiling = self.ptConfig.maxTpsToTest
        binSearchTarget = self.ptConfig.maxTpsToTest
        minStep = self.ptConfig.testIterationMinStep

        maxTpsAchieved = 0
        maxTpsReport = {}
        searchResults = []

        while ceiling >= floor:
            print(f"Running scenario: floor {floor} binSearchTarget {binSearchTarget} ceiling {ceiling}")
            ptbResult = PerformanceTest.PerfTestSearchIndivResult.PerfTestBasicResult()
            scenarioResult = PerformanceTest.PerfTestSearchIndivResult(success=False, searchTarget=binSearchTarget, searchFloor=floor, searchCeiling=ceiling, basicTestResult=ptbResult)
            ptbConfig = PerformanceTestBasic.PtbConfig(targetTps=binSearchTarget, testTrxGenDurationSec=self.ptConfig.testDurationSec, tpsLimitPerGenerator=self.ptConfig.tpsLimitPerGenerator,
                                                       numAddlBlocksToPrune=self.ptConfig.numAddlBlocksToPrune, logDirRoot=logDirRoot, delReport=delReport,
                                                       quiet=quiet, userTrxDataFile=self.ptConfig.userTrxDataFile)

            myTest = PerformanceTestBasic(testHelperConfig=self.testHelperConfig, clusterConfig=clusterConfig, ptbConfig=ptbConfig,  testNamePath="performance_test")
            testSuccessful = myTest.runTest()
            if self.evaluateSuccess(myTest, testSuccessful, ptbResult):
                maxTpsAchieved = binSearchTarget
                maxTpsReport = myTest.report
                floor = binSearchTarget + minStep
                scenarioResult.success = True
            else:
                ceiling = binSearchTarget - minStep

            scenarioResult.basicTestResult = ptbResult
            searchResults.append(scenarioResult)
            if not self.ptConfig.quiet:
                print(f"searchResult: {binSearchTarget} : {searchResults[-1]}")

            binSearchTarget = floor + (math.ceil(((ceiling - floor) / minStep) / 2) * minStep)

        return PerformanceTest.TpsTestResult.PerfTestSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

    def performPtbReverseLinearSearch(self, tpsInitial: int) -> TpsTestResult.PerfTestSearchResults:

        # Default - Decrementing Max TPS in range [1, tpsInitial]
        absFloor = 1
        tpsInitial = absFloor if tpsInitial <= 0 else tpsInitial
        absCeiling = tpsInitial

        step = self.ptConfig.testIterationMinStep

        searchTarget = tpsInitial

        maxTpsAchieved = 0
        maxTpsReport = {}
        searchResults = []
        maxFound = False

        while not maxFound:
            print(f"Running scenario: floor {absFloor} searchTarget {searchTarget} ceiling {absCeiling}")
            ptbResult = PerformanceTest.PerfTestSearchIndivResult.PerfTestBasicResult()
            scenarioResult = PerformanceTest.PerfTestSearchIndivResult(success=False, searchTarget=searchTarget, searchFloor=absFloor, searchCeiling=absCeiling, basicTestResult=ptbResult)
            ptbConfig = PerformanceTestBasic.PtbConfig(targetTps=searchTarget, testTrxGenDurationSec=self.ptConfig.testDurationSec, tpsLimitPerGenerator=self.ptConfig.tpsLimitPerGenerator,
                                                    numAddlBlocksToPrune=self.ptConfig.numAddlBlocksToPrune, logDirRoot=self.loggingConfig.ptbLogsDirPath, delReport=self.ptConfig.delReport,
                                                    quiet=self.ptConfig.quiet, delPerfLogs=self.ptConfig.delPerfLogs, userTrxDataFile=self.ptConfig.userTrxDataFile)

            myTest = PerformanceTestBasic(testHelperConfig=self.testHelperConfig, clusterConfig=self.clusterConfig, ptbConfig=ptbConfig,  testNamePath="performance_test")
            testSuccessful = myTest.runTest()
            if self.evaluateSuccess(myTest, testSuccessful, ptbResult):
                maxTpsAchieved = searchTarget
                maxTpsReport = myTest.report
                scenarioResult.success = True
                maxFound = True
            else:
                if searchTarget <= absFloor:
                    # This means it has already run a search at absFloor, and failed, so exit.
                    maxFound = True
                searchTarget = max(searchTarget - step, absFloor)

            scenarioResult.basicTestResult = ptbResult
            searchResults.append(scenarioResult)
            if not self.ptConfig.quiet:
                print(f"searchResult: {searchTarget} : {searchResults[-1]}")

        return PerformanceTest.TpsTestResult.PerfTestSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

    def evaluateSuccess(self, test: PerformanceTestBasic, testSuccessful: bool, result: PerfTestSearchIndivResult.PerfTestBasicResult) -> bool:
        result.targetTPS = test.ptbConfig.targetTps
        result.expectedTxns = test.ptbConfig.expectedTransactionsSent
        reportDict = test.report
        result.testStart = reportDict["testStart"]
        result.testEnd = reportDict["testFinish"]
        result.resultAvgTps = reportDict["Analysis"]["TPS"]["avg"]
        result.resultTxns = reportDict["Analysis"]["TrxLatency"]["samples"]
        print(f"targetTPS: {result.targetTPS} expectedTxns: {result.expectedTxns} resultAvgTps: {result.resultAvgTps} resultTxns: {result.resultTxns}")

        result.tpsExpectMet = True if result.resultAvgTps >= result.targetTPS else abs(result.targetTPS - result.resultAvgTps) < 100
        result.trxExpectMet = result.expectedTxns == result.resultTxns
        result.basicTestSuccess = testSuccessful
        result.testAnalysisBlockCnt = reportDict["Analysis"]["BlocksGuide"]["testAnalysisBlockCnt"]
        result.logsDir = test.loggingConfig.logDirPath

        print(f"basicTestSuccess: {result.basicTestSuccess} tpsExpectationMet: {result.tpsExpectMet} trxExpectationMet: {result.trxExpectMet}")

        return result.basicTestSuccess and result.tpsExpectMet and result.trxExpectMet

    class PluginThreadOpt(Enum):
        PRODUCER = "producer"
        CHAIN = "chain"
        NET = "net"

    class PluginThreadOptRunType(Enum):
        FULL = 1
        LOCAL_MAX = 2

    @dataclass
    class PluginThreadOptResult:
        recommendedThreadCount: int = 0
        threadToMaxTpsDict: dict = field(default_factory=dict)
        analysisStart: datetime = datetime.utcnow()
        analysisFinish: datetime = datetime.utcnow()

    def optimizePluginThreadCount(self,  optPlugin: PluginThreadOpt, optType: PluginThreadOptRunType=PluginThreadOptRunType.LOCAL_MAX,
                                  minThreadCount: int=2, maxThreadCount: int=os.cpu_count()) -> PluginThreadOptResult:

        resultsFile = self.loggingConfig.pluginThreadOptLogsDirPath/Path(f"{optPlugin.value}ThreadResults.txt")

        threadToMaxTpsDict: dict = {}

        clusterConfig = copy.deepcopy(self.clusterConfig)
        analysisStart = datetime.utcnow()

        with open(resultsFile, 'w') as log:
            log.write(f"{optPlugin.value}Threads, maxTpsAchieved\n")

        lastMaxTpsAchieved = 0
        for threadCount in range(minThreadCount, maxThreadCount+1):
            print(f"Running {optPlugin.value} thread count optimization check with {threadCount} {optPlugin.value} threads")

            setattr(getattr(clusterConfig.extraNodeosArgs, optPlugin.value + 'PluginArgs'), f"{optPlugin.value}Threads", threadCount)

            binSearchResults = self.performPtbBinarySearch(clusterConfig=clusterConfig, logDirRoot=self.loggingConfig.pluginThreadOptLogsDirPath,
                                                            delReport=True, quiet=False, delPerfLogs=True)

            threadToMaxTpsDict[threadCount] = binSearchResults.maxTpsAchieved
            if not self.ptConfig.quiet:
                print("Search Results:")
                for i in range(len(binSearchResults.searchResults)):
                    print(f"Search scenario {optPlugin.value} thread count {threadCount}: {i} result: {binSearchResults.searchResults[i]}")

            with open(resultsFile, 'a') as log:
                log.write(f"{threadCount},{binSearchResults.maxTpsAchieved}\n")

            if optType == PerformanceTest.PluginThreadOptRunType.LOCAL_MAX:
                if binSearchResults.maxTpsAchieved <= lastMaxTpsAchieved:
                    break
            lastMaxTpsAchieved = binSearchResults.maxTpsAchieved

        analysisFinish = datetime.utcnow()

        def calcLocalMax(threadToMaxDict: dict):
            localMax = 0
            recThreadCount = 0
            for threads, tps in threadToMaxDict.items():
                if tps > localMax:
                    localMax = tps
                    recThreadCount = threads
                else:
                    break
            return recThreadCount

        recommendedThreadCount = calcLocalMax(threadToMaxDict=threadToMaxTpsDict)

        return PerformanceTest.PluginThreadOptResult(recommendedThreadCount=recommendedThreadCount, threadToMaxTpsDict=threadToMaxTpsDict,
                                                     analysisStart=analysisStart, analysisFinish=analysisFinish)

    def createTpsTestReport(self, tpsTestResult: TpsTestResult) -> dict:
        report = {}
        report['InitialMaxTpsAchieved'] = tpsTestResult.binSearchResults.maxTpsAchieved
        report['LongRunningMaxTpsAchieved'] = tpsTestResult.longRunningSearchResults.maxTpsAchieved
        report['tpsTestStart'] = tpsTestResult.tpsTestStart
        report['tpsTestFinish'] = tpsTestResult.tpsTestFinish
        report['InitialSearchResults'] =  {x: asdict(tpsTestResult.binSearchResults.searchResults[x]) for x in range(len(tpsTestResult.binSearchResults.searchResults))}
        report['InitialMaxTpsReport'] =  tpsTestResult.binSearchResults.maxTpsReport
        report['LongRunningSearchResults'] =  {x: asdict(tpsTestResult.longRunningSearchResults.searchResults[x]) for x in range(len(tpsTestResult.longRunningSearchResults.searchResults))}
        report['LongRunningMaxTpsReport'] =  tpsTestResult.longRunningSearchResults.maxTpsReport
        return report

    def createReport(self, producerThreadResult: PluginThreadOptResult=None, chainThreadResult: PluginThreadOptResult=None, netThreadResult: PluginThreadOptResult=None,
                     tpsTestResult: dict=None, nodeosVers: str="") -> dict:
        report = {}
        report['perfTestsBegin'] = self.testsStart
        report['perfTestsFinish'] = self.testsFinish
        if tpsTestResult is not None:
            report.update(self.createTpsTestReport(tpsTestResult))

        if producerThreadResult is not None:
            report['ProducerThreadAnalysis'] = asdict(producerThreadResult)

        if chainThreadResult is not None:
            report['ChainThreadAnalysis'] = asdict(chainThreadResult)

        if netThreadResult is not None:
            report['NetThreadAnalysis'] = asdict(netThreadResult)

        report['args'] =  self.prepArgsDict()
        report['env'] = {'system': system(), 'os': os.name, 'release': release(), 'logical_cpu_count': os.cpu_count()}
        report['nodeosVersion'] = nodeosVers
        return report

    def reportAsJSON(self, report: dict) -> json:
        return json.dumps(report, indent=2, cls=LogReaderEncoder)

    def exportReportAsJSON(self, report: json, exportPath):
        with open(exportPath, 'wt') as f:
            f.write(report)

    def testDirsCleanup(self):
        try:
            def removeArtifacts(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if Path(path).is_dir():
                    print(f"Cleaning up test artifacts dir and all contents of: {path}")
                    shutil.rmtree(f"{path}")

            if not self.ptConfig.delReport:
                removeArtifacts(self.loggingConfig.ptbLogsDirPath)
                removeArtifacts(self.loggingConfig.pluginThreadOptLogsDirPath)
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

            createArtifactsDir(self.loggingConfig.logDirBase)
            createArtifactsDir(self.loggingConfig.logDirPath)
            createArtifactsDir(self.loggingConfig.ptbLogsDirPath)
            createArtifactsDir(self.loggingConfig.pluginThreadOptLogsDirPath)

        except OSError as error:
            print(error)

    def prepArgsDict(self) -> dict:
        argsDict = {}
        argsDict.update({"rawCmdLine ": ' '.join(sys.argv[0:])})
        argsDict.update(asdict(self.testHelperConfig))
        argsDict.update(asdict(self.clusterConfig))
        argsDict.update(asdict(self.ptConfig))
        argsDict.update(asdict(self.loggingConfig))
        return argsDict

    def performTpsTest(self) -> TpsTestResult:
        tpsTestStart = datetime.utcnow()
        perfRunSuccessful = False

        binSearchResults = self.performPtbBinarySearch(clusterConfig=self.clusterConfig, logDirRoot=self.loggingConfig.ptbLogsDirPath,
                                                            delReport=self.ptConfig.delReport, quiet=self.ptConfig.quiet, delPerfLogs=self.ptConfig.delPerfLogs)

        print(f"Successful rate of: {binSearchResults.maxTpsAchieved}")

        if not self.ptConfig.quiet:
            print("Search Results:")
            for i in range(len(binSearchResults.searchResults)):
                print(f"Search scenario: {i} result: {binSearchResults.searchResults[i]}")

        longRunningSearchResults = self.performPtbReverseLinearSearch(tpsInitial=binSearchResults.maxTpsAchieved)

        print(f"Long Running Test - Successful rate of: {longRunningSearchResults.maxTpsAchieved}")
        perfRunSuccessful = True

        if not self.ptConfig.quiet:
            print("Long Running Test - Search Results:")
            for i in range(len(longRunningSearchResults.searchResults)):
                print(f"Search scenario: {i} result: {longRunningSearchResults.searchResults[i]}")

        tpsTestFinish = datetime.utcnow()

        return PerformanceTest.TpsTestResult(binSearchResults=binSearchResults, longRunningSearchResults=longRunningSearchResults,
                                             tpsTestStart=tpsTestStart, tpsTestFinish=tpsTestFinish, perfRunSuccessful=perfRunSuccessful)

    def runTest(self):
        testSuccessful = True

        TestHelper.printSystemInfo("BEGIN")
        self.testDirsCleanup()
        self.testDirsSetup()

        prodResults = None
        if self.ptConfig.calcProducerThreads != "none":
            print(f"Performing Producer Thread Optimization Tests")
            if self.ptConfig.calcProducerThreads == "full":
                optType = PerformanceTest.PluginThreadOptRunType.FULL
            else:
                optType = PerformanceTest.PluginThreadOptRunType.LOCAL_MAX
            prodResults = self.optimizePluginThreadCount(optPlugin=PerformanceTest.PluginThreadOpt.PRODUCER, optType=optType,
                                                         minThreadCount=self.clusterConfig.extraNodeosArgs.producerPluginArgs._producerThreadsNodeosDefault)
            print(f"Producer Thread Optimization results: {prodResults}")
            self.clusterConfig.extraNodeosArgs.producerPluginArgs.threads = prodResults.recommendedThreadCount

        chainResults = None
        if self.ptConfig.calcChainThreads != "none":
            print(f"Performing Chain Thread Optimization Tests")
            if self.ptConfig.calcChainThreads == "full":
                optType = PerformanceTest.PluginThreadOptRunType.FULL
            else:
                optType = PerformanceTest.PluginThreadOptRunType.LOCAL_MAX
            chainResults = self.optimizePluginThreadCount(optPlugin=PerformanceTest.PluginThreadOpt.CHAIN, optType=optType,
                                                          minThreadCount=self.clusterConfig.extraNodeosArgs.chainPluginArgs._chainThreadsNodeosDefault)
            print(f"Chain Thread Optimization results: {chainResults}")
            self.clusterConfig.extraNodeosArgs.chainPluginArgs.threads = chainResults.recommendedThreadCount

        netResults = None
        if self.ptConfig.calcNetThreads != "none":
            print(f"Performing Net Thread Optimization Tests")
            if self.ptConfig.calcNetThreads == "full":
                optType = PerformanceTest.PluginThreadOptRunType.FULL
            else:
                optType = PerformanceTest.PluginThreadOptRunType.LOCAL_MAX
            netResults = self.optimizePluginThreadCount(optPlugin=PerformanceTest.PluginThreadOpt.NET, optType=optType,
                                                        minThreadCount=self.clusterConfig.extraNodeosArgs.netPluginArgs._netThreadsNodeosDefault)
            print(f"Net Thread Optimization results: {netResults}")
            self.clusterConfig.extraNodeosArgs.netPluginArgs.threads = netResults.recommendedThreadCount

        tpsTestResult = None
        if not self.ptConfig.skipTpsTests:
            print(f"Performing TPS Performance Tests")
            testSuccessful = False
            tpsTestResult = self.performTpsTest()
            testSuccessful = tpsTestResult.perfRunSuccessful

        self.testsFinish = datetime.utcnow()

        self.report = self.createReport(producerThreadResult=prodResults, chainThreadResult=chainResults, netThreadResult=netResults, tpsTestResult=tpsTestResult, nodeosVers=self.clusterConfig.nodeosVers)
        jsonReport = self.reportAsJSON(self.report)

        if not self.ptConfig.quiet:
            print(f"Full Performance Test Report: {jsonReport}")

        if not self.ptConfig.delReport:
            self.exportReportAsJSON(jsonReport, self.loggingConfig.logDirPath/Path("report.json"))

        if self.ptConfig.delPerfLogs:
            print(f"Cleaning up logs directory: {self.loggingConfig.logDirPath}")
            self.testDirsCleanup()

        return testSuccessful

class PerfTestArgumentsHandler(object):
    @staticmethod
    def createArgumentParser():
        ptbArgParser = PtbArgumentsHandler.createBaseArgumentParser()
        ptParser = argparse.ArgumentParser(parents=[ptbArgParser], add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptGrpTitle="Performance Harness"
        ptGrpDescription="Performance Harness testing configuration items."
        ptParserGroup = ptParser.add_argument_group(title=ptGrpTitle, description=ptGrpDescription)
        ptParserGroup.add_argument("--skip-tps-test", help="Determines whether to skip the max TPS measurement tests", action='store_true')
        ptParserGroup.add_argument("--calc-producer-threads", type=str, help="Determines whether to calculate number of worker threads to use in producer thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                    In \"none\" mode, the default, no calculation will be attempted and the configured --producer-threads value will be used. \
                                                                    In \"lmax\" mode, producer threads will incrementally be tested, starting at plugin default, until the performance rate ceases to increase with the addition of additional threads. \
                                                                    In \"full\" mode producer threads will incrementally be tested from plugin default..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                    Useful for graphing the full performance impact of each available thread.",
                                                                    choices=["none", "lmax", "full"], default="none")
        ptParserGroup.add_argument("--calc-chain-threads", type=str, help="Determines whether to calculate number of worker threads to use in chain thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                    In \"none\" mode, the default, no calculation will be attempted and the configured --chain-threads value will be used. \
                                                                    In \"lmax\" mode, producer threads will incrementally be tested, starting at plugin default, until the performance rate ceases to increase with the addition of additional threads. \
                                                                    In \"full\" mode producer threads will incrementally be tested from plugin default..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                    Useful for graphing the full performance impact of each available thread.",
                                                                    choices=["none", "lmax", "full"], default="none")
        ptParserGroup.add_argument("--calc-net-threads", type=str, help="Determines whether to calculate number of worker threads to use in net thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                    In \"none\" mode, the default, no calculation will be attempted and the configured --net-threads value will be used. \
                                                                    In \"lmax\" mode, producer threads will incrementally be tested, starting at plugin default, until the performance rate ceases to increase with the addition of additional threads. \
                                                                    In \"full\" mode producer threads will incrementally be tested from plugin default..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                    Useful for graphing the full performance impact of each available thread.",
                                                                    choices=["none", "lmax", "full"], default="none")
        ptParserGroup.add_argument("--del-test-report", help="Whether to save json reports from each test scenario.", action='store_true')

        ptTpsGrpTitle="Performance Harness - TPS Test Config"
        ptTpsGrpDescription="TPS Performance Test configuration items."
        ptTpsParserGroup = ptParser.add_argument_group(title=ptTpsGrpTitle, description=ptTpsGrpDescription)

        ptTpsParserGroup.add_argument("--max-tps-to-test", type=int, help="The max target transfers realistic as ceiling of test range", default=50000)
        ptTpsParserGroup.add_argument("--test-iteration-duration-sec", type=int, help="The duration of transfer trx generation for each iteration of the test during the initial search (seconds)", default=150)
        ptTpsParserGroup.add_argument("--test-iteration-min-step", type=int, help="The step size determining granularity of tps result during initial search", default=500)
        ptTpsParserGroup.add_argument("--final-iterations-duration-sec", type=int, help="The duration of transfer trx generation for each final longer run iteration of the test during the final search (seconds)", default=300)

        return ptParser

    @staticmethod
    def parseArgs():
        ptParser=PerfTestArgumentsHandler.createArgumentParser()
        args=ptParser.parse_args()
        return args

def main():

    args = PerfTestArgumentsHandler.parseArgs()
    Utils.Debug = args.v

    testHelperConfig = PerformanceTestBasic.TestHelperConfig(killAll=args.clean_run, dontKill=args.leave_running, keepLogs=not args.del_perf_logs,
                                                             dumpErrorDetails=args.dump_error_details, delay=args.d, nodesFile=args.nodes_file,
                                                             verbose=args.v)

    chainPluginArgs = ChainPluginArgs(signatureCpuBillablePct=args.signature_cpu_billable_pct,
                                      chainThreads=args.chain_threads, databaseMapMode=args.database_map_mode,
                                      wasmRuntime=args.wasm_runtime, contractsConsole=args.contracts_console,
                                      eosVmOcCacheSizeMb=args.eos_vm_oc_cache_size_mb, eosVmOcCompileThreads=args.eos_vm_oc_compile_threads,
                                      blockLogRetainBlocks=args.block_log_retain_blocks,
                                      chainStateDbSizeMb=args.chain_state_db_size_mb, abiSerializerMaxTimeMs=990000)

    lbto = args.last_block_time_offset_us
    lbcep = args.last_block_cpu_effort_percent
    if args.p > 1 and lbto == 0 and lbcep == 100:
        print("Overriding defaults for last_block_time_offset_us and last_block_cpu_effort_percent to ensure proper production windows.")
        lbto = -200000
        lbcep = 80
    producerPluginArgs = ProducerPluginArgs(disableSubjectiveBilling=args.disable_subjective_billing,
                                            lastBlockTimeOffsetUs=lbto, produceTimeOffsetUs=args.produce_time_offset_us,
                                            cpuEffortPercent=args.cpu_effort_percent, lastBlockCpuEffortPercent=lbcep,
                                            producerThreads=args.producer_threads, maxTransactionTime=-1)
    httpPluginArgs = HttpPluginArgs(httpMaxResponseTimeMs=args.http_max_response_time_ms, httpMaxBytesInFlightMb=args.http_max_bytes_in_flight_mb,
                                    httpThreads=args.http_threads)
    netPluginArgs = NetPluginArgs(netThreads=args.net_threads, maxClients=0)
    nodeosVers=Utils.getNodeosVersion().split('.')[0]
    resourceMonitorPluginArgs = ResourceMonitorPluginArgs(resourceMonitorNotShutdownOnThresholdExceeded=not nodeosVers == "v2")
    ENA = PerformanceTestBasic.ClusterConfig.ExtraNodeosArgs
    extraNodeosArgs = ENA(chainPluginArgs=chainPluginArgs, httpPluginArgs=httpPluginArgs, producerPluginArgs=producerPluginArgs, netPluginArgs=netPluginArgs,
                          resourceMonitorPluginArgs=resourceMonitorPluginArgs)
    SC = PerformanceTestBasic.ClusterConfig.SpecifiedContract
    specifiedContract=SC(contractDir=args.contract_dir, wasmFile=args.wasm_file, abiFile=args.abi_file, account=Account(args.account_name))
    testClusterConfig = PerformanceTestBasic.ClusterConfig(pnodes=args.p, totalNodes=args.n, topo=args.s, genesisPath=args.genesis,
                                                           prodsEnableTraceApi=args.prods_enable_trace_api, extraNodeosArgs=extraNodeosArgs,
                                                           specifiedContract=specifiedContract, loggingLevel=args.cluster_log_lvl,
                                                           nodeosVers=nodeosVers, nonProdsEosVmOcEnable=args.non_prods_eos_vm_oc_enable)


    ptConfig = PerformanceTest.PtConfig(testDurationSec=args.test_iteration_duration_sec,
                                        finalDurationSec=args.final_iterations_duration_sec,
                                        delPerfLogs=args.del_perf_logs,
                                        maxTpsToTest=args.max_tps_to_test,
                                        testIterationMinStep=args.test_iteration_min_step,
                                        tpsLimitPerGenerator=args.tps_limit_per_generator,
                                        delReport=args.del_report,
                                        delTestReport=args.del_test_report,
                                        numAddlBlocksToPrune=args.num_blocks_to_prune,
                                        quiet=args.quiet,
                                        logDirRoot=Path("."),
                                        skipTpsTests=args.skip_tps_test,
                                        calcProducerThreads=args.calc_producer_threads,
                                        calcChainThreads=args.calc_chain_threads,
                                        calcNetThreads=args.calc_net_threads,
                                        userTrxDataFile=Path(args.user_trx_data_file) if args.user_trx_data_file is not None else None)

    myTest = PerformanceTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, ptConfig=ptConfig)
    perfRunSuccessful = myTest.runTest()

    exitCode = 0 if perfRunSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
