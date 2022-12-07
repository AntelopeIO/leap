#!/usr/bin/env python3

from dataclasses import dataclass
from .BasePluginArgs import BasePluginArgs

@dataclass
class SignatureProviderPluginArgs(BasePluginArgs):
    _pluginNamespace: str="eosio"
    _pluginName: str="signature_provider_plugin"
    keosdProviderTimeout: int=None
    _keosdProviderTimeoutNodeosDefault: int=5
    _keosdProviderTimeoutNodeosArg: str="--keosd-provider-timeout"

def main():
    pluginArgs = SignatureProviderPluginArgs()
    print(pluginArgs.supportedNodeosArgs())
    exit(0)

if __name__ == '__main__':
    main()
