# Transaction Generator

The Transaction Generator is a program built to create and send transactions at a specified rate in order to generate load on a blockchain.  It is comprised of 3 main components: Transaction Generator, Transaction Provider, and Performance Monitor.

The `trx_generator.[hpp, cpp]` is currently specialized to be a `transfer_trx_generator` primarily focused on generating token transfer transactions.  The transactions are then provided to the network by the `trx_provider.[hpp, cpp]` which is currently aimed at the P2P network protocol in the `p2p_trx_provider`.  The third component, the `tps_performance_monitor`, allows the Transaction Generator to monitor its own performance and take action to notify and exit if it is unable to keep up with the requested transaction generation rate.

The Transaction Generator logs each transaction's id and sent timestamp at the moment the Transaction Provider sends the transaction.  Logs are written to the configured log directory and will follow the naming convention `trx_data_output_10744.txt` where `10744` is the transaction generator instance's process ID.

## Configuration Options
`./build/tests/trx_generator/trx_generator` can be configured using the following command line arguments:

<details open>
    <summary>Expand Argument List</summary>

* `--chain-id arg`                  set the chain id
* `--handler-account arg`           Account name of the handler account for
                                    the transfer actions
* `--accounts arg`                  comma-separated list of accounts that 
                                    will be used for transfers. Minimum 
                                    required accounts: 2.
* `--priv-keys arg`                 comma-separated list of private keys in
                                    same order of accounts list that will 
                                    be used to sign transactions. Minimum 
                                    required: 2.
* `--trx-expiration arg` (=3600)    transaction expiration time in seconds.
                                    Defaults to 3,600. Maximum allowed: 
                                    3,600
* `--trx-gen-duration arg` (=60)    Transaction generation duration 
                                    (seconds). Defaults to 60 seconds.
* `--target-tps arg` (=1)           Target transactions per second to 
                                    generate/send. Defaults to 1 
                                    transaction per second.
* `--last-irreversible-block-id arg`      Current last-irreversible-block-id (LIB
                                    ID) to use for transactions.
* `--monitor-spinup-time-us arg` (=1000000)
                                    Number of microseconds to wait before 
                                    monitoring TPS. Defaults to 1000000 
                                    (1s).
* `--monitor-max-lag-percent arg` (=5)    Max percentage off from expected 
                                    transactions sent before being in 
                                    violation. Defaults to 5.
* `--monitor-max-lag-duration-us arg` (=1000000)
                                    Max microseconds that transaction 
                                    generation can be in violation before 
                                    quitting. Defaults to 1000000 (1s).
* `--log-dir arg`                   set the logs directory
</details>