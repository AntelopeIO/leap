#!/usr/bin/env python3
# Unit tests to ensure that nodeos log scraping and evaluation behavior from log_reader.py does not change
# Also ensures that all versions of nodeos logs can be handled
import log_reader

from pathlib import Path

testSuccessful = False

# Test log scraping for 3.2 log format
dataCurrent = log_reader.chainData()
dataCurrent.startBlock = None
dataCurrent.ceaseBlock = None
log_reader.scrapeLog(dataCurrent, Path("tests")/"performance_tests"/"nodeos_log_3_2.txt.gz")

expectedCurrent = log_reader.chainData()
expectedCurrent.startBlock = 2
expectedCurrent.ceaseBlock = 265
expectedCurrent.totalTransactions = 133
expectedCurrent.totalNet = 105888
expectedCurrent.totalCpu = 27275
expectedCurrent.totalElapsed = 7704
expectedCurrent.totalTime = 5743400
expectedCurrent.totalLatency = -9398

dataCurrent.assertEquality(expectedCurrent)

# First test full block data stats with no pruning
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataCurrent, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataCurrent, guide)
blkSizeStats = log_reader.calcBlockSizeStats(dataCurrent, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 264, expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 0, 0, 15, 30, 0, 264-15-30)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(0, 21, 1.2110091743119267, 3.2256807673357684, 147, 219)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"
expectedBlkSizeStats = log_reader.stats(0, 66920, 483.5068493150685, 4582.238297120407, 147, 219)
assert expectedBlkSizeStats == blkSizeStats , f"Error: Stats calculated: {blkSizeStats} did not match expected stats: {expectedBlkSizeStats}"

# Next test block data stats with empty block pruning
dataCurrent.startBlock = 105
dataCurrent.ceaseBlock = 257
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataCurrent, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataCurrent, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 264, 105, 257, 103, 8, 12, 22, 0, 264-103-8-12-22)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(1, 1, 1.0, 0.0, 59, 119)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

# Next test block data stats with additional block pruning
dataCurrent.startBlock = 105
dataCurrent.ceaseBlock = 257
numAddlBlocksToPrune = 2
guide = log_reader.calcChainGuide(dataCurrent, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataCurrent, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 264, 105, 257, 103, 8, 12, 22, 2, 264-103-8-12-22-4)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(1, 1, 1.0, 0.0, 57, 115)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

# Next test block data stats with 0 blocks left
dataCurrent.startBlock = 117
dataCurrent.ceaseBlock = 118
numAddlBlocksToPrune = 2
guide = log_reader.calcChainGuide(dataCurrent, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataCurrent, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 264, 117, 118, 115, 147, 0, 1, 2, 0)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(0, 0, 0, 0.0, 0, 0)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

# Next test block data stats with 1 block left
dataCurrent.startBlock = 117
dataCurrent.ceaseBlock = 117
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataCurrent, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataCurrent, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 264, 117, 117, 115, 148, 0, 0, 0, 264-115-148)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(1, 1, 1.0, 0.0, 0, 1)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

# Next test block data stats with 2 blocks left
dataCurrent.startBlock = 80
dataCurrent.ceaseBlock = 81
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataCurrent, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataCurrent, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedCurrent.startBlock, expectedCurrent.ceaseBlock, 264, 80, 81, 78, 184, 0, 0, 0, 264-78-184)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(3, 3, 3, 0.0, 0, 2)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"


# Test log scraping from a 2.0.14 log format
dataOld = log_reader.chainData()
dataOld.startBlock = None
dataOld.ceaseBlock = None
log_reader.scrapeLog(dataOld, Path("tests")/"performance_tests"/"nodeos_log_2_0_14.txt.gz")
expectedOld = log_reader.chainData()
expectedOld.startBlock = 2
expectedOld.ceaseBlock = 93
expectedOld.totalTransactions = 129
# Net, Cpu, Elapsed, and Time are not logged in the old logging and will thus be 0
expectedOld.totalNet = 0
expectedOld.totalCpu = 0
expectedOld.totalElapsed = 0
expectedOld.totalTime = 0
expectedOld.totalLatency = -5802

