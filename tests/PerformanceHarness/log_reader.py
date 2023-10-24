#!/usr/bin/env python3

import sys
import re
import numpy as np
import json
import gzip

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

from TestHarness import Utils, Account
from dataclasses import dataclass, field
from datetime import datetime, timedelta
from typing import List
from pathlib import Path

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError

COMPLETEPRODUCTIONWINDOWSIZE = 12

@dataclass
class ArtifactPaths:
    nodeosLogDir: Path = Path("")
    nodeosLogPath: Path = Path("")
    trxGenLogDirPath: Path = Path("")
    blockTrxDataPath: Path = Path("")
    blockDataPath: Path = Path("")
    transactionMetricsDataPath: Path = Path("")

@dataclass
class TpsTestConfig:
    targetTps: int = 0
    testDurationSec: int = 0
    tpsLimitPerGenerator: int = 0
    numBlocksToPrune: int = 0
    numTrxGensUsed: int = 0
    targetTpsPerGenList: List[int] = field(default_factory=list)
    quiet: bool = False
    printMissingTransactions: bool=True

@dataclass
class stats():
    min: int = 0
    max: int = 0
    avg: float = 0
    sigma: float = 0
    emptyBlocks: int = 0
    numBlocks: int = 0

@dataclass
class basicStats():
    min: float = 0
    max: float = 0
    avg: float = 0
    sigma: float = 0
    samples: int = 0

@dataclass
class trxData():
    blockNum: int = 0
    cpuUsageUs: int = 0
    netUsageUs: int = 0
    blockTime: datetime = None
    latency: float = 0
    acknowledged: str = "NA"
    ackRespTimeUs: int = -1
    _sentTimestamp: str = ""
    _calcdTimeEpoch: float = 0

    @property
    def sentTimestamp(self):
        return self._sentTimestamp

    @property
    def calcdTimeEpoch(self):
        return self._calcdTimeEpoch

    @sentTimestamp.setter
    def sentTimestamp(self, sentTime: str):
        self._sentTimestamp = sentTime
        # When we no longer support Python 3.6, would be great to update to use this
        # self._calcdTimeEpoch = datetime.fromisoformat(sentTime).timestamp()
        self._calcdTimeEpoch = datetime.strptime(sentTime, "%Y-%m-%dT%H:%M:%S.%f").timestamp()

    @sentTimestamp.deleter
    def sentTimestamp(self):
        self._sentTimestamp = ""
        self._calcdTimeEpoch = 0

@dataclass
class productionWindow():
    producer: str = ""
    startBlock: int = 0
    endBlock: int = 0
    blockCount: int = 0

@dataclass
class productionWindows():
    totalWindows: int = 0
    averageWindowSize: float = 0
    missedWindows: int = 0

@dataclass
class chainBlocksGuide():
    firstBlockNum: int = 0
    lastBlockNum: int = 0
    totalBlocks: int = 0
    testStartBlockNum: int = 0
    testEndBlockNum: int = 0
    setupBlocksCnt: int = 0
    tearDownBlocksCnt: int = 0
    leadingEmptyBlocksCnt: int = 0
    trailingEmptyBlocksCnt: int = 0
    configAddlDropCnt: int = 0
    testAnalysisBlockCnt: int = 0

@dataclass
class blockData():
    blockId: int = 0
    blockNum: int = 0
    transactions: int = 0
    net: int = 0
    cpu: int = 0
    elapsed: int = 0
    time: int = 0
    producer: str = ""
    status: str = ""
    _timestamp: str = field(init=True, repr=True, default='')
    _calcdTimeEpoch: float = 0

    def __post_init__(self):
        self.timestamp = self._timestamp

    @property
    def timestamp(self):
        return self._timestamp

    @property
    def calcdTimeEpoch(self):
        return self._calcdTimeEpoch

    @timestamp.setter
    def timestamp(self, time: str):
        self._timestamp = time[:-1]
        # When we no longer support Python 3.6, would be great to update to use this
        # self._calcdTimeEpoch = datetime.fromisoformat(time[:-1]).timestamp()
        #Note block timestamp formatted like: '2022-09-30T16:48:13.500Z', but 'Z' is not part of python's recognized iso format, so strip it off the end
        self._calcdTimeEpoch = datetime.strptime(time[:-1], "%Y-%m-%dT%H:%M:%S.%f").timestamp()

    @timestamp.deleter
    def timestamp(self):
        self._timestamp = ""
        self._calcdTimeEpoch = 0

