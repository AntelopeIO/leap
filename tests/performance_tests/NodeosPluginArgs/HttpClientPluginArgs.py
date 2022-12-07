#!/usr/bin/env python3

from dataclasses import dataclass
from BasePluginArgs import BasePluginArgs

@dataclass
class HttpClientPluginArgs(BasePluginArgs):
    _pluginNamespace: str="eosio"
    _pluginName: str="http_client_plugin"
    httpsClientRootCert: str=None
    _httpsClientRootCertNodeosDefault: str=None
    _httpsClientRootCertNodeosArg: str="--https-client-root-cert"
    httpsClientValidatePeers: int=None
    _httpsClientValidatePeersNodeosDefault: int=1
    _httpsClientValidatePeersNodeosArg: str="--https-client-validate-peers"

def main():
    pluginArgs = HttpClientPluginArgs()
    print(pluginArgs.supportedNodeosArgs())
    exit(0)

if __name__ == '__main__':
    main()