dataOld.assertEquality(expectedOld)

# First test full block data stats with no pruning
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataOld, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataOld, guide)
blkSizeStats = log_reader.calcBlockSizeStats(dataOld, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedOld.startBlock, expectedOld.ceaseBlock, 92, 2, 93, 0, 0, 17, 9, 0, 92-17-9)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(0, 61, 3.753846153846154, 11.38153804562563, 51, 66)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"
expectedBlkSizeStats = log_reader.stats(0, 0, 0, 0, 66, 66)
assert expectedBlkSizeStats == blkSizeStats , f"Error: Stats calculated: {blkSizeStats} did not match expected stats: {expectedBlkSizeStats}"

# Next test block data stats with empty block pruning
dataOld.startBlock = 15
dataOld.ceaseBlock = 33
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataOld, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataOld, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedOld.startBlock, expectedOld.ceaseBlock, 92, 15, 33, 13, 60, 4, 6, 0, 92-13-60-4-6)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(0, 61, 24.5, 22.666053913286273, 3, 9)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"


# Next test block data stats with additional block pruning
dataOld.startBlock = 15
dataOld.ceaseBlock = 33
numAddlBlocksToPrune = 2
guide = log_reader.calcChainGuide(dataOld, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataOld, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedOld.startBlock, expectedOld.ceaseBlock, 92, 15, 33, 13, 60, 4, 6, 2, 92-13-60-4-6-4)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(0, 52, 17.75, 21.241174637952582, 2, 5)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"


# Next test block data stats with 0 blocks left
dataOld.startBlock = 19
dataOld.ceaseBlock = 20
numAddlBlocksToPrune = 2
guide = log_reader.calcChainGuide(dataOld, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataOld, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedOld.startBlock, expectedOld.ceaseBlock, 92, 19, 20, 17, 73, 0, 0, 2, 0)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(0, 0, 0, 0.0, 0, 0)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

# Next test block data stats with 1 block left
dataOld.startBlock = 19
dataOld.ceaseBlock = 19
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataOld, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataOld, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedOld.startBlock, expectedOld.ceaseBlock, 92, 19, 19, 17, 74, 0, 0, 0, 92-17-74)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(13, 13, 13.0, 0.0, 0, 1)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

# Next test block data stats with 2 blocks left
dataOld.startBlock = 19
dataOld.ceaseBlock = 20
numAddlBlocksToPrune = 0
guide = log_reader.calcChainGuide(dataOld, numAddlBlocksToPrune)
stats = log_reader.scoreTransfersPerSecond(dataOld, guide)

expectedGuide = log_reader.chainBlocksGuide(expectedOld.startBlock, expectedOld.ceaseBlock, 92, 19, 20, 17, 73, 0, 0, 0, 92-17-73)
assert expectedGuide == guide, f"Error: Guide calculated: {guide} did not match expected stats: {expectedGuide}"
expectedTpsStats = log_reader.stats(41, 41, 41, 0.0, 0, 2)
assert expectedTpsStats == stats , f"Error: Stats calculated: {stats} did not match expected stats: {expectedTpsStats}"

#ensure that scraping of trxDataLog is compatible with 2.0
trxDict = {}
log_reader.scrapeBlockTrxDataLog(trxDict=trxDict, path=Path("tests")/"performance_tests"/"block_trx_data_log_2_0_14.txt", nodeosVersion="v2")
expectedDict = {}
expectedDict["41c6dca250f9b74d9fa6a8177a9c8390cb1d01b2123d6f88354f571f0053df72"] = log_reader.trxData(blockNum='112',cpuUsageUs='1253',netUsageUs='19')
expectedDict["fa17f9033589bb8757be009af46d465f0d903e26b7d198ea0fb6a3cbed93c2e6"] = log_reader.trxData(blockNum='112',cpuUsageUs='1263',netUsageUs='19')
assert trxDict == expectedDict, f"Scraped transaction dictionary: {trxDict} did not match expected dictionary : {expectedDict}"

testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)
