## Description

Gets current blockchain state and, if available, transaction information given the transaction id.

For query to work, the transaction finality status feature must be enabled by configuring
the chain plugin with the config option "--transaction-finality-status-max-storage-size-gb \<size\>"
in nodeos.

## Position Parameters

- `id` _TEXT_ - The transaction ID of the transaction to retrieve status about (required)

## Options
- `-h` - --help                   Print this help message and exit
## Example


```sh
cleos get transaction-status 6438df82216dfaf46978f703fb818b49110dbfc5d9b521b5d08c342277438b29
```

This command simply returns the current chain status and transaction status information (if available).

```json
{
    "state": "IN_BLOCK",
    "block_number": 90,
    "block_id": "0000005accfd59ba80a05380f60d51687406337b2aedd28b7daa33fdb8c16b5a",
    "block_timestamp": "2022-04-27T16:11:26.500",
    "expiration": "2022-04-27T16:11:56.000",
    "head_number": 186,
    "head_id": "000000bab27da51f76f483bb629b532510c22e2eb1acc632f5b37b421adecf63",
    "head_timestamp": "2022-04-27T16:12:14.500",
    "irreversible_number": 25,
    "irreversible_id": "0000001922118216bc16bf2c60d950598d80af3beca820eab751f7beecdb29e4",
    "irreversible_timestamp": "2022-04-27T16:10:54.000",
    "earliest_tracked_block_id": "000000129cee97f3e27312f0184d52d006a470f0e620553dfb4c5b4f3c856ab2",
    "earliest_tracked_block_number": 18
}
```