class chainData():
    def __init__(self):
        self.blockList = []
        self.blockDict = {}
        self.trxDict = {}
        self.startBlock = None
        self.ceaseBlock = None
        self.totalTransactions = 0
        self.totalNet = 0
        self.totalCpu = 0
        self.totalElapsed = 0
        self.totalTime = 0
        self.droppedBlocks = {}
        self.forkedBlocks = {}
        self.numNodes = 0
    def __eq__(self, other):
        return self.startBlock == other.startBlock and\
         self.ceaseBlock == other.ceaseBlock and\
         self.totalTransactions == other.totalTransactions and\
         self.totalNet == other.totalNet and\
         self.totalCpu == other.totalCpu and\
         self.totalElapsed == other.totalElapsed and\
         self.totalTime == other.totalTime and\
         self.numNodes == other.numNodes
    def updateTotal(self, transactions, net, cpu, elapsed, time):
        self.totalTransactions += transactions
        self.totalNet += net
        self.totalCpu += cpu
        self.totalElapsed += elapsed
        self.totalTime += time
    def __str__(self):
        return (f"Starting block: {self.startBlock}\nEnding block:{self.ceaseBlock}\nChain transactions: {self.totalTransactions}\n"
         f"Chain cpu: {self.totalCpu}\nChain net: {(self.totalNet / (self.ceaseBlock - self.startBlock + 1))}\nChain elapsed: {self.totalElapsed}\n"
         f"Chain time: {self.totalTime}\n")
    def assertEquality(self, other):
        assert self == other, f"Error: Actual log:\n{self}\ndid not match expected log:\n{other}"

@dataclass
class LogAnalysis:
    guide: chainBlocksGuide
    tpsStats: stats
    blockSizeStats: stats
    trxLatencyStats: basicStats
    trxCpuStats: basicStats
    trxNetStats: basicStats
    trxAckStatsApplicable: str
    trxAckStats: basicStats
    prodWindows: productionWindows
    notFound: list

def selectedOpen(path):
    return gzip.open if path.suffix == '.gz' else open

def scrapeLogBlockElapsedTime(data: chainData, path):
    # node_XX/stderr.txt where XX is the first nonproducing node
    selectedopen = selectedOpen(path)
    with selectedopen(path, 'rt') as f:
        line = f.read()
        blockResult = re.findall(r'Received block ([0-9a-fA-F]*).* #(\d+) .*trxs: (\d+)(.*)', line)
        for value in blockResult:
            if int(value[1]) in range(data.startBlock, data.ceaseBlock + 1):
                v3Logging = re.findall(r'elapsed: (\d+), time: (\d+)', value[3])
                if v3Logging:
                    data.blockDict[str(value[1])].elapsed = int(v3Logging[0][0])
                    data.blockDict[str(value[1])].time = int(v3Logging[0][1])

def scrapeLogDroppedForkedBlocks(data: chainData, path):
    for nodeNum in range(0, data.numNodes):
        nodePath = path/f"node_{str(nodeNum).zfill(2)}"/"stderr.txt"
        selectedopen = selectedOpen(path)
        with selectedopen(nodePath, 'rt') as f:
            line = f.read()
            droppedBlocksByCurrentNode = {}
            forkedBlocksByCurrentNode = []
            droppedBlocks = re.findall(r'dropped incoming block #(\d+) id: ([0-9a-fA-F]+)', line)
            for block in droppedBlocks:
                droppedBlocksByCurrentNode[block[0]] = block[1]
            forks = re.findall(r'switching forks from ([0-9a-fA-F]+) \(block number (\d+)\) to ([0-9a-fA-F]+) \(block number (\d+)\)', line)
            for fork in forks:
                forkedBlocksByCurrentNode.append(int(fork[1]) - int(fork[3]) + 1)
            data.droppedBlocks[str(nodeNum).zfill(2)] = droppedBlocksByCurrentNode
            data.forkedBlocks[str(nodeNum).zfill(2)] = forkedBlocksByCurrentNode

