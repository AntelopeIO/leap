#!/usr/bin/env python3

import math
import os
import sys
import json
import shutil

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import TestHelper, Utils
from TestHarness.TestHelper import AppArgs
from performance_test_basic import PerformanceBasicTest
from platform import release, system
from dataclasses import dataclass, asdict, field
from datetime import datetime

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

@dataclass
class PerfTestSearchIndivResult:
    success: bool = False
    searchTarget: int = 0
    searchFloor: int = 0
    searchCeiling: int = 0
    basicTestResult: PerfTestBasicResult = PerfTestBasicResult()

@dataclass
class PerfTestSearchResults:
    maxTpsAchieved: int = 0
    searchResults: list = field(default_factory=list) #PerfTestSearchIndivResult list
    maxTpsReport: dict = field(default_factory=dict)

def performPtbBinarySearch(tpsTestFloor: int, tpsTestCeiling: int, minStep: int, testHelperConfig: PerformanceBasicTest.TestHelperConfig,
                           testClusterConfig: PerformanceBasicTest.ClusterConfig, testDurationSec: int, tpsLimitPerGenerator: int,
                           numAddlBlocksToPrune: int, testLogDir: str, saveJson: bool, quiet: bool) -> PerfTestSearchResults:
    floor = tpsTestFloor
    ceiling = tpsTestCeiling
    binSearchTarget = tpsTestCeiling

    maxTpsAchieved = 0
    maxTpsReport = {}
    searchResults = []

    while ceiling >= floor:
        print(f"Running scenario: floor {floor} binSearchTarget {binSearchTarget} ceiling {ceiling}")
        ptbResult = PerfTestBasicResult()
        scenarioResult = PerfTestSearchIndivResult(success=False, searchTarget=binSearchTarget, searchFloor=floor, searchCeiling=ceiling, basicTestResult=ptbResult)

        myTest = PerformanceBasicTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, targetTps=binSearchTarget,
                                    testTrxGenDurationSec=testDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                                    numAddlBlocksToPrune=numAddlBlocksToPrune, rootLogDir=testLogDir, saveJsonReport=saveJson, quiet=quiet)
        testSuccessful = myTest.runTest()
        if evaluateSuccess(myTest, testSuccessful, ptbResult):
            maxTpsAchieved = binSearchTarget
            maxTpsReport = myTest.report
            floor = binSearchTarget + minStep
            scenarioResult.success = True
        else:
            ceiling = binSearchTarget - minStep

        scenarioResult.basicTestResult = ptbResult
        searchResults.append(scenarioResult)
        if not quiet:
            print(f"searchResult: {binSearchTarget} : {searchResults[-1]}")

        binSearchTarget = floor + (math.ceil(((ceiling - floor) / minStep) / 2) * minStep)

    return PerfTestSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

def performPtbReverseLinearSearch(tpsInitial: int, step: int, testHelperConfig: PerformanceBasicTest.TestHelperConfig,
                                  testClusterConfig: PerformanceBasicTest.ClusterConfig, testDurationSec: int, tpsLimitPerGenerator: int,
                                  numAddlBlocksToPrune: int, testLogDir: str, saveJson: bool, quiet: bool) -> PerfTestSearchResults:

    # Default - Decrementing Max TPS in range [0, tpsInitial]
    absFloor = 0
    absCeiling = tpsInitial

    searchTarget = tpsInitial

    maxTpsAchieved = 0
    maxTpsReport = {}
    searchResults = []
    maxFound = False

    while not maxFound:
        print(f"Running scenario: floor {absFloor} searchTarget {searchTarget} ceiling {absCeiling}")
        ptbResult = PerfTestBasicResult()
        scenarioResult = PerfTestSearchIndivResult(success=False, searchTarget=searchTarget, searchFloor=absFloor, searchCeiling=absCeiling, basicTestResult=ptbResult)

        myTest = PerformanceBasicTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, targetTps=searchTarget,
                                    testTrxGenDurationSec=testDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                                    numAddlBlocksToPrune=numAddlBlocksToPrune, rootLogDir=testLogDir, saveJsonReport=saveJson, quiet=quiet)
        testSuccessful = myTest.runTest()
        if evaluateSuccess(myTest, testSuccessful, ptbResult):
            maxTpsAchieved = searchTarget
            maxTpsReport = myTest.report
            scenarioResult.success = True
            maxFound = True
        else:
            searchTarget = searchTarget - step

        scenarioResult.basicTestResult = ptbResult
        searchResults.append(scenarioResult)
        if not quiet:
            print(f"searchResult: {searchTarget} : {searchResults[-1]}")

    return PerfTestSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

