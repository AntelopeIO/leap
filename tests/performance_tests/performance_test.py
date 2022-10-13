#!/usr/bin/env python3

from dataclasses import dataclass, asdict, field
from math import floor
import os
import sys
import json
from datetime import datetime
import shutil

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import TestHelper, Utils
from TestHarness.TestHelper import AppArgs
from performance_test_basic import PerformanceBasicTest

@dataclass
class PerfTestBasicResult:
    targetTPS: int = 0
    resultAvgTps: float = 0
    expectedTxns: int = 0
    resultTxns: int = 0
    tpsExpectMet: bool = False
    trxExpectMet: bool = False
    basicTestSuccess: bool = False
    logsDir: str = ""

@dataclass
class PerfTestSearchIndivResult:
    success: bool = False
    searchTarget: int = 0
    searchFloor: int = 0
    searchCeiling: int = 0
    basicTestResult: PerfTestBasicResult = PerfTestBasicResult()

@dataclass
class PerfTestBinSearchResults:
    maxTpsAchieved: int = 0
    searchResults: list = field(default_factory=list) #PerfTestSearchIndivResult list
    maxTpsReport: dict = field(default_factory=dict)

def performPtbBinarySearch(tpsTestFloor: int, tpsTestCeiling: int, minStep: int, testHelperConfig: PerformanceBasicTest.TestHelperConfig,
                           testClusterConfig: PerformanceBasicTest.ClusterConfig, testDurationSec: int, tpsLimitPerGenerator: int,
                           numAddlBlocksToPrune: int, testLogDir: str, saveJson: bool) -> PerfTestBinSearchResults:
    floor = tpsTestFloor
    ceiling = tpsTestCeiling
    binSearchTarget = 0
    lastRun = False

    maxTpsAchieved = 0
    maxTpsReport = {}
    searchResults = []

    while floor < ceiling:
        binSearchTarget = round(((ceiling + floor) // 2) / 100) * 100
        print(f"Running scenario: floor {floor} binSearchTarget {binSearchTarget} ceiling {ceiling}")
        ptbResult = PerfTestBasicResult()
        scenarioResult = PerfTestSearchIndivResult(success=False, searchTarget=binSearchTarget, searchFloor=floor, searchCeiling=ceiling, basicTestResult=ptbResult)

        myTest = PerformanceBasicTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, targetTps=binSearchTarget,
                                    testTrxGenDurationSec=testDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                                    numAddlBlocksToPrune=numAddlBlocksToPrune, rootLogDir=testLogDir, saveJsonReport=saveJson)
        testSuccessful = myTest.runTest()

        if evaluateSuccess(myTest, testSuccessful, ptbResult):
            maxTpsAchieved = binSearchTarget
            maxTpsReport = json.loads(myTest.report)
            floor = binSearchTarget
            scenarioResult.success = True
        else:
            ceiling = binSearchTarget

        scenarioResult.basicTestResult = ptbResult
        searchResults.append(scenarioResult)
        print(f"searchResult: {binSearchTarget} : {searchResults[-1]}")
        if lastRun:
            break
        if ceiling - floor <= minStep:
            lastRun = True

    return PerfTestBinSearchResults(maxTpsAchieved=maxTpsAchieved, searchResults=searchResults, maxTpsReport=maxTpsReport)

def evaluateSuccess(test: PerformanceBasicTest, testSuccessful: bool, result: PerfTestBasicResult) -> bool:
    result.targetTPS = test.targetTps
    result.expectedTxns = test.expectedTransactionsSent
    reportDict = json.loads(test.report)
    result.resultAvgTps = reportDict["Analysis"]["TPS"]["avg"]
    result.resultTxns = reportDict["Analysis"]["TrxLatency"]["samples"]
    print(f"targetTPS: {result.targetTPS} expectedTxns: {result.expectedTxns} resultAvgTps: {result.resultAvgTps} resultTxns: {result.resultTxns}")

    result.tpsExpectMet = True if result.resultAvgTps >= result.targetTPS else abs(result.targetTPS - result.resultAvgTps) < 100
    result.trxExpectMet = result.expectedTxns == result.resultTxns
    result.basicTestSuccess = testSuccessful
    result.logsDir = test.testTimeStampDirPath

    print(f"basicTestSuccess: {result.basicTestSuccess} tpsExpectationMet: {result.tpsExpectMet} trxExpectationMet: {result.trxExpectMet}")

    return result.basicTestSuccess and result.tpsExpectMet and result.trxExpectMet

def createJSONReport(maxTpsAchieved, searchResults, maxTpsReport, longRunningMaxTpsAchieved, longRunningSearchResults, longRunningMaxTpsReport, argsDict) -> json:
    js = {}
    js['InitialMaxTpsAchieved'] = maxTpsAchieved
    js['LongRunningMaxTpsAchieved'] = longRunningMaxTpsAchieved
    js['InitialSearchResults'] =  {x: asdict(searchResults[x]) for x in range(len(searchResults))}
    js['InitialMaxTpsReport'] =  maxTpsReport
    js['LongRunningSearchResults'] =  {x: asdict(longRunningSearchResults[x]) for x in range(len(longRunningSearchResults))}
    js['LongRunningMaxTpsReport'] =  longRunningMaxTpsReport
    js['args'] =  argsDict
    return json.dumps(js, indent=2)

def exportReportAsJSON(report: json, exportPath):
    with open(exportPath, 'wt') as f:
        f.write(report)

def testDirsCleanup(saveJsonReport, testTimeStampDirPath, ptbLogsDirPath):
    try:
        if saveJsonReport:
            print(f"Checking if test artifacts dir exists: {ptbLogsDirPath}")
            if os.path.isdir(f"{ptbLogsDirPath}"):
                print(f"Cleaning up test artifacts dir and all contents of: {ptbLogsDirPath}")
                shutil.rmtree(f"{ptbLogsDirPath}")
        else:
            print(f"Checking if test artifacts dir exists: {testTimeStampDirPath}")
            if os.path.isdir(f"{testTimeStampDirPath}"):
                print(f"Cleaning up test artifacts dir and all contents of: {testTimeStampDirPath}")
                shutil.rmtree(f"{testTimeStampDirPath}")
    except OSError as error:
        print(error)

def testDirsSetup(rootLogDir, testTimeStampDirPath, ptbLogsDirPath):
    try:
        print(f"Checking if test artifacts dir exists: {rootLogDir}")
        if not os.path.isdir(f"{rootLogDir}"):
            print(f"Creating test artifacts dir: {rootLogDir}")
            os.mkdir(f"{rootLogDir}")

        print(f"Checking if logs dir exists: {testTimeStampDirPath}")
        if not os.path.isdir(f"{testTimeStampDirPath}"):
            print(f"Creating logs dir: {testTimeStampDirPath}")
            os.mkdir(f"{testTimeStampDirPath}")

        print(f"Checking if logs dir exists: {ptbLogsDirPath}")
        if not os.path.isdir(f"{ptbLogsDirPath}"):
            print(f"Creating logs dir: {ptbLogsDirPath}")
            os.mkdir(f"{ptbLogsDirPath}")

    except OSError as error:
        print(error)

def prepArgsDict(testDurationSec, finalDurationSec, testTimeStampDirPath, maxTpsToTest, testIterationMinStep,
             tpsLimitPerGenerator, saveJsonReport, saveTestJsonReports, numAddlBlocksToPrune, testHelperConfig, testClusterConfig) -> dict:
    argsDict = {}
    argsDict.update(asdict(testHelperConfig))
    argsDict.update(asdict(testClusterConfig))
    argsDict["testDurationSec"] = testDurationSec
    argsDict["finalDurationSec"] = finalDurationSec
    argsDict["maxTpsToTest"] = maxTpsToTest
    argsDict["testIterationMinStep"] = testIterationMinStep
    argsDict["tpsLimitPerGenerator"] = tpsLimitPerGenerator
    argsDict["saveJsonReport"] = saveJsonReport
    argsDict["saveTestJsonReports"] = saveTestJsonReports
    argsDict["numAddlBlocksToPrune"] = numAddlBlocksToPrune
    argsDict["logsDir"] = testTimeStampDirPath
    return argsDict

def parseArgs():
    appArgs=AppArgs()
    appArgs.add(flag="--max-tps-to-test", type=int, help="The max target transfers realistic as ceiling of test range", default=50000)
    appArgs.add(flag="--test-iteration-duration-sec", type=int, help="The duration of transfer trx generation for each iteration of the test during the initial search (seconds)", default=30)
    appArgs.add(flag="--test-iteration-min-step", type=int, help="The step size determining granularity of tps result during initial search", default=500)
    appArgs.add(flag="--final-iterations-duration-sec", type=int, help="The duration of transfer trx generation for each final longer run iteration of the test during the final search (seconds)", default=90)
    appArgs.add(flag="--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    appArgs.add(flag="--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
    appArgs.add(flag="--num-blocks-to-prune", type=int, help="The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end of the range of blocks of interest for evaluation.", default=2)
    appArgs.add(flag="--save-json", type=bool, help="Whether to save overarching performance run report.", default=False)
    appArgs.add(flag="--save-test-json", type=bool, help="Whether to save json reports from each test scenario.", default=False)
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

    rootLogDir: str=os.path.splitext(os.path.basename(__file__))[0]
    testTimeStampDirPath = f"{rootLogDir}/{datetime.now().strftime('%Y-%m-%d_%H-%M-%S')}"
    ptbLogsDirPath = f"{testTimeStampDirPath}/testRunLogs"

    testDirsSetup(rootLogDir=rootLogDir, testTimeStampDirPath=testTimeStampDirPath, ptbLogsDirPath=ptbLogsDirPath)

    testHelperConfig = PerformanceBasicTest.TestHelperConfig(killAll=killAll, dontKill=dontKill, keepLogs=keepLogs,
                                                             dumpErrorDetails=dumpErrorDetails, delay=delay, nodesFile=nodesFile,
                                                             verbose=verbose)

    testClusterConfig = PerformanceBasicTest.ClusterConfig(pnodes=pnodes, totalNodes=totalNodes, topo=topo, genesisPath=genesisPath)

    argsDict = prepArgsDict(testDurationSec=testDurationSec, finalDurationSec=finalDurationSec, testTimeStampDirPath=testTimeStampDirPath,
                        maxTpsToTest=maxTpsToTest, testIterationMinStep=testIterationMinStep, tpsLimitPerGenerator=tpsLimitPerGenerator,
                        saveJsonReport=saveJsonReport, saveTestJsonReports=saveTestJsonReports, numAddlBlocksToPrune=numAddlBlocksToPrune, testHelperConfig=testHelperConfig, testClusterConfig=testClusterConfig)

    perfRunSuccessful = False

    try:
        binSearchResults = performPtbBinarySearch(tpsTestFloor=0, tpsTestCeiling=maxTpsToTest, minStep=testIterationMinStep, testHelperConfig=testHelperConfig,
                           testClusterConfig=testClusterConfig, testDurationSec=testDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                           numAddlBlocksToPrune=numAddlBlocksToPrune, testLogDir=ptbLogsDirPath, saveJson=saveTestJsonReports)

        print(f"Successful rate of: {binSearchResults.maxTpsAchieved}")

        print("Search Results:")
        for i in range(len(binSearchResults.searchResults)):
            print(f"Search scenario: {i} result: {binSearchResults.searchResults[i]}")

        longRunningFloor = binSearchResults.maxTpsAchieved - 3 * testIterationMinStep if binSearchResults.maxTpsAchieved - 3 * testIterationMinStep > 0 else 0
        longRunningCeiling = binSearchResults.maxTpsAchieved + 3 * testIterationMinStep

        longRunningBinSearchResults = performPtbBinarySearch(tpsTestFloor=longRunningFloor, tpsTestCeiling=longRunningCeiling, minStep=testIterationMinStep, testHelperConfig=testHelperConfig,
                           testClusterConfig=testClusterConfig, testDurationSec=finalDurationSec, tpsLimitPerGenerator=tpsLimitPerGenerator,
                           numAddlBlocksToPrune=numAddlBlocksToPrune, testLogDir=ptbLogsDirPath, saveJson=saveTestJsonReports)

        print(f"Long Running Test - Successful rate of: {longRunningBinSearchResults.maxTpsAchieved}")
        perfRunSuccessful = True

        print("Long Running Test - Search Results:")
        for i in range(len(longRunningBinSearchResults.searchResults)):
            print(f"Search scenario: {i} result: {longRunningBinSearchResults.searchResults[i]}")

        fullReport = createJSONReport(maxTpsAchieved=binSearchResults.maxTpsAchieved, searchResults=binSearchResults.searchResults, maxTpsReport=binSearchResults.maxTpsReport,
                                      longRunningMaxTpsAchieved=longRunningBinSearchResults.maxTpsAchieved, longRunningSearchResults=longRunningBinSearchResults.searchResults,
                                      longRunningMaxTpsReport=longRunningBinSearchResults.maxTpsReport, argsDict=argsDict)

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