@dataclass
class sentTrx():
    sentTime: str = ""
    acked: str = ""
    ackResponseTimeUs: int = -1

@dataclass
class sentTrxExtTrace():
    sentTime: str = ""
    acked: str = ""
    ackResponseTimeUs: int = -1
    blockNum: int = -1
    cpuUsageUs: int = -1
    netUsageWords: int = -1
    blockTime: str = ""

def scrapeTrxGenLog(trxSent: dict, path):
    #trxGenLogs/trx_data_output_*.txt
    selectedopen = selectedOpen(path)
    with selectedopen(path, 'rt') as f:
        trxSent.update(dict([(x[0], sentTrx(x[1], x[2], x[3]) if len(x) == 4 else sentTrxExtTrace(x[1], x[2], x[3], x[4], x[5], x[6], x[7])) for x in (line.rstrip('\n').split(',') for line in f)]))

def scrapeTrxGenTrxSentDataLogs(trxSent: dict, trxGenLogDirPath, quiet):
    filesScraped = []
    for fileName in trxGenLogDirPath.glob("trx_data_output_*.txt"):
        filesScraped.append(fileName)
        scrapeTrxGenLog(trxSent, fileName)

    if not quiet:
        print(f"Transaction Log Files Scraped: {filesScraped}")

def populateTrxSentAndAcked(trxSent: dict, data: chainData, notFound):
    trxDict = data.trxDict
    for sentTrxId in trxSent.keys():
        if (isinstance(trxSent[sentTrxId], sentTrxExtTrace)):
            trxDict[sentTrxId] = trxData(blockNum=trxSent[sentTrxId].blockNum, cpuUsageUs=trxSent[sentTrxId].cpuUsageUs, netUsageUs=trxSent[sentTrxId].netUsageWords, blockTime=trxSent[sentTrxId].blockTime, acknowledged=trxSent[sentTrxId].acked, ackRespTimeUs=trxSent[sentTrxId].ackResponseTimeUs)
            trxDict[sentTrxId].sentTimestamp = trxSent[sentTrxId].sentTime
            data.blockDict[str(trxSent[sentTrxId].blockNum)].transactions +=1
        elif sentTrxId in trxDict.keys():
            trxDict[sentTrxId].sentTimestamp = trxSent[sentTrxId].sentTime
            trxDict[sentTrxId].acknowledged = trxSent[sentTrxId].acked
            trxDict[sentTrxId].ackRespTimeUs = trxSent[sentTrxId].ackResponseTimeUs
        else:
            notFound.append(sentTrxId)

def populateTrxLatencies(data: chainData):
    for trxId, trxData in data.trxDict.items():
        if trxData.calcdTimeEpoch != 0:
            data.trxDict[trxId].latency = data.blockDict[str(trxData.blockNum)].calcdTimeEpoch - trxData.calcdTimeEpoch

def updateBlockTotals(data: chainData):
    for _, block in data.blockDict.items():
        data.updateTotal(transactions=block.transactions, net=block.net, cpu=block.cpu, elapsed=block.elapsed, time=block.time)

def writeTransactionMetrics(trxDict: dict, path):
    with open(path, 'wt') as transactionMetricsFile:
        transactionMetricsFile.write("TransactionId,BlockNumber,BlockTime,CpuUsageUs,NetUsageUs,Latency,SentTimestamp,CalcdTimeEpoch,Acknowledged,SentToAckDurationUs\n")
        for trxId, data in trxDict.items():
            transactionMetricsFile.write(f"{trxId},{data.blockNum},{data.blockTime},{data.cpuUsageUs},{data.netUsageUs},{data.latency},{data._sentTimestamp},{data._calcdTimeEpoch}{data.acknowledged}{data.ackRespTimeUs}\n")