def evaluateSuccess(test: PerformanceBasicTest, testSuccessful: bool, result: PerfTestBasicResult) -> bool:
    result.targetTPS = test.targetTps
    result.expectedTxns = test.expectedTransactionsSent
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
    result.logsDir = test.testTimeStampDirPath

    print(f"basicTestSuccess: {result.basicTestSuccess} tpsExpectationMet: {result.tpsExpectMet} trxExpectationMet: {result.trxExpectMet}")

    return result.basicTestSuccess and result.tpsExpectMet and result.trxExpectMet

def createReport(maxTpsAchieved, searchResults, maxTpsReport, longRunningMaxTpsAchieved, longRunningSearchResults, longRunningMaxTpsReport, testStart: datetime, testFinish: datetime, argsDict) -> dict:
    report = {}
    report['InitialMaxTpsAchieved'] = maxTpsAchieved
    report['LongRunningMaxTpsAchieved'] = longRunningMaxTpsAchieved
    report['testStart'] = testStart
    report['testFinish'] = testFinish
    report['InitialSearchResults'] =  {x: asdict(searchResults[x]) for x in range(len(searchResults))}
    report['InitialMaxTpsReport'] =  maxTpsReport
    report['LongRunningSearchResults'] =  {x: asdict(longRunningSearchResults[x]) for x in range(len(longRunningSearchResults))}
    report['LongRunningMaxTpsReport'] =  longRunningMaxTpsReport
    report['args'] =  argsDict
    report['env'] = {'system': system(), 'os': os.name, 'release': release()}
    report['nodeosVersion'] = Utils.getNodeosVersion()
    return report

def createJSONReport(maxTpsAchieved, searchResults, maxTpsReport, longRunningMaxTpsAchieved, longRunningSearchResults, longRunningMaxTpsReport, testStart: datetime, testFinish: datetime, argsDict) -> json:
    report = createReport(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport, longRunningMaxTpsAchieved=longRunningMaxTpsAchieved,
                              longRunningSearchResults=longRunningSearchResults, longRunningMaxTpsReport=longRunningMaxTpsReport, testStart=testStart, testFinish=testFinish, argsDict=argsDict)
    return reportAsJSON(report)

def reportAsJSON(report: dict) -> json:
    report['testStart'] = "Unknown" if report['testStart'] is None else report['testStart'].isoformat()
    report['testFinish'] = "Unknown" if report['testFinish'] is None else report['testFinish'].isoformat()
    return json.dumps(report, indent=2)

def exportReportAsJSON(report: json, exportPath):
    with open(exportPath, 'wt') as f:
        f.write(report)

def testDirsCleanup(saveJsonReport, testTimeStampDirPath, ptbLogsDirPath):
    try:
        def removeArtifacts(path):
            print(f"Checking if test artifacts dir exists: {path}")
            if os.path.isdir(f"{path}"):
                print(f"Cleaning up test artifacts dir and all contents of: {path}")
                shutil.rmtree(f"{path}")

        if saveJsonReport:
            removeArtifacts(ptbLogsDirPath)
        else:
            removeArtifacts(testTimeStampDirPath)
    except OSError as error:
        print(error)

