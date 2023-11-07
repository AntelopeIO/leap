#!/usr/bin/env python3

import argparse
import copy
import math
import os
import sys
import shutil

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

from TestHarness import TestHelper, Utils, Account
from .performance_test_basic import PerformanceTestBasic, PtbArgumentsHandler
from platform import release, system
from dataclasses import dataclass, asdict, field
from datetime import datetime
from enum import Enum
from .log_reader import JsonReportHandler

class PerformanceTest:

    @dataclass
    class PerfTestSearchIndivResult:
        success: bool = False
        searchTarget: int = 0
        searchFloor: int = 0
        searchCeiling: int = 0
        basicTestResult: PerformanceTestBasic.PerfTestBasicResult = field(default_factory=PerformanceTestBasic.PerfTestBasicResult)

    @dataclass
    class PtConfig:
        testDurationSec: int=150
        finalDurationSec: int=300
        delPerfLogs: bool=False
        maxTpsToTest: int=50000
        minTpsToTest: int=1
        testIterationMinStep: int=500
        tpsLimitPerGenerator: int=4000
        delReport: bool=False
        delTestReport: bool=False
        numAddlBlocksToPrune: int=2
        quiet: bool=False
        logDirRoot: Path=Path(".")
        skipTpsTests: bool=False
        calcChainThreads: str="none"
        calcNetThreads: str="none"
        userTrxDataFile: Path=None
        endpointMode: str="p2p"
        opModeCmd: str=""
        trxGenerator: Path=Path(".")
        saveState: bool=False


        def __post_init__(self):
            self.opModeDesc = "Block Producer Operational Mode" if self.opModeCmd == "testBpOpMode" else "API Node Operational Mode" if self.opModeCmd == "testApiOpMode" else "Undefined Operational Mode"
            if self.maxTpsToTest < 1:
                self.maxTpsToTest = 1
            if self.minTpsToTest < 1:
                self.minTpsToTest = 1
            if self.maxTpsToTest < self.minTpsToTest:
                self.minTpsToTest = self.maxTpsToTest

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
        logDirBase: Path = Path(".")/os.path.basename(sys.argv[0]).rsplit('.',maxsplit=1)[0]
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

        self.loggingConfig = PerformanceTest.LoggingConfig(logDirBase=Path(self.ptConfig.logDirRoot)/f"PHSRLogs",
                                                           logDirTimestamp=f"{self.testsStart.strftime('%Y-%m-%d_%H-%M-%S')}")

    def performPtbBinarySearch(self, clusterConfig: PerformanceTestBasic.ClusterConfig, logDirRoot: Path, delReport: bool, quiet: bool, delPerfLogs: bool, saveState: bool) -> TpsTestResult.PerfTestSearchResults:
        floor = self.ptConfig.minTpsToTest
        ceiling = self.ptConfig.maxTpsToTest
        binSearchTarget = self.ptConfig.maxTpsToTest
        minStep = self.ptConfig.testIterationMinStep

        maxTpsAchieved = 0
        maxTpsReport = {}
        searchResults = []

        while ceiling >= floor:
            print(f"Running scenario: floor {floor} binSearchTarget {binSearchTarget} ceiling {ceiling}")
            scenarioResult = PerformanceTest.PerfTestSearchIndivResult(success=False, searchTarget=binSearchTarget, searchFloor=floor, searchCeiling=ceiling)
            ptbConfig = PerformanceTestBasic.PtbConfig(targetTps=binSearchTarget, testTrxGenDurationSec=self.ptConfig.testDurationSec, tpsLimitPerGenerator=self.ptConfig.tpsLimitPerGenerator,
                                                       numAddlBlocksToPrune=self.ptConfig.numAddlBlocksToPrune, logDirRoot=logDirRoot, delReport=delReport,
                                                       quiet=quiet, delPerfLogs=delPerfLogs, userTrxDataFile=self.ptConfig.userTrxDataFile, endpointMode=self.ptConfig.endpointMode,
                                                       trxGenerator=self.ptConfig.trxGenerator, saveState=saveState)

            myTest = PerformanceTestBasic(testHelperConfig=self.testHelperConfig, clusterConfig=clusterConfig, ptbConfig=ptbConfig, testNamePath="PHSRun")
            myTest.runTest()
            if myTest.testResult.testPassed:
                maxTpsAchieved = binSearchTarget
                maxTpsReport = myTest.report
                floor = binSearchTarget + minStep
                scenarioResult.success = True
            else:
                ceiling = binSearchTarget - minStep

            scenarioResult.basicTestResult = myTest.testResult
            searchResults.append(scenarioResult)
            if not self.ptConfig.quiet:
                print(f"binary search result -- target: {binSearchTarget} | result: {searchResults[-1]}")

            binSearchTarget = floor + (math.ceil(((ceiling - floor) / minStep) / 2) * minStep)

        return PerformanceTest.TpsTestResult.PerfTestSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

    def performPtbReverseLinearSearch(self, tpsInitial: int) -> TpsTestResult.PerfTestSearchResults:

        # Default - Decrementing Max TPS in range [minTpsToTest (def=1), tpsInitial]
        absFloor = self.ptConfig.minTpsToTest
        tpsInitial = absFloor if tpsInitial <= 0 or tpsInitial < absFloor else tpsInitial
        absCeiling = tpsInitial

        step = self.ptConfig.testIterationMinStep

        searchTarget = tpsInitial

        maxTpsAchieved = 0
        maxTpsReport = {}
        searchResults = []
        maxFound = False

        while not maxFound:
            print(f"Running scenario: floor {absFloor} searchTarget {searchTarget} ceiling {absCeiling}")
            scenarioResult = PerformanceTest.PerfTestSearchIndivResult(success=False, searchTarget=searchTarget, searchFloor=absFloor, searchCeiling=absCeiling)
            ptbConfig = PerformanceTestBasic.PtbConfig(targetTps=searchTarget, testTrxGenDurationSec=self.ptConfig.testDurationSec, tpsLimitPerGenerator=self.ptConfig.tpsLimitPerGenerator,
                                                    numAddlBlocksToPrune=self.ptConfig.numAddlBlocksToPrune, logDirRoot=self.loggingConfig.ptbLogsDirPath, delReport=self.ptConfig.delReport,
                                                    quiet=self.ptConfig.quiet, delPerfLogs=self.ptConfig.delPerfLogs, userTrxDataFile=self.ptConfig.userTrxDataFile, endpointMode=self.ptConfig.endpointMode,
                                                    trxGenerator=self.ptConfig.trxGenerator, saveState=self.ptConfig.saveState)

            myTest = PerformanceTestBasic(testHelperConfig=self.testHelperConfig, clusterConfig=self.clusterConfig, ptbConfig=ptbConfig, testNamePath="PHSRun")
            myTest.runTest()
            if myTest.testResult.testPassed:
                maxTpsAchieved = searchTarget
                maxTpsReport = myTest.report
                scenarioResult.success = True
                maxFound = True
            else:
                if searchTarget <= absFloor:
                    # This means it has already run a search at absFloor, and failed, so exit.
                    maxFound = True
                searchTarget = max(searchTarget - step, absFloor)

            scenarioResult.basicTestResult = myTest.testResult
            searchResults.append(scenarioResult)
            if not self.ptConfig.quiet:
                print(f"reverse linear search result -- target: {searchTarget} | result: {searchResults[-1]}")

        return PerformanceTest.TpsTestResult.PerfTestSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

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
                                                            delReport=True, quiet=False, delPerfLogs=True, saveState=False)

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
        report['tpsTestDuration'] = tpsTestResult.tpsTestFinish - tpsTestResult.tpsTestStart
        report['InitialSearchScenariosSummary'] =  {tpsTestResult.binSearchResults.searchResults[x].searchTarget : "PASS" if tpsTestResult.binSearchResults.searchResults[x].success else "FAIL" for x in range(len(tpsTestResult.binSearchResults.searchResults))}
        report['LongRunningSearchScenariosSummary'] =  {tpsTestResult.longRunningSearchResults.searchResults[x].searchTarget : "PASS" if tpsTestResult.longRunningSearchResults.searchResults[x].success else "FAIL" for x in range(len(tpsTestResult.longRunningSearchResults.searchResults))}
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
        report['perfTestsDuration'] = self.testsFinish - self.testsStart
        report['operationalMode'] = self.ptConfig.opModeDesc
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
                                                       delReport=self.ptConfig.delReport, quiet=self.ptConfig.quiet, delPerfLogs=self.ptConfig.delPerfLogs,
                                                       saveState=self.ptConfig.saveState)

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

        self.report = self.createReport(chainThreadResult=chainResults, netThreadResult=netResults, tpsTestResult=tpsTestResult, nodeosVers=self.clusterConfig.nodeosVers)
        jsonReport = JsonReportHandler.reportAsJSON(self.report)

        if not self.ptConfig.quiet:
            print(f"Full Performance Test Report: {jsonReport}")

        if not self.ptConfig.delReport:
            JsonReportHandler.exportReportAsJSON(jsonReport, self.loggingConfig.logDirPath/Path("report.json"))

        if self.ptConfig.delPerfLogs:
            print(f"Cleaning up logs directory: {self.loggingConfig.logDirPath}")
            self.testDirsCleanup()

        return testSuccessful