def getProductionWindows(prodDict: dict, data: chainData):
    prod = ""
    count = 0
    blocksFromCurProd = 0
    numProdWindows = 0
    for k, v in data.blockDict.items():
        count += 1
        if prod == "":
            prod = v.producer
        if prod != v.producer or count+data.startBlock == data.ceaseBlock:
            prodDict[str(numProdWindows)] = productionWindow(prod, count-blocksFromCurProd+data.startBlock-1, count+data.startBlock-2, blocksFromCurProd)
            prod = v.producer
            blocksFromCurProd = 1
            numProdWindows += 1
        else:
            blocksFromCurProd += 1
    return prodDict

def calcProductionWindows(prodDict: dict):
    prodWindows = productionWindows()
    totalBlocksForAverage = 0
    for i, (k, v) in enumerate(prodDict.items()):
        if v.blockCount == COMPLETEPRODUCTIONWINDOWSIZE:
            prodWindows.totalWindows += 1
            totalBlocksForAverage += v.blockCount
        else:
            #First and last production windows are possibly incomplete but
            #should not count against total or missed windows
            if i != 0 and i != len(prodDict)-1:
                prodWindows.missedWindows += 1
                totalBlocksForAverage += v.blockCount
                prodWindows.totalWindows += 1
    if prodWindows.totalWindows > 0:
        prodWindows.averageWindowSize = totalBlocksForAverage / prodWindows.totalWindows
    return prodWindows

def calcChainGuide(data: chainData, numAddlBlocksToDrop=0) -> chainBlocksGuide:
    """Calculates guide to understanding key points/blocks in chain data. In particular, test scenario phases like setup, teardown, etc.

    This includes breaking out 3 distinct ranges of blocks from the total block data log:
    1) Blocks during test scenario setup and tear down
    2) Empty blocks during test scenario ramp up and ramp down
    3) Additional blocks - potentially partially full blocks while test scenario ramps up to steady state

    Keyword arguments:
    data -- the chainData for the test run.  Includes blockList, startBlock, and ceaseBlock
    numAddlBlocksToDrop -- num potentially non-empty blocks to ignore at beginning and end of test for steady state purposes

    Returns:
    chain guide describing key blocks and counts of blocks to describe test scenario
    """
    firstBN = data.blockList[0].blockNum
    lastBN = data.blockList[-1].blockNum
    total = len(data.blockList)
    testStartBN = data.startBlock
    testEndBN = data.ceaseBlock

    setupCnt = 0
    if data.startBlock is not None:
        setupCnt = data.startBlock - firstBN

    tearDownCnt = 0
    if data.ceaseBlock is not None:
        tearDownCnt = lastBN - data.ceaseBlock

    leadingEmpty = 0
    for le in range(setupCnt, total - tearDownCnt - 1):
        if data.blockList[le].transactions == 0:
            leadingEmpty += 1
        else:
            break

    trailingEmpty = 0
    for te in range(total - tearDownCnt - 1, setupCnt + leadingEmpty, -1):
        if data.blockList[te].transactions == 0:
            trailingEmpty += 1
        else:
            break

    testAnalysisBCnt = total - setupCnt - tearDownCnt - leadingEmpty - trailingEmpty - ( 2 * numAddlBlocksToDrop )
    testAnalysisBCnt = 0 if testAnalysisBCnt < 0 else testAnalysisBCnt

    return chainBlocksGuide(firstBN, lastBN, total, testStartBN, testEndBN, setupCnt, tearDownCnt, leadingEmpty, trailingEmpty, numAddlBlocksToDrop, testAnalysisBCnt)

def pruneToSteadyState(data: chainData, guide: chainBlocksGuide):
    """Prunes the block data log down to range of blocks when steady state has been reached.

    This includes pruning out 3 distinct ranges of blocks from the total block data log:
    1) Blocks during test scenario setup and tear down
    2) Empty blocks during test scenario ramp up and ramp down
    3) Additional blocks - potentially partially full blocks while test scenario ramps up to steady state

    Keyword arguments:
    data -- the chainData for the test run.  Includes blockList, startBlock, and ceaseBlock
    guide -- chain guiderails calculated over chain data to guide interpretation of whole run's block data

    Returns:
    pruned list of blockData representing steady state operation
    """

    return data.blockList[guide.setupBlocksCnt + guide.leadingEmptyBlocksCnt + guide.configAddlDropCnt:-(guide.tearDownBlocksCnt + guide.trailingEmptyBlocksCnt + guide.configAddlDropCnt)]

