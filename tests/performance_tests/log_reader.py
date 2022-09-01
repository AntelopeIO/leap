#!/usr/bin/env python3

import os
import sys
import re

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from testUtils import Utils
from dataclasses import dataclass

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30
emptyBlockGoal = 5

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

def waitForEmptyBlocks(node):
    emptyBlocks = 0
    while emptyBlocks < emptyBlockGoal:
        headBlock = node.getHeadBlockNum()
        block = node.processCurlCmd("chain", "get_block_info", f'{{"block_num":{headBlock}}}', silentErrors=False, exitOnError=True)
        node.waitForHeadToAdvance()
        if block['transaction_mroot'] == "0000000000000000000000000000000000000000000000000000000000000000":
            emptyBlocks += 1
        else:
            emptyBlocks = 0
    return node.getHeadBlockNum()

def fetchStats(total, path):
    with open(path) as f:
        blockResult = re.findall(r'Received block ([0-9a-fA-F]*).* #(\d+) .*trxs: (\d+)(.*)', f.read())
        if total.ceaseBlock == 0:
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
