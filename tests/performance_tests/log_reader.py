#!/usr/bin/env python3

import os
import sys
import re
import numpy as np

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Utils
from dataclasses import dataclass
import gzip

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError

@dataclass
class stats():
    min: int = 0
    max: int = 0
    avg: int = 0
    sigma: int = 0
    emptyBlocks: int = 0
    numBlocks: int = 0

@dataclass
class chainGuide():
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
    partialBlockId: str = ""
    blockNum: int = 0
    transactions: int = 0
    net: int = 0
    cpu: int = 0
    elapsed: int = 0
    time: int = 0
    latency: int = 0

class chainData():
    def __init__(self):
        self.blockLog = []
        self.startBlock = None
        self.ceaseBlock = None
        self.totalTransactions = 0
        self.totalNet = 0
        self.totalCpu = 0
        self.totalElapsed = 0
        self.totalTime = 0
        self.totalLatency = 0
    def __eq__(self, other):
        return self.startBlock == other.startBlock and\
         self.ceaseBlock == other.ceaseBlock and\
         self.totalTransactions == other.totalTransactions and\
         self.totalNet == other.totalNet and\
         self.totalCpu == other.totalCpu and\
         self.totalElapsed == other.totalElapsed and\
         self.totalTime == other.totalTime and\
         self.totalLatency == other.totalLatency
    def updateTotal(self, transactions, net, cpu, elapsed, time, latency):
        self.totalTransactions += transactions
        self.totalNet += net
        self.totalCpu += cpu
        self.totalElapsed += elapsed
        self.totalTime += time
        self.totalLatency += latency
    def __str__(self):
        return (f"Starting block: {self.startBlock}\nEnding block:{self.ceaseBlock}\nChain transactions: {self.totalTransactions}\n"
         f"Chain cpu: {self.totalCpu}\nChain net: {(self.totalNet / (self.ceaseBlock - self.startBlock + 1))}\nChain elapsed: {self.totalElapsed}\n"
         f"Chain time: {self.totalTime}\nChain latency: {self.totalLatency}")
    def printBlockData(self):
        for block in self.blockLog:
            print(block)
    def assertEquality(self, other):
        assert self == other, f"Error: Actual log:\n{self}\ndid not match expected log:\n{other}"

def scrapeLog(data, path):
    selectedopen = gzip.open if path.endswith('.gz') else open
    with selectedopen(path, 'rt') as f:
        blockResult = re.findall(r'Received block ([0-9a-fA-F]*).* #(\d+) .*trxs: (\d+)(.*)', f.read())
        if data.startBlock is None:
            data.startBlock = 2
        if data.ceaseBlock is None:
            data.ceaseBlock = len(blockResult) + 1
        for value in blockResult:
            v3Logging = re.findall(r'net: (\d+), cpu: (\d+), elapsed: (\d+), time: (\d+), latency: (-?\d+) ms', value[3])
            if v3Logging:
                data.blockLog.append(blockData(value[0], int(value[1]), int(value[2]), int(v3Logging[0][0]), int(v3Logging[0][1]), int(v3Logging[0][2]), int(v3Logging[0][3]), int(v3Logging[0][4])))
                if int(value[1]) in range(data.startBlock, data.ceaseBlock + 1):
                    data.updateTotal(int(value[2]), int(v3Logging[0][0]), int(v3Logging[0][1]), int(v3Logging[0][2]), int(v3Logging[0][3]), int(v3Logging[0][4]))
            else:
                v2Logging = re.findall(r'latency: (-?\d+) ms', value[3])
                if v2Logging:
                    data.blockLog.append(blockData(value[0], int(value[1]), int(value[2]), 0, 0, 0, 0, int(v2Logging[0])))
                    if int(value[1]) in range(data.startBlock, data.ceaseBlock + 1):
                        data.updateTotal(int(value[2]), 0, 0, 0, 0, int(v2Logging[0]))
                else:
                    print("Error: Unknown log format")

def calcChainGuide(data: chainData, numAddlBlocksToDrop=0) -> chainGuide:
    """Calculates guide to understanding key points/blocks in chain data. In particular, test scenario phases like setup, teardown, etc.

    This includes breaking out 3 distinct ranges of blocks from the total block data log:
    1) Blocks during test scenario setup and tear down
    2) Empty blocks during test scenario ramp up and ramp down
    3) Additional blocks - potentially partially full blocks while test scenario ramps up to steady state

    Keyword arguments:
    data -- the chainData for the test run.  Includes blockLog, startBlock, and ceaseBlock
    numAddlBlocksToDrop -- num potentially non-empty blocks to ignore at beginning and end of test for steady state purposes

    Returns:
    chain guide describing key blocks and counts of blocks to describe test scenario
    """
    firstBN = data.blockLog[0].blockNum
    lastBN = data.blockLog[-1].blockNum
    total = len(data.blockLog)
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
        if data.blockLog[le].transactions == 0:
            leadingEmpty += 1
        else:
            break

    trailingEmpty = 0
    for te in range(total - tearDownCnt - 1, setupCnt + leadingEmpty, -1):
        if data.blockLog[te].transactions == 0:
            trailingEmpty += 1
        else:
            break

    testAnalysisBCnt = total - setupCnt - tearDownCnt - leadingEmpty - trailingEmpty - ( 2 * numAddlBlocksToDrop )
    testAnalysisBCnt = 0 if testAnalysisBCnt < 0 else testAnalysisBCnt

    return chainGuide(firstBN, lastBN, total, testStartBN, testEndBN, setupCnt, tearDownCnt, leadingEmpty, trailingEmpty, numAddlBlocksToDrop, testAnalysisBCnt)

def pruneToSteadyState(data: chainData, guide: chainGuide):
    """Prunes the block data log down to range of blocks when steady state has been reached.

    This includes pruning out 3 distinct ranges of blocks from the total block data log:
    1) Blocks during test scenario setup and tear down
    2) Empty blocks during test scenario ramp up and ramp down
    3) Additional blocks - potentially partially full blocks while test scenario ramps up to steady state

    Keyword arguments:
    data -- the chainData for the test run.  Includes blockLog, startBlock, and ceaseBlock
    guide -- chain guiderails calculated over chain data to guide interpretation of whole run's block data

    Returns:
    pruned list of blockData representing steady state operation
    """

    return data.blockLog[guide.setupBlocksCnt + guide.leadingEmptyBlocksCnt + guide.configAddlDropCnt:-(guide.tearDownBlocksCnt + guide.trailingEmptyBlocksCnt + guide.configAddlDropCnt)]

def scoreTransfersPerSecond(data: chainData, guide : chainGuide) -> stats:
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
        return stats(np.min(npCBTAEC[:,0]), np.max(npCBTAEC[:,0]), np.average(npCBTAEC[:,0]), np.std(npCBTAEC[:,0]), np.sum(npCBTAEC[:,1]), len(prunedBlockDataLog))