def scoreTransfersPerSecond(data: chainData, guide: chainBlocksGuide) -> stats:
    """Analyzes a test scenario's steady state block data for statistics around transfers per second over every two-consecutive-block window"""
    prunedBlockDataLog = pruneToSteadyState(data, guide)

    blocksToAnalyze = len(prunedBlockDataLog)
    if blocksToAnalyze == 0:
        return stats()
    elif blocksToAnalyze == 1:
        onlyBlockTrxs = prunedBlockDataLog[0].transactions
        return stats(onlyBlockTrxs, onlyBlockTrxs, onlyBlockTrxs, 0, int(onlyBlockTrxs == 0), 1)
    else:
        # Calculate the num trxs in each two-consecutive-block window and count any empty blocks in range.
        # for instance: given 4 blocks [1, 2, 3, 4], the two-consecutive-block windows analyzed would be [(1,2),(2,3),(3,4)]
        consecBlkTrxsAndEmptyCnt = [(first.transactions + second.transactions, int(first.transactions == 0)) for first, second in zip(prunedBlockDataLog, prunedBlockDataLog[1:])]

        npCBTAEC = np.array(consecBlkTrxsAndEmptyCnt, dtype=np.uint)

        # Note: numpy array slicing in use -> [:,0] -> from all elements return index 0
        return stats(int(np.min(npCBTAEC[:,0])), int(np.max(npCBTAEC[:,0])), float(np.average(npCBTAEC[:,0])), float(np.std(npCBTAEC[:,0])), int(np.sum(npCBTAEC[:,1])), len(prunedBlockDataLog))

def calcBlockSizeStats(data: chainData, guide : chainBlocksGuide) -> stats:
    """Analyzes a test scenario's steady state block data for block size statistics during the test window"""
    prunedBlockDataLog = pruneToSteadyState(data, guide)

    blocksToAnalyze = len(prunedBlockDataLog)
    if blocksToAnalyze == 0:
        return stats()
    elif blocksToAnalyze == 1:
        onlyBlockNetSize = prunedBlockDataLog[0].net
        return stats(onlyBlockNetSize, onlyBlockNetSize, onlyBlockNetSize, 0, int(onlyBlockNetSize == 0), 1)
    else:
        blockSizeList = [(blk.net, int(blk.net == 0)) for blk in prunedBlockDataLog]

        npBlkSizeList = np.array(blockSizeList, dtype=np.uint)

        # Note: numpy array slicing in use -> [:,0] -> from all elements return index 0
        return stats(int(np.min(npBlkSizeList[:,0])), int(np.max(npBlkSizeList[:,0])), float(np.average(npBlkSizeList[:,0])), float(np.std(npBlkSizeList[:,0])), int(np.sum(npBlkSizeList[:,1])), len(prunedBlockDataLog))

def calcTrxLatencyCpuNetStats(trxDict : dict):
    """Analyzes a test scenario's steady state block data for transaction latency statistics during the test window

    Keyword arguments:
    trxDict -- the dictionary mapping trx id to trxData, wherein the trx sent timestamp has been populated from the trx generator at moment of send

    Returns:
    transaction latency stats as a basicStats object
    """
    trxLatencyCpuNetAckList = [(data.latency, data.cpuUsageUs, data.netUsageUs, data.ackRespTimeUs) for trxId, data in trxDict.items() if data.calcdTimeEpoch != 0]

    npLatencyCpuNetAckList = np.array(trxLatencyCpuNetAckList, dtype=float)

    return basicStats(float(np.min(npLatencyCpuNetAckList[:,0])), float(np.max(npLatencyCpuNetAckList[:,0])), float(np.average(npLatencyCpuNetAckList[:,0])), float(np.std(npLatencyCpuNetAckList[:,0])), len(npLatencyCpuNetAckList)), \
           basicStats(float(np.min(npLatencyCpuNetAckList[:,1])), float(np.max(npLatencyCpuNetAckList[:,1])), float(np.average(npLatencyCpuNetAckList[:,1])), float(np.std(npLatencyCpuNetAckList[:,1])), len(npLatencyCpuNetAckList)), \
           basicStats(float(np.min(npLatencyCpuNetAckList[:,2])), float(np.max(npLatencyCpuNetAckList[:,2])), float(np.average(npLatencyCpuNetAckList[:,2])), float(np.std(npLatencyCpuNetAckList[:,2])), len(npLatencyCpuNetAckList)), \
           basicStats(float(np.min(npLatencyCpuNetAckList[:,3])), float(np.max(npLatencyCpuNetAckList[:,3])), float(np.average(npLatencyCpuNetAckList[:,3])), float(np.std(npLatencyCpuNetAckList[:,3])), len(npLatencyCpuNetAckList))

