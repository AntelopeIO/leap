#!/usr/bin/env python3

import dataclasses
import re

from dataclasses import dataclass

@dataclass
class ResourceMonitorPluginArgs:
    _pluginNamespace: str = "eosio"
    _pluginName: str = "resource_monitor_plugin"
    resourceMonitorIntervalSeconds: int=None
    _resourceMonitorIntervalSecondsNodeosDefault: int=2
    _resourceMonitorIntervalSecondsNodeosArg: str="--resource-monitor-interval-seconds"
    resourceMonitorSpaceThreshold: int=None
    _resourceMonitorSpaceThresholdNodeosDefault: int=90
    _resourceMonitorSpaceThresholdNodeosArg: str="--resource-monitor-space-threshold"
    resourceMonitorNotShutdownOnThresholdExceeded: bool=None
    _resourceMonitorNotShutdownOnThresholdExceededNodeosDefault: bool=False
    _resourceMonitorNotShutdownOnThresholdExceededNodeosArg: str="--resource-monitor-not-shutdown-on-threshold-exceeded"
    resourceMonitorWarningInterval: int=None
    _resourceMonitorWarningIntervalNodeosDefault: int=30
    _resourceMonitorWarningIntervalNodeosArg: str="--resource-monitor-warning-interval"

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
    pluginArgs = ResourceMonitorPluginArgs()
    print(pluginArgs.supportedNodeosArgs())
    exit(0)

if __name__ == '__main__':
    main()
