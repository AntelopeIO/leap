#!/usr/bin/env python3
 
import argparse
import log_reader
import glob

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
parser.add_argument("--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=30)
parser.add_argument("--log-path", type=str, help="Path to nodeos log to scrape")
parser.add_argument("--block-data-logs-dir", type=str, help="Path to block data logs directory (contains blockData.txt and blockTrxData.txt) to scrape")
parser.add_argument("--trx-data-logs-dir", type=str, help="Path to trx data logs dir to scrape")
parser.add_argument("--start-block", type=int, help="First significant block number in the log", default=2)
parser.add_argument("--cease-block", type=int, help="Last significant block number in the log")
parser.add_argument("--num-blocks-to-prune", type=int, default=2, help="The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end of the range of blocks of interest for evaluation.")
parser.add_argument("--save-json", type=bool, help="Whether to save json output of stats", default=False)
parser.add_argument("--json-path", type=str, help="Path to save json output", default="data.json")
args = parser.parse_args()
logPath=args.log_path
blockDataLogDirPath = args.block_data_logs_dir
trxGenLogDirPath = args.trx_data_logs_dir
data = log_reader.chainData()
data.startBlock = args.start_block
data.ceaseBlock = args.cease_block
blockDataPath = f"{blockDataLogDirPath}/blockData.txt"
blockTrxDataPath = f"{blockDataLogDirPath}/blockTrxData.txt"

log_reader.scrapeLog(data, logPath)
print(data)
data.printBlockData()

trxSent = {}
filesScraped = []
for fileName in glob.glob(f"{trxGenLogDirPath}/trx_data_output_*.txt"):
    filesScraped.append(fileName)
    log_reader.scrapeTrxGenLog(trxSent, fileName)

print("Transaction Log Files Scraped:")
print(filesScraped)

trxDict = {}
log_reader.scrapeBlockTrxDataLog(trxDict, blockTrxDataPath)

blockDict = {}
log_reader.scrapeBlockDataLog(blockDict, blockDataPath)

notFound = []
for sentTrxId in trxSent.keys():
    if sentTrxId in trxDict.keys():
        trxDict[sentTrxId].sentTimestamp = trxSent[sentTrxId]
    else:
        notFound.append(sentTrxId)

if len(notFound) > 0:
    print(f"Transactions logged as sent but NOT FOUND in block!! lost {len(notFound)} out of {len(trxSent)}")

guide = log_reader.calcChainGuide(data, args.num_blocks_to_prune)
trxLatencyStats = log_reader.calcTrxLatencyStats(trxDict, blockDict)
tpsStats = log_reader.scoreTransfersPerSecond(data, guide)
blkSizeStats = log_reader.calcBlockSizeStats(data, guide)

print(f"Blocks Guide: {guide}\nTPS: {tpsStats}\nBlock Size: {blkSizeStats}\nTrx Latency: {trxLatencyStats}")

report = log_reader.createJSONReport(guide, tpsStats, blkSizeStats, trxLatencyStats, args, True)
print("Report:")
print(report)

if args.save_json:
    log_reader.exportAsJSON(report, args)