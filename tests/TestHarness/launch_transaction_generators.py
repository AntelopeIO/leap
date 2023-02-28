#!/usr/bin/env python3

from dataclasses import dataclass
import os
import sys
import math
import argparse
import subprocess

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from .testUtils import Utils
Print = Utils.Print

class TpsTrxGensConfig:

    def __init__(self, targetTps: int, tpsLimitPerGenerator: int):
        self.targetTps: int = targetTps
        self.tpsLimitPerGenerator: int = tpsLimitPerGenerator

        self.numGenerators = math.ceil(self.targetTps / self.tpsLimitPerGenerator)
        self.initialTpsPerGenerator = math.floor(self.targetTps / self.numGenerators)
        self.modTps = self.targetTps % self.numGenerators
        self.cleanlyDivisible = self.modTps == 0
        self.incrementPoint = self.numGenerators + 1 - self.modTps

        self.targetTpsPerGenList = []
        curTps = self.initialTpsPerGenerator
        for num in range(1, self.numGenerators + 1):
            if not self.cleanlyDivisible and num == self.incrementPoint:
                curTps = curTps + 1
            self.targetTpsPerGenList.append(curTps)

class TransactionGeneratorsLauncher:

    def __init__(self, chainId: int, lastIrreversibleBlockId: int, handlerAcct: str, accts: str, privateKeys: str,
                 trxGenDurationSec: int, logDir: str, peerEndpoint: str, port: int, tpsTrxGensConfig: TpsTrxGensConfig):
        self.chainId = chainId
        self.lastIrreversibleBlockId = lastIrreversibleBlockId
        self.handlerAcct  = handlerAcct
        self.accts = accts
        self.privateKeys = privateKeys
        self.trxGenDurationSec  = trxGenDurationSec
        self.tpsTrxGensConfig = tpsTrxGensConfig
        self.logDir = logDir
        self.peerEndpoint = peerEndpoint
        self.port = port

    def launch(self, waitToComplete=True):
        self.subprocess_ret_codes = []
        for targetTps in self.tpsTrxGensConfig.targetTpsPerGenList:
            if Utils.Debug:
                Print(
                    f'Running trx_generator: ./tests/trx_generator/trx_generator  '
                    f'--chain-id {self.chainId} '
                    f'--last-irreversible-block-id {self.lastIrreversibleBlockId} '
                    f'--handler-account {self.handlerAcct} '
                    f'--accounts {self.accts} '
                    f'--priv-keys {self.privateKeys} '
                    f'--trx-gen-duration {self.trxGenDurationSec} '
                    f'--target-tps {targetTps} '
                    f'--log-dir {self.logDir} '
                    f'--peer-endpoint {self.peerEndpoint} '
                    f'--port {self.port}'
                )
            self.subprocess_ret_codes.append(
                subprocess.Popen([
                    './tests/trx_generator/trx_generator',
                    '--chain-id', f'{self.chainId}',
                    '--last-irreversible-block-id', f'{self.lastIrreversibleBlockId}',
                    '--handler-account', f'{self.handlerAcct}',
                    '--accounts', f'{self.accts}',
                    '--priv-keys', f'{self.privateKeys}',
                    '--trx-gen-duration', f'{self.trxGenDurationSec}',
                    '--target-tps', f'{targetTps}',
                    '--log-dir', f'{self.logDir}',
                    '--peer-endpoint', f'{self.peerEndpoint}',
                    '--port', f'{self.port}'
                ])
            )
        exitCodes=None
        if waitToComplete:
            exitCodes = [ret_code.wait() for ret_code in self.subprocess_ret_codes]
        return exitCodes

    def killAll(self):
        for ret_code in self.subprocess_ret_codes:
            ret_code.kill()
        for ret_code in self.subprocess_ret_codes:
            ret_code.wait()

def parseArgs():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('-?', action='help', default=argparse.SUPPRESS, help=argparse._('show this help message and exit'))
    parser.add_argument("chain_id", type=str, help="Chain ID")
    parser.add_argument("last_irreversible_block_id", type=str, help="Last irreversible block ID")
    parser.add_argument("handler_account", type=str, help="Cluster handler account name")
    parser.add_argument("accounts", type=str, help="Comma separated list of account names")
    parser.add_argument("priv_keys", type=str, help="Comma separated list of private keys")
    parser.add_argument("trx_gen_duration", type=str, help="How long to run transaction generators")
    parser.add_argument("target_tps", type=int, help="Goal transactions per second")
    parser.add_argument("tps_limit_per_generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    parser.add_argument("log_dir", type=str, help="Path to directory where trx logs should be written.")
    parser.add_argument("peer_endpoint", type=str, help="set the peer endpoint to send transactions to", default="127.0.0.1")
    parser.add_argument("port", type=int, help="set the peer endpoint port to send transactions to", default=9876)
    args = parser.parse_args()
    return args

def main():
    args = parseArgs()

    trxGenLauncher = TransactionGeneratorsLauncher(chainId=args.chain_id, lastIrreversibleBlockId=args.last_irreversible_block_id,
                                                   handlerAcct=args.handler_account, accts=args.accounts,
                                                   privateKeys=args.priv_keys, trxGenDurationSec=args.trx_gen_duration, logDir=args.log_dir,
                                                   peerEndpoint=args.peer_endpoint, port=args.port,
                                                   tpsTrxGensConfig=TpsTrxGensConfig(targetTps=args.target_tps, tpsLimitPerGenerator=args.tps_limit_per_generator))


    exit_codes = trxGenLauncher.launch()
    exit(exit_codes)

if __name__ == '__main__':
    main()
