## Description

Gets current blockchain state and, if available, transaction information given the transaction id

## Position Parameters

- `id` _TEXT_ - The string ID of the transaction to retrieve status about (required)

## Options
- `-h` - --help                   Print this help message and exit
## Example


```sh
cleos get transaction-status 6438df82216dfaf46978f703fb818b49110dbfc5d9b521b5d08c342277438b29
```

This command simply returns the current chain status and transaction status information (if available). 

```json
{
    "state": "UNKNOWN",
    "block_number": 0,
    "block_id": "0000000000000000000000000000000000000000000000000000000000000000",
    "block_timestamp": "2000-01-01T00:00:00.000",
    "expiration": "2000-01-01T00:00:00.000",
    "head_number": 0,
    "head_id": "0000000000000000000000000000000000000000000000000000000000000000",
    "head_timestamp": "2000-01-01T00:00:00.000",
    "irreversible_number": 0,
    "irreversible_id": "0000000000000000000000000000000000000000000000000000000000000000",
    "irreversible_timestamp": "2000-01-01T00:00:00.000",
    "last_tracked_block_id": "0000000000000000000000000000000000000000000000000000000000000000"
}
```
