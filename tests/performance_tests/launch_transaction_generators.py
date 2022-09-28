#!/usr/bin/env python3

import os
import sys
import math
import argparse
import subprocess

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from TestHarness import Utils

Print = Utils.Print

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("chain_id", type=str, help="Chain ID")
parser.add_argument("last_irreversible_block_id", type=str, help="Last irreversible block ID")
parser.add_argument("handler_account", type=str, help="Cluster handler account name")
parser.add_argument("account_1_name", type=str, help="First account name")
parser.add_argument("account_2_name", type=str, help="Second account name")
parser.add_argument("account_1_priv_key", type=str, help="First account private key")
parser.add_argument("account_2_priv_key", type=str, help="Second account private key")
parser.add_argument("trx_gen_duration", type=str, help="How long to run transaction generators")
parser.add_argument("target_tps", type=int, help="Goal transactions per second")
parser.add_argument("tps_limit_per_generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
args = parser.parse_args()

targetTps = args.target_tps
numGenerators = math.ceil(targetTps / args.tps_limit_per_generator)
tpsPerGenerator = math.floor(targetTps / numGenerators)
modTps = targetTps % numGenerators
cleanlyDivisible = modTps == 0
incrementPoint = numGenerators + 1 - modTps
subprocess_ret_codes = []
for num in range(1, numGenerators + 1):
    if not cleanlyDivisible and num == incrementPoint:
        tpsPerGenerator = tpsPerGenerator + 1
    if Utils.Debug: Print(
       f'Running trx_generator: ./tests/trx_generator/trx_generator  '
       f'--chain-id {args.chain_id} '
       f'--last-irreversible-block-id {args.last_irreversible_block_id} '
       f'--handler-account {args.handler_account} '
       f'--accounts {args.account_1_name},{args.account_2_name} '
       f'--priv-keys {args.account_1_priv_key},{args.account_2_priv_key} '
       f'--trx-gen-duration {args.trx_gen_duration} '
       f'--target-tps {tpsPerGenerator}'
    )
    subprocess_ret_codes.append(
       subprocess.Popen([
           './tests/trx_generator/trx_generator',
           '--chain-id', f'{args.chain_id}',
           '--last-irreversible-block-id', f'{args.last_irreversible_block_id}',
           '--handler-account', f'{args.handler_account}',
           '--accounts', f'{args.account_1_name},{args.account_2_name}',
           '--priv-keys', f'{args.account_1_priv_key},{args.account_2_priv_key}',
           '--trx-gen-duration', f'{args.trx_gen_duration}',
           '--target-tps', f'{tpsPerGenerator}'
       ])
    )
exit_codes = [ret_code.wait() for ret_code in subprocess_ret_codes]