def testDirsSetup(rootLogDir, testTimeStampDirPath, ptbLogsDirPath):
    try:
        def createArtifactsDir(path):
            print(f"Checking if test artifacts dir exists: {path}")
            if not os.path.isdir(f"{path}"):
                print(f"Creating test artifacts dir: {path}")
                os.mkdir(f"{path}")

        createArtifactsDir(rootLogDir)
        createArtifactsDir(testTimeStampDirPath)
        createArtifactsDir(ptbLogsDirPath)

    except OSError as error:
        print(error)

def prepArgsDict(testDurationSec, finalDurationSec, logsDir, maxTpsToTest, testIterationMinStep,
             tpsLimitPerGenerator, saveJsonReport, saveTestJsonReports, numAddlBlocksToPrune, quiet, testHelperConfig, testClusterConfig) -> dict:
    argsDict = {}
    argsDict.update(asdict(testHelperConfig))
    argsDict.update(asdict(testClusterConfig))
    argsDict.update({key:val for key, val in locals().items() if key in set(['testDurationSec', 'finalDurationSec', 'maxTpsToTest', 'testIterationMinStep', 'tpsLimitPerGenerator',
                                                                                  'saveJsonReport', 'saveTestJsonReports', 'numAddlBlocksToPrune', 'logsDir', 'quiet'])})
    return argsDict

def parseArgs():
    appArgs=AppArgs()
    appArgs.add(flag="--max-tps-to-test", type=int, help="The max target transfers realistic as ceiling of test range", default=50000)
    appArgs.add(flag="--test-iteration-duration-sec", type=int, help="The duration of transfer trx generation for each iteration of the test during the initial search (seconds)", default=150)
    appArgs.add(flag="--test-iteration-min-step", type=int, help="The step size determining granularity of tps result during initial search", default=500)
    appArgs.add(flag="--final-iterations-duration-sec", type=int, help="The duration of transfer trx generation for each final longer run iteration of the test during the final search (seconds)", default=300)
    appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
    appArgs.add(flag="--num-blocks-to-prune", type=int, help="The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end of the range of blocks of interest for evaluation.", default=2)
    appArgs.add_bool(flag="--save-json", help="Whether to save overarching performance run report.")
    appArgs.add_bool(flag="--save-test-json", help="Whether to save json reports from each test scenario.")
    appArgs.add_bool(flag="--quiet", help="Whether to quiet printing intermediate results and reports to stdout")
    appArgs.add_bool(flag="--prods-enable-trace-api", help="Determines whether producer nodes should have eosio::trace_api_plugin enabled")
    args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                                ,"--dump-error-details","-v","--leave-running"
                                ,"--clean-run","--keep-logs"}, applicationSpecificArgs=appArgs)
    return args

