#!/usr/bin/env python3

from dataclasses import dataclass
from BasePluginArgs import BasePluginArgs

@dataclass
class TraceApiPluginArgs(BasePluginArgs):
    _pluginNamespace: str="eosio"
    _pluginName: str="trace_api_plugin"
    traceDir: str=None
    _traceDirNodeosDefault: str='"traces"'
    _traceDirNodeosArg: str="--trace-dir"
    traceSliceStride: int=None
    _traceSliceStrideNodeosDefault: int=10000
    _traceSliceStrideNodeosArg: str="--trace-slice-stride"
    traceMinimumIrreversibleHistoryBlocks: int=None
    _traceMinimumIrreversibleHistoryBlocksNodeosDefault: int=-1
    _traceMinimumIrreversibleHistoryBlocksNodeosArg: str="--trace-minimum-irreversible-history-blocks"
    traceMinimumUncompressedIrreversibleHistoryBlocks: int=None
    _traceMinimumUncompressedIrreversibleHistoryBlocksNodeosDefault: int=-1
    _traceMinimumUncompressedIrreversibleHistoryBlocksNodeosArg: str="--trace-minimum-uncompressed-irreversible-history-blocks"
    traceRpcAbi: str=None
    _traceRpcAbiNodeosDefault: str=None
    _traceRpcAbiNodeosArg: str="--trace-rpc-abi"
    traceNoAbis: bool=None
    _traceNoAbisNodeosDefault: bool=False
    _traceNoAbisNodeosArg: str="--trace-no-abis"

def main():
    pluginArgs = TraceApiPluginArgs()
    print(pluginArgs.supportedNodeosArgs())
    exit(0)

if __name__ == '__main__':
    main()