class LogReaderEncoder(json.JSONEncoder):
    def default(self, obj):
        if isinstance(obj, datetime):
            return obj.isoformat()
        if isinstance(obj, timedelta):
            return str(obj)
        if isinstance(obj, PurePath):
            return str(obj)
        if obj is None:
            return "Unknown"
        if isinstance(obj, Path):
            return str(obj)
        if isinstance(obj, Account):
            return str(obj)
        defaultStr = ""
        try:
            defaultStr = json.JSONEncoder.default(self, obj)
        except TypeError as err:
            defaultStr = f"ERROR: {str(err)}"
        return defaultStr

class JsonReportHandler:
    def reportAsJSON(report: dict) -> json:
        return json.dumps(report, indent=2, cls=LogReaderEncoder)

    def exportReportAsJSON(report: json, exportPath):
        with open(exportPath, 'wt') as f:
            f.write(report)

def analyzeLogResults(data: chainData, tpsTestConfig: TpsTestConfig, artifacts: ArtifactPaths) -> LogAnalysis:
    scrapeLogBlockElapsedTime(data, artifacts.nodeosLogPath)
    scrapeLogDroppedForkedBlocks(data, artifacts.nodeosLogDir)

    trxSent = {}
    scrapeTrxGenTrxSentDataLogs(trxSent, artifacts.trxGenLogDirPath, tpsTestConfig.quiet)

    trxAckStatsApplicable="NOT APPLICABLE" if list(trxSent.values())[0].acked == "NA" else "APPLICABLE"

    notFound = []
    populateTrxSentAndAcked(trxSent, data, notFound)

    prodDict = {}
    getProductionWindows(prodDict, data)

    if len(notFound) > 0:
        print(f"Transactions logged as sent but NOT FOUND in block!! lost {len(notFound)} out of {len(trxSent)}")
        if tpsTestConfig.printMissingTransactions:
            print(notFound)

    updateBlockTotals(data)
    populateTrxLatencies(data)
    writeTransactionMetrics(data.trxDict, artifacts.transactionMetricsDataPath)
    guide = calcChainGuide(data, tpsTestConfig.numBlocksToPrune)
    trxLatencyStats, trxCpuStats, trxNetStats, trxAckStats = calcTrxLatencyCpuNetStats(data.trxDict)
    tpsStats = scoreTransfersPerSecond(data, guide)
    blkSizeStats = calcBlockSizeStats(data, guide)
    prodWindows = calcProductionWindows(prodDict)

    if not tpsTestConfig.quiet:
        print(f"Blocks Guide: {guide}\nTPS: {tpsStats}\nBlock Size: {blkSizeStats}\nTrx Latency: {trxLatencyStats}\nTrx CPU: {trxCpuStats}\nTrx Net: {trxNetStats}")

    return LogAnalysis(guide=guide, tpsStats=tpsStats, blockSizeStats=blkSizeStats, trxLatencyStats=trxLatencyStats, trxCpuStats=trxCpuStats, trxNetStats=trxNetStats,
                       trxAckStatsApplicable=trxAckStatsApplicable, trxAckStats=trxAckStats, prodWindows=prodWindows, notFound=notFound)
