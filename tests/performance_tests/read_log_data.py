#!/usr/bin/env python3
 
import argparse
import log_reader

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("log_path", type=str, help="Path to nodeos log to scrape")
parser.add_argument("--start_block", type=int, help="First significant block number in the log", default=2)
parser.add_argument("--cease_block", type=int, help="Last significant block number in the log")
parser.add_argument("--num-blocks-to-prune", type=int, default=2, help="The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end of the range of blocks of interest for evaluation.")
parser.add_argument("--save-json", type=str, default="data.json", help="Path to save json output")
args = parser.parse_args()
logPath=args.log_path
data = log_reader.chainData()
data.startBlock = args.start_block
data.ceaseBlock = args.cease_block
log_reader.scrapeLog(data, logPath)
print(data)
data.printBlockData()

stats = log_reader.scoreTransfersPerSecond(data, args.num_blocks_to_prune)
print(f"TPS: {stats}")
if args.save_json:
    log_reader.exportAsJSON(stats, args)