def main():

    args = parseArgs()
    Utils.Debug = args.v
    testDurationSec=args.test_iteration_duration_sec
    finalDurationSec=args.final_iterations_duration_sec
    killAll=args.clean_run
    dontKill=args.leave_running
    keepLogs=args.keep_logs
    dumpErrorDetails=args.dump_error_details
    delay=args.d
    nodesFile=args.nodes_file
    verbose=args.v
    pnodes=args.p
    totalNodes=args.n
    topo=args.s
    genesisPath=args.genesis
    maxTpsToTest=args.max_tps_to_test
    testIterationMinStep=args.test_iteration_min_step
    tpsLimitPerGenerator=args.tps_limit_per_generator
    saveJsonReport=args.save_json
    saveTestJsonReports=args.save_test_json
    numAddlBlocksToPrune=args.num_blocks_to_prune
    quiet=args.quiet
    prodsEnableTraceApi=args.prods_enable_trace_api

    testStart = datetime.utcnow()

    rootLogDir: str=os.path.splitext(os.path.basename(__file__))[0]
    testTimeStampDirPath = f"{rootLogDir}/{testStart.strftime('%Y-%m-%d_%H-%M-%S')}"
    ptbLogsDirPath = f"{testTimeStampDirPath}/testRunLogs"

    testDirsSetup(rootLogDir=rootLogDir, testTimeStampDirPath=testTimeStampDirPath, ptbLogsDirPath=ptbLogsDirPath)

    testHelperConfig = PerformanceBasicTest.TestHelperConfig(killAll=killAll, dontKill=dontKill, keepLogs=keepLogs,
                                                             dumpErrorDetails=dumpErrorDetails, delay=delay, nodesFile=nodesFile,
                                                             verbose=verbose)

    testClusterConfig = PerformanceBasicTest.ClusterConfig(pnodes=pnodes, totalNodes=totalNodes, topo=topo, genesisPath=genesisPath, prodsEnableTraceApi=prodsEnableTraceApi)

    argsDict = prepArgsDict(testDurationSec=testDurationSec, finalDurationSec=finalDurationSec, logsDir=testTimeStampDirPath,
                        maxTpsToTest=maxTpsToTest, testIterationMinStep=testIterationMinStep, tpsLimitPerGenerator=tpsLimitPerGenerator,
                        saveJsonReport=saveJsonReport, saveTestJsonReports=saveTestJsonReports, numAddlBlocksToPrune=numAddlBlocksToPrune,
                        quiet=quiet, testHelperConfig=testHelperConfig, testClusterConfig=testClusterConfig)

    perfRunSuccessful = False

    try:
        binSearchResults = performPtbBinarySearch(tpsTestFloor=0, tpsTestCeiling=maxTpsToTest, minStep=testIterationMinStep, testHelperConfig=testHelperConfig,
                           testClusterConfig=testClusterConfig, testDurationSec=testDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                           numAddlBlocksToPrune=numAddlBlocksToPrune, testLogDir=ptbLogsDirPath, saveJson=saveTestJsonReports, quiet=quiet)

        print(f"Successful rate of: {binSearchResults.maxTpsAchieved}")

        if not quiet:
            print("Search Results:")
            for i in range(len(binSearchResults.searchResults)):
                print(f"Search scenario: {i} result: {binSearchResults.searchResults[i]}")

        longRunningSearchResults = performPtbReverseLinearSearch(tpsInitial=binSearchResults.maxTpsAchieved, step=testIterationMinStep, testHelperConfig=testHelperConfig,
                                                                 testClusterConfig=testClusterConfig, testDurationSec=finalDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                                                                 numAddlBlocksToPrune=numAddlBlocksToPrune, testLogDir=ptbLogsDirPath, saveJson=saveTestJsonReports, quiet=quiet)

        print(f"Long Running Test - Successful rate of: {longRunningSearchResults.maxTpsAchieved}")
        perfRunSuccessful = True

        if not quiet:
            print("Long Running Test - Search Results:")
            for i in range(len(longRunningSearchResults.searchResults)):
                print(f"Search scenario: {i} result: {longRunningSearchResults.searchResults[i]}")

        testFinish = datetime.utcnow()
        fullReport = createJSONReport(maxTpsAchieved=binSearchResults.maxTpsAchieved, searchResults=binSearchResults.searchResults, maxTpsReport=binSearchResults.maxTpsReport,
                                      longRunningMaxTpsAchieved=longRunningSearchResults.maxTpsAchieved, longRunningSearchResults=longRunningSearchResults.searchResults,
                                      longRunningMaxTpsReport=longRunningSearchResults.maxTpsReport, testStart=testStart, testFinish=testFinish, argsDict=argsDict)

        if not quiet:
            print(f"Full Performance Test Report: {fullReport}")

        if saveJsonReport:
            exportReportAsJSON(fullReport, f"{testTimeStampDirPath}/report.json")

    finally:

        if not keepLogs:
            print(f"Cleaning up logs directory: {testTimeStampDirPath}")
            testDirsCleanup(saveJsonReport=saveJsonReport, testTimeStampDirPath=testTimeStampDirPath, ptbLogsDirPath=ptbLogsDirPath)

    exitCode = 0 if perfRunSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
