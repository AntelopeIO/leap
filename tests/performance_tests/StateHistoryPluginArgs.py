#!/usr/bin/env python3

import dataclasses
import re

from dataclasses import dataclass

@dataclass
class StateHistoryPluginArgs:
    _pluginNamespace: str="eosio"
    _pluginName: str="state_history_plugin"
    stateHistoryDir: str=None
    _stateHistoryDirNodeosDefault: str='"state-history"'
    _stateHistoryDirNodeosArg: str="--state-history-dir"
    traceHistory: bool=None
    _traceHistoryNodeosDefault: bool=False
    _traceHistoryNodeosArg: str="--trace-history"
    chainStateHistory: bool=None
    _chainStateHistoryNodeosDefault: bool=False
    _chainStateHistoryNodeosArg: str="--chain-state-history"
    stateHistoryEndpoint: str=None
    _stateHistoryEndpointNodeosDefault: str="127.0.0.1:8080"
    _stateHistoryEndpointNodeosArg: str="--state-history-endpoint"
    stateHistoryUnixSocketPath: str=None
    _stateHistoryUnixSocketPathNodeosDefault: str=None
    _stateHistoryUnixSocketPathNodeosArg: str="--state-history-unix-socket-path"
    traceHistoryDebugMode: bool=None
    _traceHistoryDebugModeNodeosDefault: bool=False
    _traceHistoryDebugModeNodeosArg: str="--trace-history-debug-mode"
    stateHistoryLogRetainBlocks: int=None
    _stateHistoryLogRetainBlocksNodeosDefault: int=None
    _stateHistoryLogRetainBlocksNodeosArg: str="--state-history-log-retain-blocks"
    deleteStateHistory: bool=None
    _deleteStateHistoryNodeosDefault: bool=False
    _deleteStateHistoryNodeosArg: str="--delete-state-history"

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
    pluginArgs = StateHistoryPluginArgs()
    print(pluginArgs.supportedNodeosArgs())
    exit(0)

if __name__ == '__main__':
    main()