class PerfTestArgumentsHandler(object):


    @staticmethod
    def createArgumentParser():
        def createPtParser(suppressHelp:bool=False):
            ptParser = argparse.ArgumentParser(add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
            ptGrpTitle="Performance Harness"
            ptGrpDescription="Performance Harness testing configuration items."
            ptParserGroup = ptParser.add_argument_group(title=None if suppressHelp else ptGrpTitle, description=None if suppressHelp else ptGrpDescription)
            ptParserGroup.add_argument("--skip-tps-test", help=argparse.SUPPRESS if suppressHelp else "Determines whether to skip the max TPS measurement tests", action='store_true')
            ptParserGroup.add_argument("--calc-chain-threads", type=str, help=argparse.SUPPRESS if suppressHelp else "Determines whether to calculate number of worker threads to use in chain thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                        In \"none\" mode, the default, no calculation will be attempted and the configured --chain-threads value will be used. \
                                                                        In \"lmax\" mode, producer threads will incrementally be tested, starting at plugin default, until the performance rate ceases to increase with the addition of additional threads. \
                                                                        In \"full\" mode producer threads will incrementally be tested from plugin default..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                        Useful for graphing the full performance impact of each available thread.",
                                                                        choices=["none", "lmax", "full"], default="none",)
            ptParserGroup.add_argument("--calc-net-threads", type=str, help=argparse.SUPPRESS if suppressHelp else "Determines whether to calculate number of worker threads to use in net thread pool (\"none\", \"lmax\", or \"full\"). \
                                                                        In \"none\" mode, the default, no calculation will be attempted and the configured --net-threads value will be used. \
                                                                        In \"lmax\" mode, producer threads will incrementally be tested, starting at plugin default, until the performance rate ceases to increase with the addition of additional threads. \
                                                                        In \"full\" mode producer threads will incrementally be tested from plugin default..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in \"lmax\" mode). \
                                                                        Useful for graphing the full performance impact of each available thread.",
                                                                        choices=["none", "lmax", "full"], default="none")
            ptParserGroup.add_argument("--del-test-report", help=argparse.SUPPRESS if suppressHelp else "Whether to save json reports from each test scenario.", action='store_true')

            ptTpsGrpTitle="Performance Harness - TPS Test Config"
            ptTpsGrpDescription="TPS Performance Test configuration items."
            ptTpsParserGroup = ptParser.add_argument_group(title=None if suppressHelp else ptTpsGrpTitle, description=None if suppressHelp else ptTpsGrpDescription)

            ptTpsParserGroup.add_argument("--max-tps-to-test", type=int, help=argparse.SUPPRESS if suppressHelp else "The max target transfers realistic as ceiling of test range", default=50000)
            ptTpsParserGroup.add_argument("--min-tps-to-test", type=int, help=argparse.SUPPRESS if suppressHelp else "The min target transfers to use as floor of test range", default=1)
            ptTpsParserGroup.add_argument("--test-iteration-duration-sec", type=int, help=argparse.SUPPRESS if suppressHelp else "The duration of transfer trx generation for each iteration of the test during the initial search (seconds)", default=150)
            ptTpsParserGroup.add_argument("--test-iteration-min-step", type=int, help=argparse.SUPPRESS if suppressHelp else "The step size determining granularity of tps result during initial search", default=500)
            ptTpsParserGroup.add_argument("--final-iterations-duration-sec", type=int, help=argparse.SUPPRESS if suppressHelp else "The duration of transfer trx generation for each final longer run iteration of the test during the final search (seconds)", default=300)
            return ptParser

        # Create 2 versions of the PT Parser, one with help suppressed to go on the top level parser where the help message is pared down
        # and the second with help message not suppressed to be the parent of the operational mode sub-commands where pt configuration help
        # should be displayed
        ptParserNoHelp = createPtParser(suppressHelp=True)
        ptParser = createPtParser()

        # Create 2 versions of the PTB Parser, one with help suppressed to go on the top level operational mode sub-commands parsers where the help message is pared down
        # and the second with help message not suppressed to be the parent of the overrideBasicTestConfig sub-command where configuration help should be displayed
        ptbBpModeArgParserNoHelp = PtbArgumentsHandler.createBaseBpP2pArgumentParser(suppressHelp=True)
        ptbBpModeArgParser = PtbArgumentsHandler.createBaseBpP2pArgumentParser()

        phParser = argparse.ArgumentParser(add_help=True, parents=[ptParserNoHelp], formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        #Let top level performance harness parser know there will be sub-commands, and that an operational mode sub-command is required
        opModeDesc=("Each Operational Mode sets up a known node operator configuration and performs load testing and analysis catered to the expectations of that specific operational mode.\
                    For additional configuration options for each operational mode use, pass --help to the sub-command.\
                    Eg:  performance_test.py testBpOpMode --help")
        ptParserSubparsers = phParser.add_subparsers(title="Operational Modes",
                                                     description=opModeDesc,
                                                     dest="op_mode_sub_cmd",
                                                     required=True, help="Currently supported operational mode sub-commands.")

        #Create the Block Producer Operational Mode Sub-Command and Parsers
        bpModeParser = ptParserSubparsers.add_parser(name="testBpOpMode", parents=[ptParser, ptbBpModeArgParserNoHelp], add_help=False, help="Test the Block Producer Operational Mode.")
        bpModeAdvDesc=("Block Producer Operational Mode Advanced Configuration Options allow low level adjustments to the basic test configuration as well as the node topology being tested.\
                        For additional information on available advanced configuration options, pass --help to the sub-command.\
                        Eg:  performance_test.py testBpOpMode overrideBasicTestConfig --help")
        bpModeParserSubparsers = bpModeParser.add_subparsers(title="Advanced Configuration Options",
                                                             description=bpModeAdvDesc,
                                                             help="sub-command to allow overriding advanced configuration options")
        bpModeParserSubparsers.add_parser(name="overrideBasicTestConfig", parents=[ptbBpModeArgParser], add_help=False,
                                          help="Use this sub-command to override low level controls for basic test, logging, node topology, etc.")

        # Create 2 versions of the PTB Parser, one with help suppressed to go on the top level operational mode sub-commands parsers where the help message is pared down
        # and the second with help message not suppressed to be the parent of the overrideBasicTestConfig sub-command where configuration help should be displayed
        ptbApiModeArgParserNoHelp = PtbArgumentsHandler.createBaseApiHttpArgumentParser(suppressHelp=True)
        ptbApiModeArgParser = PtbArgumentsHandler.createBaseApiHttpArgumentParser()

        #Create the API Node Operational Mode Sub-Command and Parsers
        apiModeParser = ptParserSubparsers.add_parser(name="testApiOpMode", parents=[ptParser, ptbApiModeArgParserNoHelp], add_help=False, help="Test the API Node Operational Mode.")
        apiModeAdvDesc=("API Node Operational Mode Advanced Configuration Options allow low level adjustments to the basic test configuration as well as the node topology being tested.\
                        For additional information on available advanced configuration options, pass --help to the sub-command.\
                        Eg:  performance_test.py testApiOpMode overrideBasicTestConfig --help")
        apiModeParserSubparsers = apiModeParser.add_subparsers(title="Advanced Configuration Options",
                                                               description=apiModeAdvDesc,
                                                               help="sub-command to allow overriding advanced configuration options")
        apiModeParserSubparsers.add_parser(name="overrideBasicTestConfig", parents=[ptbApiModeArgParser], add_help=False,
                                           help="Use this sub-command to override low level controls for basic test, logging, node topology, etc.")

        return phParser

    @staticmethod
    def parseArgs():
        ptParser=PerfTestArgumentsHandler.createArgumentParser()
        args=ptParser.parse_args()
        return args
