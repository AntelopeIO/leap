## Overview

This how-to guide provides instructions on how to create a new slim Antelope blockchain account using the `cleos` CLI tool. You can use accounts to deploy smart contracts and perform other related blockchain operations. Create one or multiple accounts as part of your development environment setup.

The example in this how-to guide creates a new account named **ted**, authorized by the default system account **eosio**, using the `cleos` CLI tool.

## Before you Begin

Make sure you meet the following requirements:

* Install the currently supported version of `cleos`.
[[info | Note]]
| The cleos tool is bundled with the Antelope software. [Installing Antelope](../../00_install/index.md) will also install the cleos tool.
* Learn about [Antelope Accounts and Permissions](/protocol-guides/04_accounts_and_permissions.md)
* Learn about Asymmetric Cryptography - [public key](/glossary.md#public-key) and [private key](/glossary.md#private-key) pairs.
* Create public/private keypair for the `active` permissions of an account.

## Command Reference

See the following reference guide for `cleos` command line usage and related options:
* [`cleos create slimaccount`](../03_command-reference/create/slim_account.md) command and its parameters

## Procedure

The following step shows how to create a new account **ted** authorized by the default system account **eosio**.

1. Run the following command to create the new account **ted**:

```sh
cleos create slimaccount eosio ted EOS87TQktA5RVse2EguhztfQVEh6XXxBmgkU8b4Y5YnGvtYAoLGNN
```
**Where**:
* `eosio` = the system account that authorizes the creation of a new account
* `ted` = the name of the new account conforming to [account naming conventions](/protocol-guides/04_accounts_and_permissions.md#2-accounts)
* `EOS87TQ...AoLGNN` = the active public key or permission level for the new account (**required**)
[[info | Note]]
| To create a new account in the Antelope blockchain, an existing account, also referred to as a creator account, is required to authorize the creation of a new account. For a newly created Antelope blockchain, the default system account used to create a new account is **eosio**.

**Example Output**

```console
executed transaction: 80a54e6220ba90edfae2f35d1841f72660313337294685937ab4eedf422a26d8  152 bytes  139 us
#         eosio <= eosio::newslimacc            "0000000000ea305500000000000092ca01000000010003a887c1d7893cb40746c6f8fc6e0a964320d0316b85c6ebc8c905a...
warning: transaction executed locally, but may not be confirmed by the network yet         ] 
```

### Summary

By following these instructions, you are able to create a new slim Antelope account in your blockchain environment.
