#!/usr/bin/env python3

import os
import sys
import re
import numpy

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Utils
from dataclasses import dataclass

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError

@dataclass
class stats():
    min: int = 0
    max: int = 0
    avg: int = 0
    sigma: int = 0

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
        self.startBlock = 0
        self.ceaseBlock = 0
        self.totalTransactions = 0
        self.totalNet = 0
        self.totalCpu = 0
        self.totalElapsed = 0
        self.totalTime = 0
        self.totalLatency = 0
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

def scrapeLog(total, path):
    with open(path) as f:
        blockResult = re.findall(r'Received block ([0-9a-fA-F]*).* #(\d+) .*trxs: (\d+)(.*)', f.read())
        if total.ceaseBlock is None:
            total.ceaseBlock = len(blockResult) + 1
        for value in blockResult:
            v3Logging = re.findall(r'net: (\d+), cpu: (\d+), elapsed: (\d+), time: (\d+), latency: (-?\d+) ms', value[3])
            if v3Logging:
                total.blockLog.append(blockData(value[0], int(value[1]), int(value[2]), int(v3Logging[0][0]), int(v3Logging[0][1]), int(v3Logging[0][2]), int(v3Logging[0][3]), int(v3Logging[0][4])))
                if int(value[1]) in range(total.startBlock, total.ceaseBlock + 1):
                    total.updateTotal(int(value[2]), int(v3Logging[0][0]), int(v3Logging[0][1]), int(v3Logging[0][2]), int(v3Logging[0][3]), int(v3Logging[0][4]))
            else:
                v2Logging = re.findall(r'latency: (-?\d+) ms', value[3])
                if v2Logging:
                    total.blockLog.append(blockData(value[0], int(value[1]), int(value[2]), 0, 0, 0, 0, int(v2Logging[0])))
                    if int(value[1]) in range(total.startBlock, total.ceaseBlock + 1):
                        total.updateTotal(int(value[2]), 0, 0, 0, 0, int(v2Logging[0]))
                else:
                    print("Error: Unknown log format")

def findPrunedRangeOfInterest(data: chainData, numAddlBlocksToDrop=0):

    startBlockIndex = 0
    endBlockIndex = len(data.blockLog) - 1

    #skip leading empty blocks in initial range of interest as well as specified number of potentially non-empty blocks
    droppedBlocks = 0
    for block in data.blockLog:
        if block.blockNum < data.startBlock or (droppedBlocks == 0 and block.transactions == 0):
            continue
        else:
            if droppedBlocks < numAddlBlocksToDrop:
                droppedBlocks += 1
                continue
            else:
                startBlockIndex = data.blockLog.index(block)
                break

    #skip trailing empty blocks at end of initial range of interest as well as specified number of potentially non-empty blocks
    droppedBlocks = 0
    for block in reversed(data.blockLog):
        if block.blockNum > data.ceaseBlock or (droppedBlocks == 0 and block.transactions == 0):
            continue
        else:
            if droppedBlocks < numAddlBlocksToDrop:
                droppedBlocks += 1
                continue
            else:
                endBlockIndex = data.blockLog.index(block)
                break

    return startBlockIndex,endBlockIndex

def scoreTransfersPerSecond(data: chainData, numAddlBlocksToDrop=0) -> stats:
    consecutiveBlockTps = []

    startBlockIndex,endBlockIndex = findPrunedRangeOfInterest(data, numAddlBlocksToDrop)

    if startBlockIndex >= endBlockIndex:
        print(f"Error: Invalid block index range start: {startBlockIndex} end: {endBlockIndex}")
        return stats()

    for i in range(startBlockIndex, endBlockIndex+1):
        if i + 1 < endBlockIndex:
            consecutiveBlockTps.append(data.blockLog[i].transactions + data.blockLog[i+1].transactions)

    return stats(numpy.min(consecutiveBlockTps) , numpy.max(consecutiveBlockTps), numpy.average(consecutiveBlockTps), numpy.std(consecutiveBlockTps))
