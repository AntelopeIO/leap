#!/usr/bin/env python3
 
import argparse
import log_reader

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("log_path", type=str, help="Path to nodeos log to scrape")
parser.add_argument("--start_block", type=int, help="First significant block number in the log", default=2)
parser.add_argument("--cease_block", type=int, help="Last significant block number in the log")
args = parser.parse_args()
logPath=args.log_path
data = log_reader.chainData()
data.startBlock = args.start_block
data.ceaseBlock = args.cease_block
log_reader.scrapeLog(data, logPath)
print(data)
data.printBlockData()
