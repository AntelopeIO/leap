#!/usr/bin/env python3
 
import argparse
import log_reader

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
nodeosLogPath=args.log_path
blockDataLogDirPath = args.block_data_logs_dir
trxGenLogDirPath = args.trx_data_logs_dir
data = log_reader.chainData()
data.startBlock = args.start_block
data.ceaseBlock = args.cease_block
blockDataPath = f"{blockDataLogDirPath}/blockData.txt"
blockTrxDataPath = f"{blockDataLogDirPath}/blockTrxData.txt"

report = log_reader.calcAndReport(data, args.target_tps, args.test_duration_sec, nodeosLogPath, trxGenLogDirPath, blockTrxDataPath, blockDataPath, args.num_blocks_to_prune, dict(item.split("=") for item in f"{args}"[10:-1].split(", ")), True)

print(data)
data.printBlockData()

print("Report:")
print(report)

if args.save_json:
    log_reader.exportReportAsJSON(report, args.json_path)
