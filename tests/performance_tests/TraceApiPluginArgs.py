#!/usr/bin/env python3

import dataclasses
import re

from dataclasses import dataclass

@dataclass
class TraceApiPluginArgs:
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

    def supportedNodeosArgs(self) -> list:
        args = []
        for field in dataclasses.fields(self):
            match = re.search("\w*NodeosArg", field.name)
            if match is not None:
                args.append(getattr(self, field.name))
        return args

    def __str__(self) -> str:
        args = [] 
        for field in dataclasses.fields(self):
            match = re.search("[^_]", field.name[0])
            if match is not None:
                default = getattr(self, f"_{field.name}NodeosDefault")
                current = getattr(self, field.name)
                if current is not None and current != default:
                    if type(current) is bool:
                        args.append(f"{getattr(self, f'_{field.name}NodeosArg')}")
                    else:
                        args.append(f"{getattr(self, f'_{field.name}NodeosArg')} {getattr(self, field.name)}")

        return "--plugin " + self._pluginNamespace + "::" + self._pluginName + " " + " ".join(args) if len(args) > 0 else ""

def main():
    pluginArgs = TraceApiPluginArgs()
    print(pluginArgs.supportedNodeosArgs())
    exit(0)

if __name__ == '__main__':
    main()
