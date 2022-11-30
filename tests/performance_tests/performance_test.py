#!/usr/bin/env python3

import copy
import math
import os
import sys
import json
import shutil

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import TestHelper, Utils
from TestHarness.TestHelper import AppArgs
from performance_test_basic import PerformanceTestBasic
from platform import release, system
from dataclasses import dataclass, asdict, field
from datetime import datetime
from enum import Enum

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
            logsDir: str = ""
            testStart: datetime = None
            testEnd: datetime = None

        success: bool = False
        searchTarget: int = 0
        searchFloor: int = 0
        searchCeiling: int = 0
        basicTestResult: PerfTestBasicResult = PerfTestBasicResult()

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
        logDirRoot: str="."
        skipTpsTests: bool=False
        calcProducerThreads: str="none"
        calcChainThreads: str="none"
        calcNetThreads: str="none"


    @dataclass
    class TpsTestResult:
        @dataclass
        class PerfTestSearchResults:
            maxTpsAchieved: int = 0
            searchResults: list = field(default_factory=list) #PerfTestSearchIndivResult list
            maxTpsReport: dict = field(default_factory=dict)

        binSearchResults: PerfTestSearchResults=PerfTestSearchResults()
        longRunningSearchResults: PerfTestSearchResults=PerfTestSearchResults()
        tpsTestStart: datetime=datetime.utcnow()
        tpsTestFinish: datetime=datetime.utcnow()
        perfRunSuccessful: bool=False

    @dataclass
    class LoggingConfig:
        logDirBase: str = f"./{os.path.splitext(os.path.basename(__file__))[0]}"
        logDirTimestamp: str = f"{datetime.utcnow().strftime('%Y-%m-%d_%H-%M-%S')}"
        logDirPath: str = field(default_factory=str, init=False)
        ptbLogsDirPath: str = field(default_factory=str, init=False)
        pluginThreadOptLogsDirPath: str = field(default_factory=str, init=False)

        def __post_init__(self):
            self.logDirPath = f"{self.logDirBase}/{self.logDirTimestamp}"
            self.ptbLogsDirPath = f"{self.logDirPath}/testRunLogs"
            self.pluginThreadOptLogsDirPath = f"{self.logDirPath}/pluginThreadOptRunLogs"

    def __init__(self, testHelperConfig: PerformanceTestBasic.TestHelperConfig=PerformanceTestBasic.TestHelperConfig(),
                 clusterConfig: PerformanceTestBasic.ClusterConfig=PerformanceTestBasic.ClusterConfig(), ptConfig=PtConfig()):
        self.testHelperConfig = testHelperConfig
        self.clusterConfig = clusterConfig
        self.ptConfig = ptConfig

        self.testsStart = datetime.utcnow()

        self.loggingConfig = PerformanceTest.LoggingConfig(logDirBase=f"{self.ptConfig.logDirRoot}/{os.path.splitext(os.path.basename(__file__))[0]}",
                                                           logDirTimestamp=f"{self.testsStart.strftime('%Y-%m-%d_%H-%M-%S')}")

    def performPtbBinarySearch(self, clusterConfig: PerformanceTestBasic.ClusterConfig, logDirRoot: str, delReport: bool, quiet: bool, delPerfLogs: bool) -> TpsTestResult.PerfTestSearchResults:
        floor = 0
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
                                                       quiet=quiet, delPerfLogs=delPerfLogs)

            myTest = PerformanceTestBasic(testHelperConfig=self.testHelperConfig, clusterConfig=clusterConfig, ptbConfig=ptbConfig)
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

        # Default - Decrementing Max TPS in range [0, tpsInitial]
        absFloor = 0
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
                                                    numAddlBlocksToPrune=self.ptConfig.numAddlBlocksToPrune, logDirRoot=self.loggingConfig.ptbLogsDirPath, delReport=self.ptConfig.delReport, quiet=self.ptConfig.quiet, delPerfLogs=self.ptConfig.delPerfLogs)

            myTest = PerformanceTestBasic(testHelperConfig=self.testHelperConfig, clusterConfig=self.clusterConfig, ptbConfig=ptbConfig)
            testSuccessful = myTest.runTest()
            if self.evaluateSuccess(myTest, testSuccessful, ptbResult):
                maxTpsAchieved = searchTarget
                maxTpsReport = myTest.report
                scenarioResult.success = True
                maxFound = True
            else:
                searchTarget = searchTarget - step

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

        resultsFile = f"{self.loggingConfig.pluginThreadOptLogsDirPath}/{optPlugin.value}ThreadResults.txt"

        threadToMaxTpsDict: dict = {}

        clusterConfig = copy.deepcopy(self.clusterConfig)
        analysisStart = datetime.utcnow()

        with open(resultsFile, 'w') as log:
            log.write(f"{optPlugin.value}Threads, maxTpsAchieved\n")
        log.close()

        lastMaxTpsAchieved = 0
        for threadCount in range(minThreadCount, maxThreadCount+1):
            print(f"Running {optPlugin.value} thread count optimization check with {threadCount} {optPlugin.value} threads")

            getattr(clusterConfig.extraNodeosArgs, optPlugin.value + 'PluginArgs').threads = threadCount

            binSearchResults = self.performPtbBinarySearch(clusterConfig=clusterConfig, logDirRoot=self.loggingConfig.pluginThreadOptLogsDirPath,
                                                            delReport=True, quiet=False, delPerfLogs=True)

            threadToMaxTpsDict[threadCount] = binSearchResults.maxTpsAchieved
            if not self.ptConfig.quiet:
                print("Search Results:")
                for i in range(len(binSearchResults.searchResults)):
                    print(f"Search scenario {optPlugin.value} thread count {threadCount}: {i} result: {binSearchResults.searchResults[i]}")

            with open(resultsFile, 'a') as log:
                log.write(f"{threadCount},{binSearchResults.maxTpsAchieved}\n")
            log.close()

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

    def createReport(self,producerThreadResult: PluginThreadOptResult=None, chainThreadResult: PluginThreadOptResult=None, netThreadResult: PluginThreadOptResult=None, tpsTestResult: dict=None) -> dict:
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
        report['nodeosVersion'] = Utils.getNodeosVersion()
        return report

    def reportAsJSON(self, report: dict) -> json:
        if 'ProducerThreadAnalysis' in report:
            report['ProducerThreadAnalysis']['analysisStart'] = report['ProducerThreadAnalysis']['analysisStart'].isoformat()
            report['ProducerThreadAnalysis']['analysisFinish'] = report['ProducerThreadAnalysis']['analysisFinish'].isoformat()
        if 'ChainThreadAnalysis' in report:
            report['ChainThreadAnalysis']['analysisStart'] = report['ChainThreadAnalysis']['analysisStart'].isoformat()
            report['ChainThreadAnalysis']['analysisFinish'] = report['ChainThreadAnalysis']['analysisFinish'].isoformat()
        if 'NetThreadAnalysis' in report:
            report['NetThreadAnalysis']['analysisStart'] = report['NetThreadAnalysis']['analysisStart'].isoformat()
            report['NetThreadAnalysis']['analysisFinish'] = report['NetThreadAnalysis']['analysisFinish'].isoformat()

        if 'tpsTestStart' in report:
            report['tpsTestStart'] = report['tpsTestStart'].isoformat()
        if 'tpsTestFinish' in report:
            report['tpsTestFinish'] = report['tpsTestFinish'].isoformat()

        report['perfTestsBegin'] = report['perfTestsBegin'].isoformat()
        report['perfTestsFinish'] = report['perfTestsFinish'].isoformat()

        return json.dumps(report, indent=2)

    def exportReportAsJSON(self, report: json, exportPath):
        with open(exportPath, 'wt') as f:
            f.write(report)

    def testDirsCleanup(self):
        try:
            def removeArtifacts(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if os.path.isdir(f"{path}"):
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
                if not os.path.isdir(f"{path}"):
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
            prodResults = self.optimizePluginThreadCount(optPlugin=PerformanceTest.PluginThreadOpt.PRODUCER, optType=optType)
            print(f"Producer Thread Optimization results: {prodResults}")
            self.clusterConfig.extraNodeosArgs.producerPluginArgs.threads = prodResults.recommendedThreadCount

        chainResults = None
        if self.ptConfig.calcChainThreads != "none":
            print(f"Performing Chain Thread Optimization Tests")
            if self.ptConfig.calcChainThreads == "full":
                optType = PerformanceTest.PluginThreadOptRunType.FULL
            else:
                optType = PerformanceTest.PluginThreadOptRunType.LOCAL_MAX
            chainResults = self.optimizePluginThreadCount(optPlugin=PerformanceTest.PluginThreadOpt.CHAIN, optType=optType)
            print(f"Chain Thread Optimization results: {chainResults}")
            self.clusterConfig.extraNodeosArgs.chainPluginArgs.threads = chainResults.recommendedThreadCount

        netResults = None
        if self.ptConfig.calcNetThreads != "none":
            print(f"Performing Net Thread Optimization Tests")
            if self.ptConfig.calcNetThreads == "full":
                optType = PerformanceTest.PluginThreadOptRunType.FULL
            else:
                optType = PerformanceTest.PluginThreadOptRunType.LOCAL_MAX
            netResults = self.optimizePluginThreadCount(optPlugin=PerformanceTest.PluginThreadOpt.NET, optType=optType)
            print(f"Net Thread Optimization results: {netResults}")
            self.clusterConfig.extraNodeosArgs.netPluginArgs.threads = netResults.recommendedThreadCount

        tpsTestResult = None
        if not self.ptConfig.skipTpsTests:
            print(f"Performing TPS Performance Tests")
            testSuccessful = False
            tpsTestResult = self.performTpsTest()
            testSuccessful = tpsTestResult.perfRunSuccessful

        self.testsFinish = datetime.utcnow()

        self.report = self.createReport(producerThreadResult=prodResults, chainThreadResult=chainResults, netThreadResult=netResults, tpsTestResult=tpsTestResult)
        jsonReport = self.reportAsJSON(self.report)

        if not self.ptConfig.quiet:
            print(f"Full Performance Test Report: {jsonReport}")

        if not self.ptConfig.delReport:
            self.exportReportAsJSON(jsonReport, f"{self.loggingConfig.logDirPath}/report.json")

        if self.ptConfig.delPerfLogs:
            print(f"Cleaning up logs directory: {self.loggingConfig.logDirPath}")
            self.testDirsCleanup()

        return testSuccessful

def parseArgs():
    appArgs=AppArgs()
    appArgs.add(flag="--max-tps-to-test", type=int, help="The max target transfers realistic as ceiling of test range", default=50000)
    appArgs.add(flag="--test-iteration-duration-sec", type=int, help="The duration of transfer trx generation for each iteration of the test during the initial search (seconds)", default=150)
    appArgs.add(flag="--test-iteration-min-step", type=int, help="The step size determining granularity of tps result during initial search", default=500)
    appArgs.add(flag="--final-iterations-duration-sec", type=int, help="The duration of transfer trx generation for each final longer run iteration of the test during the final search (seconds)", default=300)
    appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
    appArgs.add(flag="--num-blocks-to-prune", type=int, help="The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end of the range of blocks of interest for evaluation.", default=2)
    appArgs.add(flag="--signature-cpu-billable-pct", type=int, help="Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50%%", default=0)
    appArgs.add(flag="--chain-state-db-size-mb", type=int, help="Maximum size (in MiB) of the chain state database", default=10*1024)
    appArgs.add(flag="--chain-threads", type=int, help="Number of worker threads in controller thread pool", default=2)
    appArgs.add(flag="--database-map-mode", type=str, help="Database map mode (\"mapped\", \"heap\", or \"locked\"). \
                                                            In \"mapped\" mode database is memory mapped as a file. \
                                                            In \"heap\" mode database is preloaded in to swappable memory and will use huge pages if available. \
                                                            In \"locked\" mode database is preloaded, locked in to memory, and will use huge pages if available.",
                                                            choices=["mapped", "heap", "locked"], default="mapped")
    appArgs.add(flag="--net-threads", type=int, help="Number of worker threads in net_plugin thread pool", default=2)
    appArgs.add(flag="--disable-subjective-billing", type=bool, help="Disable subjective CPU billing for API/P2P transactions", default=True)
    appArgs.add(flag="--last-block-time-offset-us", type=int, help="Offset of last block producing time in microseconds. Valid range 0 .. -block_time_interval.", default=0)
    appArgs.add(flag="--produce-time-offset-us", type=int, help="Offset of non last block producing time in microseconds. Valid range 0 .. -block_time_interval.", default=0)
    appArgs.add(flag="--cpu-effort-percent", type=int, help="Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%%", default=100)
    appArgs.add(flag="--last-block-cpu-effort-percent", type=int, help="Percentage of cpu block production time used to produce last block. Whole number percentages, e.g. 80 for 80%%", default=100)
    appArgs.add(flag="--producer-threads", type=int, help="Number of worker threads in producer thread pool", default=2)
    appArgs.add(flag="--http-max-response-time-ms", type=int, help="Maximum time for processing a request, -1 for unlimited", default=990000)
    appArgs.add_bool(flag="--del-perf-logs", help="Whether to delete performance test specific logs.")
    appArgs.add_bool(flag="--del-report", help="Whether to delete overarching performance run report.")
    appArgs.add_bool(flag="--del-test-report", help="Whether to save json reports from each test scenario.")
    appArgs.add_bool(flag="--quiet", help="Whether to quiet printing intermediate results and reports to stdout")
    appArgs.add_bool(flag="--prods-enable-trace-api", help="Determines whether producer nodes should have eosio::trace_api_plugin enabled")
    appArgs.add_bool(flag="--skip-tps-test", help="Determines whether to skip the max TPS measurement tests")
    appArgs.add(flag="--calc-producer-threads", type=str, help="Determines whether to calculate number of worker threads to use in producer thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                In \"none\" mode, the default, no calculation will be attempted and default configured --producer-threads value will be used. \
                                                                In \"lmax\" mode, producer threads will incrementally be tested until the performance rate ceases to increase with the addition of additional threads. \
                                                                In \"full\" mode producer threads will incrementally be tested from 2..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                Useful for graphing the full performance impact of each available thread.",
                                                                choices=["none", "lmax", "full"], default="none")
    appArgs.add(flag="--calc-chain-threads", type=str, help="Determines whether to calculate number of worker threads to use in chain thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                In \"none\" mode, the default, no calculation will be attempted and default configured --chain-threads value will be used. \
                                                                In \"lmax\" mode, producer threads will incrementally be tested until the performance rate ceases to increase with the addition of additional threads. \
                                                                In \"full\" mode producer threads will incrementally be tested from 2..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                Useful for graphing the full performance impact of each available thread.",
                                                                choices=["none", "lmax", "full"], default="none")
    appArgs.add(flag="--calc-net-threads", type=str, help="Determines whether to calculate number of worker threads to use in net thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                In \"none\" mode, the default, no calculation will be attempted and default configured --net-threads value will be used. \
                                                                In \"lmax\" mode, producer threads will incrementally be tested until the performance rate ceases to increase with the addition of additional threads. \
                                                                In \"full\" mode producer threads will incrementally be tested from 2..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                Useful for graphing the full performance impact of each available thread.",
                                                                choices=["none", "lmax", "full"], default="none")
    args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                                ,"--dump-error-details","-v","--leave-running"
                                ,"--clean-run"}, applicationSpecificArgs=appArgs)
    return args

