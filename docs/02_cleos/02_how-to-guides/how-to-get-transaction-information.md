## Overview

This how-to guide provides instructions on how to retrieve infomation of an EOSIO transaction using a transaction ID.

The example in this how-to retrieves transaction information associated with the creation of the account **bob**. 

## Before you begin

Make sure you meet the following requirements:
* Install the currently supported version of `cleos`.
[[info | Note]]
| `cleos` is bundled with the EOSIO software. [Installing EOSIO](../../00_install/index.md) will also install `cleos`.
* Understand how transactions work in an EOSIO blockchain. For more information on transactions, see the [Transactions Protocol](https://developers.eos.io/welcome/latest/protocol-guides/transactions_protocol) section.

## Command Reference

See the following reference guide for `cleos` command line usage and related options:
* [`cleos get transaction`](../03_command-reference/get/transaction.md) command and its parameters

## Procedure

The following step shows how to retrieve transaction information associated with the creation of the account **bob**.

1. Retrieve transaction information by transaction ID:
```sh
cleos get transaction 870a6b6e3882061ff0f64016e1eedfdd9439e2499bf978c3fb29fcedadada9b1
```
* Where `870a6b6e38...dada9b1`= The transaction ID associated with the creation of account **bob**. 

**Example Output**

The `cleos` command returns detailed information of the transaction:

```json
{
  "id": "870a6b6e3882061ff0f64016e1eedfdd9439e2499bf978c3fb29fcedadada9b1",
  "trx": {
    "receipt": {
      "status": "executed",
      "cpu_usage_us": 3493,
      "net_usage_words": 25,
      "trx": [
        1,{
          "signatures": [
            "SIG_K1_KYEvKx3nC81H6brKXHNjhrw32rNtTC2dP6nYFjZvj1N3k7KSU4CXBTJyiXd38ANu2ZPTUf66qUghUp5Jarkhiqdx3D8pwf"
          ],
          "compression": "none",
          "packed_context_free_data": "",
          "packed_trx": "0c252e60fe001fe2acca00000000010000000000ea305500409e9a2264b89a010000000000ea305500000000a8ed3232660000000000ea30550000000000000e3d01000000010002dfcee032f2e84bfc8ecc5c10fffb870ec1c690c1f3fdae3d8b7d65690b6455560100000001000000010002dfcee032f2e84bfc8ecc5c10fffb870ec1c690c1f3fdae3d8b7d65690b6455560100000000"
        }
      ]
    },
    "trx": {
      "expiration": "2021-02-18T08:27:56",
      "ref_block_num": 254,
      "ref_block_prefix": 3400327711,
      "max_net_usage_words": 0,
      "max_cpu_usage_ms": 0,
      "delay_sec": 0,
      "context_free_actions": [],
      "actions": [{
          "account": "eosio",
          "name": "newaccount",
          "authorization": [{
              "actor": "eosio",
              "permission": "active"
            }
          ],
          "data": {
            "creator": "eosio",
            "name": "bob",
            "owner": {
              "threshold": 1,
              "keys": [{
                  "key": "EOS6b4ENeXffuKGmsk3xCk3kbFM5M8dcANrUa9Mj9RzKLNhPKhzyj",
                  "weight": 1
                }
              ],
              "accounts": [],
              "waits": []
            },
            "active": {
              "threshold": 1,
              "keys": [{
                  "key": "EOS6b4ENeXffuKGmsk3xCk3kbFM5M8dcANrUa9Mj9RzKLNhPKhzyj",
                  "weight": 1
                }
              ],
              "accounts": [],
              "waits": []
            }
          },
          "hex_data": "0000000000ea30550000000000000e3d01000000010002dfcee032f2e84bfc8ecc5c10fffb870ec1c690c1f3fdae3d8b7d65690b6455560100000001000000010002dfcee032f2e84bfc8ecc5c10fffb870ec1c690c1f3fdae3d8b7d65690b64555601000000"
        }
      ],
      "transaction_extensions": [],
      "signatures": [
        "SIG_K1_KYEvKx3nC81H6brKXHNjhrw32rNtTC2dP6nYFjZvj1N3k7KSU4CXBTJyiXd38ANu2ZPTUf66qUghUp5Jarkhiqdx3D8pwf"
      ],
      "context_free_data": []
    }
  },
  "block_time": "2021-02-18T08:27:27.000",
  "block_num": 256,
  "last_irreversible_block": 305,
  "traces": [{
      "action_ordinal": 1,
      "creator_action_ordinal": 0,
      "closest_unnotified_ancestor_action_ordinal": 0,
      "receipt": {
        "receiver": "eosio",
        "act_digest": "2640ce4d4a789393dec3b7938cea2f78c5669498d0d22adeab9204c489c2cfd6",
        "global_sequence": 256,
        "recv_sequence": 256,
        "auth_sequence": [[
            "eosio",
            256
          ]
        ],
        "code_sequence": 0,
        "abi_sequence": 0
      },
      "receiver": "eosio",
      "act": {
        "account": "eosio",
        "name": "newaccount",
        "authorization": [{
            "actor": "eosio",
            "permission": "active"
          }
        ],
        "data": {
          "creator": "eosio",
          "name": "bob",
          "owner": {
            "threshold": 1,
            "keys": [{
                "key": "EOS6b4ENeXffuKGmsk3xCk3kbFM5M8dcANrUa9Mj9RzKLNhPKhzyj",
                "weight": 1
              }
            ],
            "accounts": [],
            "waits": []
          },
          "active": {
            "threshold": 1,
            "keys": [{
                "key": "EOS6b4ENeXffuKGmsk3xCk3kbFM5M8dcANrUa9Mj9RzKLNhPKhzyj",
                "weight": 1
              }
            ],
            "accounts": [],
            "waits": []
          }
        },
        "hex_data": "0000000000ea30550000000000000e3d01000000010002dfcee032f2e84bfc8ecc5c10fffb870ec1c690c1f3fdae3d8b7d65690b6455560100000001000000010002dfcee032f2e84bfc8ecc5c10fffb870ec1c690c1f3fdae3d8b7d65690b64555601000000"
      },
      "context_free": false,
      "elapsed": 1565,
      "console": "",
      "trx_id": "870a6b6e3882061ff0f64016e1eedfdd9439e2499bf978c3fb29fcedadada9b1",
      "block_num": 256,
      "block_time": "2021-02-18T08:27:27.000",
      "producer_block_id": null,
      "account_ram_deltas": [{
          "account": "bob",
          "delta": 2724
        }
      ],
      "except": null,
      "error_code": null
    }
  ]
}
```

[[info]]
| Be aware that you need to connect to a `nodeos` instance that enables both the [history plugin](../../01_nodeos/03_plugins/history_plugin/index.md) and [history API plugin](../../01_nodeos/03_plugins/history_api_plugin/index.md) to query transaction information.

## Summary

By following these instructions, you are able to retrieve transaction information using a transaction ID. 

## Trobleshooting

If the [history plugin](../../01_nodeos/03_plugins/history_plugin/index.md) and [history API plugin](../../01_nodeos/03_plugins/history_api_plugin/index.md) are not enabled in the `nodeos` **config.ini file**, the `cleos get transaction id` command will result in an error as shown below:

```sh
cleos get transaction 509eee3aa8988d533a336fec7a4c8b067ae3205cd97e2d27b3e9a2da61ef460c
```
```console
Error 3110003: Missing History API Plugin
Ensure that you have eosio::history_api_plugin added to your node's configuration!
Error Details:
History API plugin is not enabled
```

To troubleshoot this error, enable the [history plugin](../../01_nodeos/03_plugins/history_plugin/index.md) and [history API plugin](../../01_nodeos/03_plugins/history_api_plugin/index.md), then run the command again. 
