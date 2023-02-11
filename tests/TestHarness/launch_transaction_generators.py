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
from pathlib import Path

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

    def __init__(self, chainId: int, lastIrreversibleBlockId: int, contractOwnerAccount: str, accts: str, privateKeys: str,
                 trxGenDurationSec: int, logDir: str, abiFile: Path, actionName: str, actionData, peerEndpoint: str, port: int, tpsTrxGensConfig: TpsTrxGensConfig,
                 ownerPrivateKey: str):
        self.chainId = chainId
        self.lastIrreversibleBlockId = lastIrreversibleBlockId
        self.contractOwnerAccount  = contractOwnerAccount
        self.accts = accts
        self.privateKeys = privateKeys
        self.trxGenDurationSec  = trxGenDurationSec
        self.tpsTrxGensConfig = tpsTrxGensConfig
        self.logDir = logDir
        self.abiFile = abiFile
        self.actionName = actionName
        self.actionData = actionData
        self.peerEndpoint = peerEndpoint
        self.port = port
        self.ownerPrivateKey=ownerPrivateKey

    def launch(self, waitToComplete=True):
        self.subprocess_ret_codes = []
        for targetTps in self.tpsTrxGensConfig.targetTpsPerGenList:
            if self.abiFile is not None and self.actionName is not None and self.actionData is not None:
                if Utils.Debug:
                    Print(
                        f'Running trx_generator: ./tests/trx_generator/trx_generator  '
                        f'--chain-id {self.chainId} '
                        f'--last-irreversible-block-id {self.lastIrreversibleBlockId} '
                        f'--contract-owner-account {self.contractOwnerAccount} '
                        f'--accounts {self.accts} '
                        f'--priv-keys {self.privateKeys} '
                        f'--trx-gen-duration {self.trxGenDurationSec} '
                        f'--target-tps {targetTps} '
                        f'--log-dir {self.logDir} '
                        f'--action-name {self.actionName} '
                        f'--action-data {self.actionData} '
                        f'--abi-file {self.abiFile} '
                        f'--peer-endpoint {self.peerEndpoint} '
                        f'--port {self.port} '
                        f'--owner-private-key {self.ownerPrivateKey}'
                    )
                self.subprocess_ret_codes.append(
                    subprocess.Popen([
                        './tests/trx_generator/trx_generator',
                        '--chain-id', f'{self.chainId}',
                        '--last-irreversible-block-id', f'{self.lastIrreversibleBlockId}',
                        '--contract-owner-account', f'{self.contractOwnerAccount}',
                        '--accounts', f'{self.accts}',
                        '--priv-keys', f'{self.privateKeys}',
                        '--trx-gen-duration', f'{self.trxGenDurationSec}',
                        '--target-tps', f'{targetTps}',
                        '--log-dir', f'{self.logDir}',
                        '--action-name', f'{self.actionName}',
                        '--action-data', f'{self.actionData}',
                        '--abi-file', f'{self.abiFile}',
                        '--peer-endpoint', f'{self.peerEndpoint}',
                        '--port', f'{self.port}',
                        '--owner-private-key', f'{self.ownerPrivateKey}'
                    ])
                )
            else:
                if Utils.Debug:
                    Print(
                        f'Running trx_generator: ./tests/trx_generator/trx_generator  '
                        f'--chain-id {self.chainId} '
                        f'--last-irreversible-block-id {self.lastIrreversibleBlockId} '
                        f'--contract-owner-account {self.contractOwnerAccount} '
                        f'--accounts {self.accts} '
                        f'--priv-keys {self.privateKeys} '
                        f'--trx-gen-duration {self.trxGenDurationSec} '
                        f'--target-tps {targetTps} '
                        f'--log-dir {self.logDir} '
                        f'--peer-endpoint {self.peerEndpoint} '
                        f'--port {self.port} '
                        f'--owner-private-key {self.ownerPrivateKey}'
                    )
                self.subprocess_ret_codes.append(
                    subprocess.Popen([
                        './tests/trx_generator/trx_generator',
                        '--chain-id', f'{self.chainId}',
                        '--last-irreversible-block-id', f'{self.lastIrreversibleBlockId}',
                        '--contract-owner-account', f'{self.contractOwnerAccount}',
                        '--accounts', f'{self.accts}',
                        '--priv-keys', f'{self.privateKeys}',
                        '--trx-gen-duration', f'{self.trxGenDurationSec}',
                        '--target-tps', f'{targetTps}',
                        '--log-dir', f'{self.logDir}',
                        '--peer-endpoint', f'{self.peerEndpoint}',
                        '--port', f'{self.port}',
                        '--owner-private-key', f'{self.ownerPrivateKey}'
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
    parser.add_argument("contract_owner_account", type=str, help="Cluster contract owner account name")
    parser.add_argument("accounts", type=str, help="Comma separated list of account names")
    parser.add_argument("priv_keys", type=str, help="Comma separated list of private keys")
    parser.add_argument("trx_gen_duration", type=str, help="How long to run transaction generators")
    parser.add_argument("target_tps", type=int, help="Goal transactions per second")
    parser.add_argument("tps_limit_per_generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    parser.add_argument("log_dir", type=str, help="Path to directory where trx logs should be written.")
    parser.add_argument("action_name", type=str, help="The action name applied to the provided action data input")
    parser.add_argument("action_data", type=str, help="The path to the json action data file or json action data description string to use")
    parser.add_argument("abi_file", type=str, help="The path to the contract abi file to use for the supplied transaction action data")
    parser.add_argument("peer_endpoint", type=str, help="set the peer endpoint to send transactions to", default="127.0.0.1")
    parser.add_argument("port", type=int, help="set the peer endpoint port to send transactions to", default=9876)
    parser.add_argument("owner_private_key", type=str, help="ownerPrivateKey of the contract owner")
    args = parser.parse_args()
    return args

def main():
    args = parseArgs()

    trxGenLauncher = TransactionGeneratorsLauncher(chainId=args.chain_id, lastIrreversibleBlockId=args.last_irreversible_block_id,
                                                   contractOwnerAccount=args.contract_owner_account, accts=args.accounts,
                                                   privateKeys=args.priv_keys, trxGenDurationSec=args.trx_gen_duration, logDir=args.log_dir,
                                                   abiFile=args.abi_file, actionName=args.action_name, actionData=args.action_data,
                                                   peerEndpoint=args.peer_endpoint, port=args.port,
                                                   tpsTrxGensConfig=TpsTrxGensConfig(targetTps=args.target_tps, tpsLimitPerGenerator=args.tps_limit_per_generator),
                                                   ownerPrivateKey=args.owner_private_key)


    exit_codes = trxGenLauncher.launch()
    exit(exit_codes)

if __name__ == '__main__':
    main()
