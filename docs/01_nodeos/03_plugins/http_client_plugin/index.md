## Description

The `http_client_plugin`  is an internal utility plugin, providing the `producer_plugin` the ability to use securely an external `keosd` instance as its block signer. It can only be used when the `producer_plugin` is configured to produce blocks.

## Usage

```console
# config.ini
plugin = eosio::http_client_plugin
```
```sh
# command-line
nodeos ... --plugin eosio::http_client_plugin 
```

## Dependencies

* [`producer_plugin`](../producer_plugin/index.md)