def main():

    args = parseArgs()
    Utils.Debug = args.v

    testHelperConfig = PerformanceTestBasic.TestHelperConfig(killAll=args.clean_run, dontKill=args.leave_running, keepLogs=not args.del_perf_logs,
                                                             dumpErrorDetails=args.dump_error_details, delay=args.d, nodesFile=args.nodes_file,
                                                             verbose=args.v)

    ENA = PerformanceTestBasic.ClusterConfig.ExtraNodeosArgs
    chainPluginArgs = ENA.ChainPluginArgs(signatureCpuBillablePct=args.signature_cpu_billable_pct, chainStateDbSizeMb=args.chain_state_db_size_mb,
                                          threads=args.chain_threads, databaseMapMode=args.database_map_mode)
    producerPluginArgs = ENA.ProducerPluginArgs(disableSubjectiveBilling=args.disable_subjective_billing,
                                                lastBlockTimeOffsetUs=args.last_block_time_offset_us, produceTimeOffsetUs=args.produce_time_offset_us,
                                                cpuEffortPercent=args.cpu_effort_percent, lastBlockCpuEffortPercent=args.last_block_cpu_effort_percent,
                                                threads=args.producer_threads)
    httpPluginArgs = ENA.HttpPluginArgs(httpMaxResponseTimeMs=args.http_max_response_time_ms)
    netPluginArgs = ENA.NetPluginArgs(threads=args.net_threads)
    extraNodeosArgs = ENA(chainPluginArgs=chainPluginArgs, httpPluginArgs=httpPluginArgs, producerPluginArgs=producerPluginArgs, netPluginArgs=netPluginArgs)
    testClusterConfig = PerformanceTestBasic.ClusterConfig(pnodes=args.p, totalNodes=args.n, topo=args.s, genesisPath=args.genesis,
                                                           prodsEnableTraceApi=args.prods_enable_trace_api, extraNodeosArgs=extraNodeosArgs)

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
                                        logDirRoot=".",
                                        skipTpsTests=args.skip_tps_test,
                                        calcProducerThreads=args.calc_producer_threads,
                                        calcChainThreads=args.calc_chain_threads,
                                        calcNetThreads=args.calc_net_threads)

    myTest = PerformanceTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, ptConfig=ptConfig)
    perfRunSuccessful = myTest.runTest()

    exitCode = 0 if perfRunSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
