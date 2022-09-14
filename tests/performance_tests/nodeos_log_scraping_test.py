#!/usr/bin/env python3
# Unit test to ensure that nodeos log scraping behavior from log_reader.py does not change
# Also ensures that all versions of nodeos logs can be handled
import log_reader

testSuccessful = False

# Test log scraping for 3.2 log format
dataCurrent = log_reader.chainData()
dataCurrent.startBlock = None
dataCurrent.ceaseBlock = None
log_reader.scrapeLog(dataCurrent, "tests/performance_tests/nodeos_log_3_2.txt.gz")

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


# Test log scraping from a 2.0.14 log format
dataOld = log_reader.chainData()
dataOld.startBlock = None
dataOld.ceaseBlock = None
log_reader.scrapeLog(dataOld, "tests/performance_tests/nodeos_log_2_0_14.txt.gz")
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

testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)
