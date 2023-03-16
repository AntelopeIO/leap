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
from re import split

Print = Utils.Print

class TpsTrxGensConfig:

    def __init__(self, targetTps: int, tpsLimitPerGenerator: int, connectionPairList: list):
        self.targetTps: int = targetTps
        self.tpsLimitPerGenerator: int = tpsLimitPerGenerator
        self.connectionPairList = connectionPairList
        self.numConnectionPairs = len(self.connectionPairList)
        self.numGenerators = math.ceil(self.targetTps / self.tpsLimitPerGenerator)
        self.numGenerators = math.ceil(self.numGenerators/self.numConnectionPairs) * self.numConnectionPairs
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

    def __init__(self, chainId: int, lastIrreversibleBlockId: int, contractOwnerAccount: str, accts: str, privateKeys: str, trxGenDurationSec: int, logDir: str,
                 abiFile: Path, actionsData, actionsAuths, tpsTrxGensConfig: TpsTrxGensConfig):
        self.chainId = chainId
        self.lastIrreversibleBlockId = lastIrreversibleBlockId
        self.contractOwnerAccount  = contractOwnerAccount
        self.accts = accts
        self.privateKeys = privateKeys
        self.trxGenDurationSec  = trxGenDurationSec
        self.tpsTrxGensConfig = tpsTrxGensConfig
        self.logDir = logDir
        self.abiFile = abiFile
        self.actionsData = actionsData
        self.actionsAuths = actionsAuths

    def launch(self, waitToComplete=True):
        self.subprocess_ret_codes = []
        connectionPairIter = 0
        for id, targetTps in enumerate(self.tpsTrxGensConfig.targetTpsPerGenList):
            connectionPair = self.tpsTrxGensConfig.connectionPairList[connectionPairIter].rsplit(":")
            if self.abiFile is not None and self.actionsData is not None and self.actionsAuths is not None:
                if Utils.Debug:
                    Print(
                        f'Running trx_generator: ./tests/trx_generator/trx_generator  '
                        f'--generator-id {id} '
                        f'--chain-id {self.chainId} '
                        f'--last-irreversible-block-id {self.lastIrreversibleBlockId} '
                        f'--contract-owner-account {self.contractOwnerAccount} '
                        f'--accounts {self.accts} '
                        f'--priv-keys {self.privateKeys} '
                        f'--trx-gen-duration {self.trxGenDurationSec} '
                        f'--target-tps {targetTps} '
                        f'--log-dir {self.logDir} '
                        f'--abi-file {self.abiFile} '
                        f'--actions-data {self.actionsData} '
                        f'--actions-auths {self.actionsAuths} '
                        f'--peer-endpoint {connectionPair[0]} '
                        f'--port {connectionPair[1]}'
                    )
                self.subprocess_ret_codes.append(
                    subprocess.Popen([
                        './tests/trx_generator/trx_generator',
                        '--generator-id', f'{id}',
                        '--chain-id', f'{self.chainId}',
                        '--last-irreversible-block-id', f'{self.lastIrreversibleBlockId}',
                        '--contract-owner-account', f'{self.contractOwnerAccount}',
                        '--accounts', f'{self.accts}',
                        '--priv-keys', f'{self.privateKeys}',
                        '--trx-gen-duration', f'{self.trxGenDurationSec}',
                        '--target-tps', f'{targetTps}',
                        '--log-dir', f'{self.logDir}',
                        '--abi-file', f'{self.abiFile}',
                        '--actions-data', f'{self.actionsData}',
                        '--actions-auths', f'{self.actionsAuths}',
                        '--peer-endpoint', f'{connectionPair[0]}',
                        '--port', f'{connectionPair[1]}'
                    ])
                )
            else:
                if Utils.Debug:
                    Print(
                        f'Running trx_generator: ./tests/trx_generator/trx_generator  '
                        f'--generator-id {id} '
                        f'--chain-id {self.chainId} '
                        f'--last-irreversible-block-id {self.lastIrreversibleBlockId} '
                        f'--contract-owner-account {self.contractOwnerAccount} '
                        f'--accounts {self.accts} '
                        f'--priv-keys {self.privateKeys} '
                        f'--trx-gen-duration {self.trxGenDurationSec} '
                        f'--target-tps {targetTps} '
                        f'--log-dir {self.logDir} '
                        f'--peer-endpoint {connectionPair[0]} '
                        f'--port {connectionPair[1]}'
                    )
                self.subprocess_ret_codes.append(
                    subprocess.Popen([
                        './tests/trx_generator/trx_generator',
                        '--generator-id', f'{id}',
                        '--chain-id', f'{self.chainId}',
                        '--last-irreversible-block-id', f'{self.lastIrreversibleBlockId}',
                        '--contract-owner-account', f'{self.contractOwnerAccount}',
                        '--accounts', f'{self.accts}',
                        '--priv-keys', f'{self.privateKeys}',
                        '--trx-gen-duration', f'{self.trxGenDurationSec}',
                        '--target-tps', f'{targetTps}',
                        '--log-dir', f'{self.logDir}',
                        '--peer-endpoint', f'{connectionPair[0]}',
                        '--port', f'{connectionPair[1]}'
                    ])
                )
            connectionPairIter = (connectionPairIter + 1) % len(self.tpsTrxGensConfig.connectionPairList)
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
    parser.add_argument('-?', '--help', action='help', default=argparse.SUPPRESS, help=argparse._('show this help message and exit'))
    parser.add_argument("chain_id", type=str, help="Chain ID")
    parser.add_argument("last_irreversible_block_id", type=str, help="Last irreversible block ID")
    parser.add_argument("contract_owner_account", type=str, help="Cluster contract owner account name")
    parser.add_argument("accounts", type=str, help="Comma separated list of account names")
    parser.add_argument("priv_keys", type=str, help="Comma separated list of private keys")
    parser.add_argument("trx_gen_duration", type=str, help="How long to run transaction generators")
    parser.add_argument("target_tps", type=int, help="Goal transactions per second")
    parser.add_argument("tps_limit_per_generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
    parser.add_argument("log_dir", type=str, help="Path to directory where trx logs should be written.")
    parser.add_argument("abi_file", type=str, help="The path to the contract abi file to use for the supplied transaction action data")
    parser.add_argument("actions_data", type=str, help="The json actions data file or json actions data description string to use")
    parser.add_argument("actions_auths", type=str, help="The json actions auth file or json actions auths description string to use, containting authAcctName to activePrivateKey pairs.")
    parser.add_argument("connection_pair_list", type=str, help="Comma separated list of endpoint:port combinations to send transactions to", default="localhost:9876")
    args = parser.parse_args()
    return args

def main():
    args = parseArgs()
    connectionPairList = args.connection_pair_list.rsplit(", ")

    trxGenLauncher = TransactionGeneratorsLauncher(chainId=args.chain_id, lastIrreversibleBlockId=args.last_irreversible_block_id,
                                                   contractOwnerAccount=args.contract_owner_account, accts=args.accounts,
                                                   privateKeys=args.priv_keys, trxGenDurationSec=args.trx_gen_duration, logDir=args.log_dir,
                                                   abiFile=args.abi_file, actionsData=args.actions_data, actionsAuths=args.actions_auths,
                                                   tpsTrxGensConfig=TpsTrxGensConfig(targetTps=args.target_tps, tpsLimitPerGenerator=args.tps_limit_per_generator, connectionPairList=connectionPairList))


    exit_codes = trxGenLauncher.launch()
    exit(exit_codes)

if __name__ == '__main__':
    main()
