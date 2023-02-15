# Performance Harness Tests

The Performance Harness is configured and run through the main `performance_test.py` script.  The script's main goal is to measure current peak performance metrics through iteratively tuning and running basic performance tests. The current basic test works to determine the maximum throughput of Token Transfers the system can sustain.  It does this by conducting a binary search of possible Token Transfers Per Second (TPS) configurations, testing each configuration in a short duration test and scoring its result. The search algorithm iteratively configures and runs `performance_test_basic.py` tests and analyzes the output to determine a success metric used to continue the search.  When the search completes, a max TPS throughput value is reported (along with other performance metrics from that run).  The script then proceeds to conduct an additional search with longer duration test runs within a narrowed TPS configuration range to determine the sustainable max TPS. Finally it produces a report on the entire performance run, summarizing each individual test scenario, results, and full report details on the tests when maximum TPS was achieved ([Performance Test Report](#performance-test-report))

The `performance_test_basic.py` support script performs a single basic performance test that targets a configurable TPS target and, if successful, reports statistics on performance metrics measured during the test.  It configures and launches a blockchain test environment, creates wallets and accounts for testing, and configures and launches transaction generators for creating specific transaction load in the ecosystem.  Finally it analyzes the performance of the system under the configuration through log analysis and chain queries and produces a [Performance Test Basic Report](#performance-test-basic-report).

The `launch_generators.py` support script provides a means to easily calculate and spawn the number of transaction generator instances to generate a given target TPS, distributing generation load between the instances in a fair manner such that the aggregate load meets the requested test load.

The `log_reader.py` support script is used primarily to analyze `nodeos` log files to glean information about generated blocks and transactions within those blocks after a test has concluded.  This information is used to produce the performance test report. In similar fashion, `read_log_data.py` allows for recreating a report from the configuration and log files without needing to rerun the test.

## Prerequisites

Please refer to [Leap: Build and Install from Source](https://github.com/AntelopeIO/leap/#build-and-install-from-source) for a full list of prerequisites.

## Steps

1. Build Leap. For complete instructions on building from source please refer to [Leap: Build and Install from Source](https://github.com/AntelopeIO/leap/#build-and-install-from-source) For older compatible nodeos versions, such as 2.X, the following binaries need to be replaced with the older version: `build/programs/nodeos/nodeos`, `build/programs/cleos/cleos`, `bin/nodeos`, and `bin/cleos`.
2. Run Performance Tests
    1. Full Performance Harness Test Run (Standard):
        ``` bash
        ./build/tests/performance_tests/performance_test.py
        ```
    2. Single Performance Test Basic Run (Manually run one-off test):
        ```bash
        ./build/tests/performance_tests/performance_test_basic.py
        ```
3. Collect Results - By default the Performance Harness will capture and save logs.  To delete logs, use `--del-perf-logs`.  Additionally, final reports will be collected by default.  To omit final reports, use `--del-report` and/or `--del-test-report`.
    1. Navigate to performance test logs directory
        ```bash
        cd ./build/performance_test/
        ```
    2. Log Directory Structure is hierarchical with each run of the `performance_test.py` reporting into a timestamped directory where it includes the full performance report as well as a directory containing output from each test type run (here, `performance_test_basic.py`) and each individual test run outputs into a timestamped directory that may contain block data logs and transaction generator logs as well as the test's basic report.  An example directory structure follows:
        <details>
            <summary>Expand Example Directory Structure</summary>

        ``` bash
        performance_test/
        └── 2022-10-27_15-28-09
            ├── report.json
            ├── pluginThreadOptRunLogs
            │   ├── performance_test_basic
            │   ├── chainThreadResults.txt
            │   ├── netThreadResults.txt
            │   └── producerThreadResults.txt
            └── testRunLogs
                └── performance_test_basic
                    └── 2022-10-19_10-29-07
                        ├── blockDataLogs
                        │   ├── blockData.txt
                        │   └── blockTrxData.txt
                        ├── data.json
                        ├── etc
                        │   └── eosio
                        │       ├── launcher
                        │       │   └── testnet.template
                        │       ├── node_00
                        │       │   ├── config.ini
                        │       │   ├── genesis.json
                        │       │   ├── logging.json
                        │       │   └── protocol_features
                        │       │       ├── BUILTIN-ACTION_RETURN_VALUE.json
                        │       │       ├── BUILTIN-BLOCKCHAIN_PARAMETERS.json
                        │       │       ├── BUILTIN-CONFIGURABLE_WASM_LIMITS2.json
                        │       │       ├── BUILTIN-CRYPTO_PRIMITIVES.json
                        │       │       ├── BUILTIN-DISALLOW_EMPTY_PRODUCER_SCHEDULE.json
                        │       │       ├── BUILTIN-FIX_LINKAUTH_RESTRICTION.json
                        │       │       ├── BUILTIN-FORWARD_SETCODE.json
                        │       │       ├── BUILTIN-GET_BLOCK_NUM.json
                        │       │       ├── BUILTIN-GET_CODE_HASH.json
                        │       │       ├── BUILTIN-GET_SENDER.json
                        │       │       ├── BUILTIN-NO_DUPLICATE_DEFERRED_ID.json
                        │       │       ├── BUILTIN-ONLY_BILL_FIRST_AUTHORIZER.json
                        │       │       ├── BUILTIN-ONLY_LINK_TO_EXISTING_PERMISSION.json
                        │       │       ├── BUILTIN-PREACTIVATE_FEATURE.json
                        │       │       ├── BUILTIN-RAM_RESTRICTIONS.json
                        │       │       ├── BUILTIN-REPLACE_DEFERRED.json
                        │       │       ├── BUILTIN-RESTRICT_ACTION_TO_SELF.json
                        │       │       ├── BUILTIN-WEBAUTHN_KEY.json
                        │       │       └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
                        │       ├── node_01
                        │       │   ├── config.ini
                        │       │   ├── genesis.json
                        │       │   ├── logging.json
                        │       │   └── protocol_features
                        │       │       ├── BUILTIN-ACTION_RETURN_VALUE.json
                        │       │       ├── BUILTIN-BLOCKCHAIN_PARAMETERS.json
                        │       │       ├── BUILTIN-CONFIGURABLE_WASM_LIMITS2.json
                        │       │       ├── BUILTIN-CRYPTO_PRIMITIVES.json
                        │       │       ├── BUILTIN-DISALLOW_EMPTY_PRODUCER_SCHEDULE.json
                        │       │       ├── BUILTIN-FIX_LINKAUTH_RESTRICTION.json
                        │       │       ├── BUILTIN-FORWARD_SETCODE.json
                        │       │       ├── BUILTIN-GET_BLOCK_NUM.json
                        │       │       ├── BUILTIN-GET_CODE_HASH.json
                        │       │       ├── BUILTIN-GET_SENDER.json
                        │       │       ├── BUILTIN-NO_DUPLICATE_DEFERRED_ID.json
                        │       │       ├── BUILTIN-ONLY_BILL_FIRST_AUTHORIZER.json
                        │       │       ├── BUILTIN-ONLY_LINK_TO_EXISTING_PERMISSION.json
                        │       │       ├── BUILTIN-PREACTIVATE_FEATURE.json
                        │       │       ├── BUILTIN-RAM_RESTRICTIONS.json
                        │       │       ├── BUILTIN-REPLACE_DEFERRED.json
                        │       │       ├── BUILTIN-RESTRICT_ACTION_TO_SELF.json
                        │       │       ├── BUILTIN-WEBAUTHN_KEY.json
                        │       │       └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
                        │       └── node_bios
                        │           ├── config.ini
                        │           ├── genesis.json
                        │           ├── logging.json
                        │           └── protocol_features
                        │               ├── BUILTIN-ACTION_RETURN_VALUE.json
                        │               ├── BUILTIN-BLOCKCHAIN_PARAMETERS.json
                        │               ├── BUILTIN-CONFIGURABLE_WASM_LIMITS2.json
                        │               ├── BUILTIN-CRYPTO_PRIMITIVES.json
                        │               ├── BUILTIN-DISALLOW_EMPTY_PRODUCER_SCHEDULE.json
                        │               ├── BUILTIN-FIX_LINKAUTH_RESTRICTION.json
                        │               ├── BUILTIN-FORWARD_SETCODE.json
                        │               ├── BUILTIN-GET_BLOCK_NUM.json
                        │               ├── BUILTIN-GET_CODE_HASH.json
                        │               ├── BUILTIN-GET_SENDER.json
                        │               ├── BUILTIN-NO_DUPLICATE_DEFERRED_ID.json
                        │               ├── BUILTIN-ONLY_BILL_FIRST_AUTHORIZER.json
                        │               ├── BUILTIN-ONLY_LINK_TO_EXISTING_PERMISSION.json
                        │               ├── BUILTIN-PREACTIVATE_FEATURE.json
                        │               ├── BUILTIN-RAM_RESTRICTIONS.json
                        │               ├── BUILTIN-REPLACE_DEFERRED.json
                        │               ├── BUILTIN-RESTRICT_ACTION_TO_SELF.json
                        │               ├── BUILTIN-WEBAUTHN_KEY.json
                        │               └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
                        ├── trxGenLogs
                        │   ├── trx_data_output_26451.txt
                        │   ├── trx_data_output_26452.txt
                        │   ├── trx_data_output_26453.txt
                        │   └── trx_data_output_26454.txt
                        └── var
                            └── var
                                ├── lib
                                │   ├── node_00
                                │   │   ├── blocks
                                │   │   │   ├── blocks.index
                                │   │   │   ├── blocks.log
                                │   │   │   └── reversible
                                │   │   ├── nodeos.pid
                                │   │   ├── snapshots
                                │   │   ├── state
                                │   │   │   └── shared_memory.bin
                                │   │   ├── stderr.2022_10_27_10_49_01.txt
                                │   │   ├── stderr.txt -> stderr.2022_10_27_10_49_01.txt
                                │   │   └── stdout.txt
                                │   ├── node_01
                                │   │   ├── blocks
                                │   │   │   ├── blocks.index
                                │   │   │   ├── blocks.log
                                │   │   │   └── reversible
                                │   │   ├── nodeos.pid
                                │   │   ├── snapshots
                                │   │   ├── state
                                │   │   │   └── shared_memory.bin
                                │   │   ├── stderr.2022_10_27_10_49_01.txt
                                │   │   ├── stderr.txt -> stderr.2022_10_27_10_49_01.txt
                                │   │   ├── stdout.txt
                                │   │   └── traces
                                │   │       ├── trace_0000000000-0000010000.log
                                │   │       ├── trace_index_0000000000-0000010000.log
                                │   │       └── trace_trx_id_0000000000-0000010000.log
                                │   └── node_bios
                                │       ├── blocks
                                │       │   ├── blocks.index
                                │       │   ├── blocks.log
                                │       │   └── reversible
                                │       │       └── fork_db.dat
                                │       ├── nodeos.pid
                                │       ├── snapshots
                                │       ├── state
                                │       │   └── shared_memory.bin
                                │       ├── stderr.2022_10_27_10_49_01.txt
                                │       ├── stderr.txt -> stderr.2022_10_27_10_49_01.txt
                                │       ├── stdout.txt
                                │       └── traces
                                │           ├── trace_0000000000-0000010000.log
                                │           ├── trace_index_0000000000-0000010000.log
                                │           └── trace_trx_id_0000000000-0000010000.log
                                ├── test_keosd_err.log
                                ├── test_keosd_out.log
                                └── test_wallet_0
                                    ├── config.ini
                                    ├── default.wallet
                                    ├── ignition.wallet
                                    ├── keosd.sock
                                    └── wallet.lock
        ```
        </details>

## Configuring Performance Harness Tests

### Performance Test

The Performance Harness main script `performance_test.py` can be configured using the following command line arguments:

<details open>
    <summary>Expand Argument List</summary>

* `-p P`                  producing nodes count (default: 1)
* `-n N`                  total nodes (default: 0)
* `-d D`                  delay between nodes startup (default: 1)
* `--nodes-file NODES_FILE`
                          File containing nodes info in JSON format. (default: None)
* `-s {mesh}`             topology (default: mesh)
* `--dump-error-details`  Upon error print `etc/eosio/node_*/config.ini` and `var/lib/node_*/stderr.log` to stdout (default: False)
* `-v`                    verbose logging (default: False)
* `--leave-running`       Leave cluster running after test finishes (default: False)
* `--clean-run`           Kill all nodeos and keosd instances (default: False)
* `--max-tps-to-test MAX_TPS_TO_TEST`
                          The max target transfers realistic as ceiling of test range (default: 50000)
* `--test-iteration-duration-sec TEST_ITERATION_DURATION_SEC`
                          The duration of transfer trx generation for each iteration of the test during the initial search (seconds) (default: 30)
* `--test-iteration-min-step TEST_ITERATION_MIN_STEP`
                          The step size determining granularity of tps result during initial search (default: 500)
* `--final-iterations-duration-sec FINAL_ITERATIONS_DURATION_SEC`
                          The duration of transfer trx generation for each final longer run iteration of the test during
                          the final search (seconds) (default: 90)
* `--tps-limit-per-generator TPS_LIMIT_PER_GENERATOR`
                          Maximum amount of transactions per second a single generator can have. (default: 4000)
* `--genesis GENESIS`     Path to genesis.json (default: tests/performance_tests/genesis.json)
* `--num-blocks-to-prune NUM_BLOCKS_TO_PRUNE`
                          The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks,
                          to prune from the beginning and end of the range of blocks of interest for evaluation.
                          (default: 2)
* `--signature-cpu-billable-pct SIGNATURE_CPU_BILLABLE_PCT`
                          Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50% (default: 0)
* `--chain-state-db-size-mb CHAIN_STATE_DB_SIZE_MB`
                          Maximum size (in MiB) of the chain state database (default: 10240)
* `--chain-threads CHAIN_THREADS`
                          Number of worker threads in controller thread pool (default: 3)
* `--database-map-mode {mapped,heap,locked}`
                          Database map mode ("mapped", "heap", or "locked").
                          In "mapped" mode database is memory mapped as a file.
                          In "heap" mode database is preloaded in to swappable memory and will use huge pages if available.
                          In "locked" mode database is preloaded, locked in to memory, and will use huge pages if available. (default: mapped)
* `--net-threads NET_THREADS`
                          Number of worker threads in net_plugin thread pool (default: 2)
* `--disable-subjective-billing DISABLE_SUBJECTIVE_BILLING`
                          Disable subjective CPU billing for API/P2P transactions (default: True)
* `--last-block-time-offset-us LAST_BLOCK_TIME_OFFSET_US`
                          Offset of last block producing time in microseconds. Valid range 0 .. -block_time_interval. (default: 0)
* `--produce-time-offset-us PRODUCE_TIME_OFFSET_US`
                          Offset of non last block producing time in microseconds. Valid range 0 .. -block_time_interval. (default: 0)
* `--cpu-effort-percent CPU_EFFORT_PERCENT`
                          Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80% (default: 100)
* `--last-block-cpu-effort-percent LAST_BLOCK_CPU_EFFORT_PERCENT`
                          Percentage of cpu block production time used to produce last block. Whole number percentages, e.g. 80 for 80% (default: 100)
* `--producer-threads PRODUCER_THREADS`
                          Number of worker threads in producer thread pool (default: 6)
* `--http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS`
                          Maximum time for processing a request, -1 for unlimited (default: 990000)
* `--del-perf-logs`       Whether to delete performance test specific logs. (default: False)
* `--del-report`          Whether to delete overarching performance run report. (default: False)
* `--del-test-report`     Whether to save json reports from each test scenario. (default: False)
* `--quiet`               Whether to quiet printing intermediate results and reports to stdout (default: False)
* `--prods-enable-trace-api`
                          Determines whether producer nodes should have eosio::trace_api_plugin enabled (default: False)
* `--skip-tps-test`       Determines whether to skip the max TPS measurement tests (default: False)
* `--calc-producer-threads {none,lmax,full}`
                          Determines whether to calculate number of worker threads to use in producer thread pool ("none", "lmax", or "full").
                          In "none" mode, the default, no calculation will be attempted and default configured --producer-threads value will be used.
                          In "lmax" mode, producer threads will incrementally be tested until the performance rate ceases to increase with the addition of additional threads.
                          In "full" mode producer threads will incrementally be tested from 2..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in "lmax" mode). Useful for graphing the full performance impact of each available thread. (default: none)
* `--calc-chain-threads {none,lmax,full}`
                          Determines whether to calculate number of worker threads to use in chain thread pool ("none", "lmax", or "full").
                          In "none" mode, the default, no calculation will be attempted and default configured --chain-threads value will be used.
                          In "lmax" mode, producer threads will incrementally be tested until the performance rate ceases to increase with the addition of additional threads.
                          In "full" mode producer threads will incrementally be tested from 2..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in "lmax" mode). Useful for graphing the full performance impact of each available thread. (default: none)
* `--calc-net-threads {none,lmax,full}`
                          Determines whether to calculate number of worker threads to use in net thread pool ("none", "lmax", or "full").
                          In "none" mode, the default, no calculation will be attempted and default configured --net-threads value will be used.
                          In "lmax" mode, producer threads will incrementally be tested until the performance rate ceases to increase with the addition of additional threads.
                          In "full" mode producer threads will incrementally be tested from 2..num logical processors, recording each performance and choosing the local max performance (same value as would be discovered in "lmax" mode). Useful for graphing the full performance impact of each available thread. (default: none)
* `--account-name`        Name of the account to create and assign a contract to
* `--owner-public-key`    Owner public key to use with specified account name
* `--active-public-key`   Active public key to use with specified account name
* `--contract-dir`        Path to contract dir
* `--wasm-file`           WASM file name for contract
* `--abi-file`            ABI file name for contract
</details>

### Support Scripts

The following scripts are typically used by the Performance Harness main script `performance_test.py` to perform specific tasks as delegated and configured by the main script.  However, there may be applications in certain use cases where running a single one-off test or transaction generator is desired.  In those situations, the following argument details might be useful to understanding how to run these utilities in stand-alone mode.  The argument breakdown may also be useful in understanding how the Performance Harness main script's arguments are being passed through to configure lower-level entities.

#### Performance Test Basic

`performance_test_basic.py` can be configured using the following command line arguments:

<details>
    <summary>Expand Argument List</summary>

* `-p P`                  producing nodes count (default: 1)
* `-n N`                  total nodes (default: 0)
* `-d D`                  delay between nodes startup (default: 1)
* `--nodes-file NODES_FILE`
                          File containing nodes info in JSON format. (default: None)
* `-s {mesh}`             topology (default: mesh)
* `--dump-error-details`  Upon error print `etc/eosio/node_*/config.ini` and `var/lib/node_*/stderr.log` to stdout (default: False)
* `-v`                    verbose logging (default: False)
* `--leave-running`       Leave cluster running after test finishes (default: False)
* `--clean-run`           Kill all nodeos and keosd instances (default: False)
* `--target-tps TARGET_TPS`
                          The target transfers per second to send during test (default: 8000)
* `--tps-limit-per-generator TPS_LIMIT_PER_GENERATOR`
                          Maximum amount of transactions per second a single generator can have. (default: 4000)
* `--test-duration-sec TEST_DURATION_SEC`
                          The duration of transfer trx generation for the test in seconds (default: 30)
* `--genesis GENESIS`     Path to genesis.json (default: tests/performance_tests/genesis.json)
* `--num-blocks-to-prune NUM_BLOCKS_TO_PRUNE`
                          The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, to prune from the beginning and end
                          of the range of blocks of interest for evaluation. (default: 2)
* `--signature-cpu-billable-pct SIGNATURE_CPU_BILLABLE_PCT`
                          Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50% (default: 0)
* `--chain-state-db-size-mb CHAIN_STATE_DB_SIZE_MB`
                          Maximum size (in MiB) of the chain state database (default: 10240)
* `--chain-threads CHAIN_THREADS`
                          Number of worker threads in controller thread pool (default: 3)
* `--database-map-mode {mapped,heap,locked}`
                          Database map mode ("mapped", "heap", or "locked").
                          In "mapped" mode database is memory mapped as a file.
                          In "heap" mode database is preloaded in to swappable memory and will use huge pages if available.
                          In "locked" mode database is preloaded, locked in to memory, and will use huge pages if available. (default: mapped)
* `--net-threads NET_THREADS`
                          Number of worker threads in net_plugin thread pool (default: 2)
* `--disable-subjective-billing DISABLE_SUBJECTIVE_BILLING`
                          Disable subjective CPU billing for API/P2P transactions (default: True)
* `--last-block-time-offset-us LAST_BLOCK_TIME_OFFSET_US`
                          Offset of last block producing time in microseconds. Valid range 0 .. -block_time_interval. (default: 0)
* `--produce-time-offset-us PRODUCE_TIME_OFFSET_US`
                          Offset of non last block producing time in microseconds. Valid range 0 .. -block_time_interval. (default: 0)
* `--cpu-effort-percent CPU_EFFORT_PERCENT`
                          Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80% (default: 100)
* `--last-block-cpu-effort-percent LAST_BLOCK_CPU_EFFORT_PERCENT`
                          Percentage of cpu block production time used to produce last block. Whole number percentages, e.g. 80 for 80% (default: 100)
* `--producer-threads PRODUCER_THREADS`
                          Number of worker threads in producer thread pool (default: 6)
* `--http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS`
                          Maximum time for processing a request, -1 for unlimited (default: 990000)
* `--del-perf-logs`       Whether to delete performance test specific logs. (default: False)
* `--del-report`          Whether to delete overarching performance run report. (default: False)
* `--quiet`               Whether to quiet printing intermediate results and reports to stdout (default: False)
* `--prods-enable-trace-api`
                          Determines whether producer nodes should have eosio::trace_api_plugin enabled (default: False)
* `--print-missing-transactions`
                          Toggles if missing transactions are be printed upon test completion. (default: False)
* `--account-name`        Name of the account to create and assign a contract to
* `--owner-public-key`    Owner public key to use with specified account name
* `--active-public-key`   Active public key to use with specified account name
* `--contract-dir`        Path to contract dir
* `--wasm-file`           WASM file name for contract
* `--abi-file`            ABI file name for contract
</details>

#### Launch Transaction Generators (TestHarness)

`launch_transaction_generators.py` can be configured using the following command line arguments:

<details>
    <summary>Expand Argument List</summary>

* `chain_id`                    set the chain id
* `last_irreversible_block_id`  Current last-irreversible-block-id (LIB ID) to use for transactions.
* `contract_owner_account`      Account name of the contract owner account for the transfer actions
* `accounts`                    Comma separated list of account names
* `priv_keys`                   Comma separated list of private keys.
* `trx_gen_duration`            Transaction generation duration (seconds). Defaults to 60 seconds.
* `target_tps`                  Target transactions per second to generate/send.
* `tps_limit_per_generator`     Maximum amount of transactions per second a single generator can have.
* `log_dir`                     set the logs directory
</details>

#### Transaction Generator
`./build/tests/trx_generator/trx_generator` can be configured using the following command line arguments:

<details>
    <summary>Expand Argument List</summary>

* `--chain-id arg`                  set the chain id
* `--contract-owner-account arg`    Account name of the contract owner account for
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

## Result Reports

### Performance Test Report

The Performance Harness generates a report to summarize results of test scenarios as well as overarching results of the performance harness run.  By default the report described below will be written to the top level timestamped directory for the performance run with the file name `report.json`. To omit final report, use `--del-report`.

Command used to run test and generate report:

``` bash
.build/tests/performance_tests/performance_test.py --test-iteration-duration-sec 10 --final-iterations-duration-sec 30 --calc-producer-threads lmax --calc-chain-threads lmax --calc-net-threads lmax
```

#### Report Breakdown
The report begins by delivering the max TPS results of the performance run.

* `InitialMaxTpsAchieved` - the max TPS throughput achieved during initial, short duration test scenarios to narrow search window
* `LongRunningMaxTpsAchieved` - the max TPS throughput achieved during final, longer duration test scenarios to zero in on sustainable max TPS

Next, a summary of the search scenario conducted and respective results is included.  Each summary includes information on the current state of the overarching search as well as basic results of the individual test that are used to determine whether the basic test was considered successful. The list of summary results are included in `InitialSearchResults` and `LongRunningSearchResults`. The number of entries in each list will vary depending on the TPS range tested (`--max-tps-to-test`) and the configured `--test-iteration-min-step`.
<details>
    <summary>Expand Search Scenario Summary Example</summary>

``` json
    "1": {
      "success": true,
      "searchTarget": 26000,
      "searchFloor": 0,
      "searchCeiling": 26500,
      "basicTestResult": {
        "targetTPS": 26000,
        "resultAvgTps": 25986.9375,
        "expectedTxns": 260000,
        "resultTxns": 260000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-18-52-26000",
        "testStart": "2022-11-23T15:18:52.115767",
        "testEnd": "2022-11-23T15:20:16.911367"
      }
    }
```
</details>

Finally, the full detail test report for each of the determined max TPS throughput (`InitialMaxTpsAchieved` and `LongRunningMaxTpsAchieved`) runs is included after each scenario summary list in the full report.  **Note:** In the example full report below, these have been truncated as they are single performance test basic run reports as detailed in the following section [Performance Test Basic Report](#performance-test-basic).  Herein these truncated reports appear as:

<details>
    <summary>Expand Truncated Report Example</summary>

``` json
"InitialMaxTpsReport": {
    <truncated>
    "Analysis": {
      <truncated>
    },
    "args": {
      <truncated>
    },
    "env": {
      <truncated>
    },
    <truncated>
}
```
</details>

<details>
    <summary>Expand for full sample Performance Test Report</summary>

``` json
{
  "perfTestsBegin": "2022-11-23T12:56:58.699686",
  "perfTestsFinish": "2022-11-23T15:20:16.979815",
  "InitialMaxTpsAchieved": 26500,
  "LongRunningMaxTpsAchieved": 26000,
  "tpsTestStart": "2022-11-23T15:05:42.005050",
  "tpsTestFinish": "2022-11-23T15:20:16.979800",
  "InitialSearchResults": {
    "0": {
      "success": false,
      "searchTarget": 50000,
      "searchFloor": 0,
      "searchCeiling": 50000,
      "basicTestResult": {
        "targetTPS": 50000,
        "resultAvgTps": 23784.324324324323,
        "expectedTxns": 500000,
        "resultTxns": 500000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 38,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-05-42-50000",
        "testStart": "2022-11-23T15:05:42.005080",
        "testEnd": "2022-11-23T15:07:24.111044"
      }
    },
    "1": {
      "success": true,
      "searchTarget": 25000,
      "searchFloor": 0,
      "searchCeiling": 49500,
      "basicTestResult": {
        "targetTPS": 25000,
        "resultAvgTps": 25013.3125,
        "expectedTxns": 250000,
        "resultTxns": 250000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-07-24-25000",
        "testStart": "2022-11-23T15:07:24.225706",
        "testEnd": "2022-11-23T15:08:47.510691"
      }
    },
    "2": {
      "success": false,
      "searchTarget": 37500,
      "searchFloor": 25500,
      "searchCeiling": 49500,
      "basicTestResult": {
        "targetTPS": 37500,
        "resultAvgTps": 24912.576923076922,
        "expectedTxns": 375000,
        "resultTxns": 375000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 27,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-08-47-37500",
        "testStart": "2022-11-23T15:08:47.579754",
        "testEnd": "2022-11-23T15:10:23.342881"
      }
    },
    "3": {
      "success": false,
      "searchTarget": 31500,
      "searchFloor": 25500,
      "searchCeiling": 37000,
      "basicTestResult": {
        "targetTPS": 31500,
        "resultAvgTps": 24525.095238095237,
        "expectedTxns": 315000,
        "resultTxns": 315000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 22,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-10-23-31500",
        "testStart": "2022-11-23T15:10:23.432821",
        "testEnd": "2022-11-23T15:11:53.366694"
      }
    },
    "4": {
      "success": false,
      "searchTarget": 28500,
      "searchFloor": 25500,
      "searchCeiling": 31000,
      "basicTestResult": {
        "targetTPS": 28500,
        "resultAvgTps": 25896.666666666668,
        "expectedTxns": 285000,
        "resultTxns": 285000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 19,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-11-53-28500",
        "testStart": "2022-11-23T15:11:53.448449",
        "testEnd": "2022-11-23T15:13:17.714663"
      }
    },
    "5": {
      "success": false,
      "searchTarget": 27000,
      "searchFloor": 25500,
      "searchCeiling": 28000,
      "basicTestResult": {
        "targetTPS": 27000,
        "resultAvgTps": 26884.625,
        "expectedTxns": 270000,
        "resultTxns": 270000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-13-17-27000",
        "testStart": "2022-11-23T15:13:17.787205",
        "testEnd": "2022-11-23T15:14:40.753850"
      }
    },
    "6": {
      "success": true,
      "searchTarget": 26000,
      "searchFloor": 25500,
      "searchCeiling": 26500,
      "basicTestResult": {
        "targetTPS": 26000,
        "resultAvgTps": 25959.0,
        "expectedTxns": 260000,
        "resultTxns": 260000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-14-40-26000",
        "testStart": "2022-11-23T15:14:40.823681",
        "testEnd": "2022-11-23T15:16:02.884525"
      }
    },
    "7": {
      "success": true,
      "searchTarget": 26500,
      "searchFloor": 26500,
      "searchCeiling": 26500,
      "basicTestResult": {
        "targetTPS": 26500,
        "resultAvgTps": 26400.5625,
        "expectedTxns": 265000,
        "resultTxns": 265000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-16-02-26500",
        "testStart": "2022-11-23T15:16:02.953195",
        "testEnd": "2022-11-23T15:17:28.412837"
      }
    }
  },
  "InitialMaxTpsReport": {
    <truncated>
    "Analysis": {
      <truncated>
    },
    "args": {
      <truncated>
    },
    "env": {
      <truncated>
    },
    <truncated>
  },
  "LongRunningSearchResults": {
    "0": {
      "success": false,
      "searchTarget": 26500,
      "searchFloor": 0,
      "searchCeiling": 26500,
      "basicTestResult": {
        "targetTPS": 26500,
        "resultAvgTps": 22554.42105263158,
        "expectedTxns": 265000,
        "resultTxns": 265000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 20,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-17-28-26500",
        "testStart": "2022-11-23T15:17:28.483195",
        "testEnd": "2022-11-23T15:18:52.048868"
      }
    },
    "1": {
      "success": true,
      "searchTarget": 26000,
      "searchFloor": 0,
      "searchCeiling": 26500,
      "basicTestResult": {
        "targetTPS": 26000,
        "resultAvgTps": 25986.9375,
        "expectedTxns": 260000,
        "resultTxns": 260000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "./performance_test/2022-11-23_12-56-58/testRunLogs/performance_test_basic/2022-11-23_15-18-52-26000",
        "testStart": "2022-11-23T15:18:52.115767",
        "testEnd": "2022-11-23T15:20:16.911367"
      }
    }
  },
  "LongRunningMaxTpsReport": {
    <truncated>
    "Analysis": {
      <truncated>
    },
    "args": {
      <truncated>
    },
    "env": {
      <truncated>
    },
    <truncated>
  },
  "ProducerThreadAnalysis": {
    "recommendedThreadCount": 6,
    "threadToMaxTpsDict": {
      "2": 16000,
      "3": 21000,
      "4": 24000,
      "5": 25500,
      "6": 27000,
      "7": 26000
    },
    "analysisStart": "2022-11-23T12:56:58.730271",
    "analysisFinish": "2022-11-23T14:05:45.727625"
  },
  "ChainThreadAnalysis": {
    "recommendedThreadCount": 3,
    "threadToMaxTpsDict": {
      "2": 25000,
      "3": 26500,
      "4": 26500
    },
    "analysisStart": "2022-11-23T14:05:45.728348",
    "analysisFinish": "2022-11-23T14:41:43.721885"
  },
  "NetThreadAnalysis": {
    "recommendedThreadCount": 2,
    "threadToMaxTpsDict": {
      "2": 25500,
      "3": 25000
    },
    "analysisStart": "2022-11-23T14:41:43.722862",
    "analysisFinish": "2022-11-23T15:05:42.004421"
  },
  "args": {
    "killAll": false,
    "dontKill": false,
    "keepLogs": true,
    "dumpErrorDetails": false,
    "delay": 1,
    "nodesFile": null,
    "verbose": false,
    "_killEosInstances": true,
    "_killWallet": true,
    "pnodes": 1,
    "totalNodes": 0,
    "topo": "mesh",
    "extraNodeosArgs": {
      "chainPluginArgs": {
        "signatureCpuBillablePct": 0,
        "chainStateDbSizeMb": 10240,
        "chainThreads": 3,
        "databaseMapMode": "mapped"
      },
      "producerPluginArgs": {
        "disableSubjectiveBilling": true,
        "lastBlockTimeOffsetUs": 0,
        "produceTimeOffsetUs": 0,
        "cpuEffortPercent": 100,
        "lastBlockCpuEffortPercent": 100,
        "producerThreads": 6
      },
      "httpPluginArgs": {
        "httpMaxResponseTimeMs": 990000
      },
      "netPluginArgs": {
        "netThreads": 2
      }
    },
    "useBiosBootFile": false,
    "genesisPath": "tests/performance_tests/genesis.json",
    "maximumP2pPerHost": 5000,
    "maximumClients": 0,
    "loggingDict": {
      "bios": "off"
    },
    "prodsEnableTraceApi": false,
    "specificExtraNodeosArgs": {
      "1": "--plugin eosio::trace_api_plugin"
    },
    "_totalNodes": 2,
    "testDurationSec": 10,
    "finalDurationSec": 30,
    "delPerfLogs": false,
    "maxTpsToTest": 50000,
    "testIterationMinStep": 500,
    "tpsLimitPerGenerator": 4000,
    "delReport": false,
    "delTestReport": false,
    "numAddlBlocksToPrune": 2,
    "quiet": false,
    "logDirRoot": ".",
    "skipTpsTests": false,
    "calcProducerThreads": "lmax",
    "calcChainThreads": "lmax",
    "calcNetThreads": "lmax",
    "logDirBase": "./performance_test",
    "logDirTimestamp": "2022-11-23_12-56-58",
    "logDirPath": "./performance_test/2022-11-23_12-56-58",
    "ptbLogsDirPath": "./performance_test/2022-11-23_12-56-58/testRunLogs",
    "pluginThreadOptLogsDirPath": "./performance_test/2022-11-23_12-56-58/pluginThreadOptRunLogs"
  },
  "env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.15.74.2-microsoft-standard-WSL2",
    "logical_cpu_count": 16
  },
  "nodeosVersion": "v4.0.0-dev"
}
```
</details>


### Performance Test Basic Report

The Performance Test Basic generates, by default, a report that details results of the test, statistics around metrics of interest, as well as diagnostic information about the test run.  If `performance_test.py` is run with `--del-test-report`, or `performance_test_basic.py` is run with `--del-report`, the report described below will not be written.  Otherwise the report will be written to the timestamped directory within the `performance_test_basic` log directory for the test run with the file name `data.json`.

<details>
    <summary>Expand for full sample report</summary>
    
``` json
{
  "completedRun": true,
  "testStart": "2023-02-15T22:32:36.946671",
  "testFinish": "2023-02-15T22:33:41.280861",
  "Analysis": {
    "BlockSize": {
      "min": 1920,
      "max": 1920,
      "avg": 1920.0,
      "sigma": 0.0,
      "emptyBlocks": 0,
      "numBlocks": 7
    },
    "BlocksGuide": {
      "firstBlockNum": 2,
      "lastBlockNum": 129,
      "totalBlocks": 128,
      "testStartBlockNum": 113,
      "testEndBlockNum": 129,
      "setupBlocksCnt": 111,
      "tearDownBlocksCnt": 0,
      "leadingEmptyBlocksCnt": 1,
      "trailingEmptyBlocksCnt": 5,
      "configAddlDropCnt": 2,
      "testAnalysisBlockCnt": 7
    },
    "TPS": {
      "min": 20,
      "max": 20,
      "avg": 20.0,
      "sigma": 0.0,
      "emptyBlocks": 0,
      "numBlocks": 7,
      "configTps": 20,
      "configTestDuration": 5,
      "tpsPerGenerator": [
        10,
        10
      ],
      "generatorCount": 2
    },
    "TrxCPU": {
      "min": 20.0,
      "max": 141.0,
      "avg": 72.15,
      "sigma": 31.078408903932,
      "samples": 100
    },
    "TrxLatency": {
      "min": 0.0409998893737793,
      "max": 0.4419999122619629,
      "avg": 0.24149999618530274,
      "sigma": 0.14142224015850113,
      "samples": 100
    },
    "TrxNet": {
      "min": 24.0,
      "max": 24.0,
      "avg": 24.0,
      "sigma": 0.0,
      "samples": 100
    },
    "DroppedBlocks": {},
    "DroppedBlocksCount": 0,
    "DroppedTransactions": 0,
    "ProductionWindowsTotal": 0,
    "ProductionWindowsAverageSize": 0,
    "ProductionWindowsMissed": 0,
    "ForkedBlocks": [],
    "ForksCount": 0
  },
  "args": {
    "rawCmdLine ": "/home/leap/build/tests/performance_tests/performance_test_basic.py -v -p 1 -n 1 --target-tps 20 --tps-limit-per-generator 10 --test-duration-sec 5 --clean-run",
    "killAll": true,
    "dontKill": false,
    "keepLogs": true,
    "dumpErrorDetails": false,
    "delay": 1,
    "nodesFile": null,
    "verbose": true,
    "_killEosInstances": true,
    "_killWallet": true,
    "pnodes": 1,
    "totalNodes": 1,
    "topo": "mesh",
    "extraNodeosArgs": {
      "chainPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "chain_plugin",
        "blocksDir": null,
        "_blocksDirNodeosDefault": "\"blocks\"",
        "_blocksDirNodeosArg": "--blocks-dir",
        "stateDir": null,
        "_stateDirNodeosDefault": "\"state\"",
        "_stateDirNodeosArg": "--state-dir",
        "protocolFeaturesDir": null,
        "_protocolFeaturesDirNodeosDefault": "\"protocol_features\"",
        "_protocolFeaturesDirNodeosArg": "--protocol-features-dir",
        "checkpoint": null,
        "_checkpointNodeosDefault": null,
        "_checkpointNodeosArg": "--checkpoint",
        "wasmRuntime": null,
        "_wasmRuntimeNodeosDefault": "eos-vm-jit",
        "_wasmRuntimeNodeosArg": "--wasm-runtime",
        "profileAccount": null,
        "_profileAccountNodeosDefault": null,
        "_profileAccountNodeosArg": "--profile-account",
        "abiSerializerMaxTimeMs": null,
        "_abiSerializerMaxTimeMsNodeosDefault": 15,
        "_abiSerializerMaxTimeMsNodeosArg": "--abi-serializer-max-time-ms",
        "chainStateDbSizeMb": 10240,
        "_chainStateDbSizeMbNodeosDefault": 1024,
        "_chainStateDbSizeMbNodeosArg": "--chain-state-db-size-mb",
        "chainStateDbGuardSizeMb": null,
        "_chainStateDbGuardSizeMbNodeosDefault": 128,
        "_chainStateDbGuardSizeMbNodeosArg": "--chain-state-db-guard-size-mb",
        "signatureCpuBillablePct": 0,
        "_signatureCpuBillablePctNodeosDefault": 50,
        "_signatureCpuBillablePctNodeosArg": "--signature-cpu-billable-pct",
        "chainThreads": 2,
        "_chainThreadsNodeosDefault": 2,
        "_chainThreadsNodeosArg": "--chain-threads",
        "contractsConsole": null,
        "_contractsConsoleNodeosDefault": false,
        "_contractsConsoleNodeosArg": "--contracts-console",
        "deepMind": null,
        "_deepMindNodeosDefault": false,
        "_deepMindNodeosArg": "--deep-mind",
        "actorWhitelist": null,
        "_actorWhitelistNodeosDefault": null,
        "_actorWhitelistNodeosArg": "--actor-whitelist",
        "actorBlacklist": null,
        "_actorBlacklistNodeosDefault": null,
        "_actorBlacklistNodeosArg": "--actor-blacklist",
        "contractWhitelist": null,
        "_contractWhitelistNodeosDefault": null,
        "_contractWhitelistNodeosArg": "--contract-whitelist",
        "contractBlacklist": null,
        "_contractBlacklistNodeosDefault": null,
        "_contractBlacklistNodeosArg": "--contract-blacklist",
        "actionBlacklist": null,
        "_actionBlacklistNodeosDefault": null,
        "_actionBlacklistNodeosArg": "--action-blacklist",
        "keyBlacklist": null,
        "_keyBlacklistNodeosDefault": null,
        "_keyBlacklistNodeosArg": "--key-blacklist",
        "senderBypassWhiteblacklist": null,
        "_senderBypassWhiteblacklistNodeosDefault": null,
        "_senderBypassWhiteblacklistNodeosArg": "--sender-bypass-whiteblacklist",
        "readMode": null,
        "_readModeNodeosDefault": "head",
        "_readModeNodeosArg": "--read-mode",
        "apiAcceptTransactions": null,
        "_apiAcceptTransactionsNodeosDefault": 1,
        "_apiAcceptTransactionsNodeosArg": "--api-accept-transactions",
        "validationMode": null,
        "_validationModeNodeosDefault": "full",
        "_validationModeNodeosArg": "--validation-mode",
        "disableRamBillingNotifyChecks": null,
        "_disableRamBillingNotifyChecksNodeosDefault": false,
        "_disableRamBillingNotifyChecksNodeosArg": "--disable-ram-billing-notify-checks",
        "maximumVariableSignatureLength": null,
        "_maximumVariableSignatureLengthNodeosDefault": 16384,
        "_maximumVariableSignatureLengthNodeosArg": "--maximum-variable-signature-length",
        "trustedProducer": null,
        "_trustedProducerNodeosDefault": null,
        "_trustedProducerNodeosArg": "--trusted-producer",
        "databaseMapMode": "mapped",
        "_databaseMapModeNodeosDefault": "mapped",
        "_databaseMapModeNodeosArg": "--database-map-mode",
        "eosVmOcCacheSizeMb": null,
        "_eosVmOcCacheSizeMbNodeosDefault": 1024,
        "_eosVmOcCacheSizeMbNodeosArg": "--eos-vm-oc-cache-size-mb",
        "eosVmOcCompileThreads": null,
        "_eosVmOcCompileThreadsNodeosDefault": 1,
        "_eosVmOcCompileThreadsNodeosArg": "--eos-vm-oc-compile-threads",
        "eosVmOcEnable": null,
        "_eosVmOcEnableNodeosDefault": false,
        "_eosVmOcEnableNodeosArg": "--eos-vm-oc-enable",
        "enableAccountQueries": null,
        "_enableAccountQueriesNodeosDefault": 0,
        "_enableAccountQueriesNodeosArg": "--enable-account-queries",
        "maxNonprivilegedInlineActionSize": null,
        "_maxNonprivilegedInlineActionSizeNodeosDefault": 4096,
        "_maxNonprivilegedInlineActionSizeNodeosArg": "--max-nonprivileged-inline-action-size",
        "transactionRetryMaxStorageSizeGb": null,
        "_transactionRetryMaxStorageSizeGbNodeosDefault": null,
        "_transactionRetryMaxStorageSizeGbNodeosArg": "--transaction-retry-max-storage-size-gb",
        "transactionRetryIntervalSec": null,
        "_transactionRetryIntervalSecNodeosDefault": 20,
        "_transactionRetryIntervalSecNodeosArg": "--transaction-retry-interval-sec",
        "transactionRetryMaxExpirationSec": null,
        "_transactionRetryMaxExpirationSecNodeosDefault": 120,
        "_transactionRetryMaxExpirationSecNodeosArg": "--transaction-retry-max-expiration-sec",
        "transactionFinalityStatusMaxStorageSizeGb": null,
        "_transactionFinalityStatusMaxStorageSizeGbNodeosDefault": null,
        "_transactionFinalityStatusMaxStorageSizeGbNodeosArg": "--transaction-finality-status-max-storage-size-gb",
        "transactionFinalityStatusSuccessDurationSec": null,
        "_transactionFinalityStatusSuccessDurationSecNodeosDefault": 180,
        "_transactionFinalityStatusSuccessDurationSecNodeosArg": "--transaction-finality-status-success-duration-sec",
        "transactionFinalityStatusFailureDurationSec": null,
        "_transactionFinalityStatusFailureDurationSecNodeosDefault": 180,
        "_transactionFinalityStatusFailureDurationSecNodeosArg": "--transaction-finality-status-failure-duration-sec",
        "integrityHashOnStart": null,
        "_integrityHashOnStartNodeosDefault": false,
        "_integrityHashOnStartNodeosArg": "--integrity-hash-on-start",
        "integrityHashOnStop": null,
        "_integrityHashOnStopNodeosDefault": false,
        "_integrityHashOnStopNodeosArg": "--integrity-hash-on-stop",
        "blockLogRetainBlocks": null,
        "_blockLogRetainBlocksNodeosDefault": null,
        "_blockLogRetainBlocksNodeosArg": "--block-log-retain-blocks",
        "genesisJson": null,
        "_genesisJsonNodeosDefault": null,
        "_genesisJsonNodeosArg": "--genesis-json",
        "genesisTimestamp": null,
        "_genesisTimestampNodeosDefault": null,
        "_genesisTimestampNodeosArg": "--genesis-timestamp",
        "printGenesisJson": null,
        "_printGenesisJsonNodeosDefault": false,
        "_printGenesisJsonNodeosArg": "--print-genesis-json",
        "extractGenesisJson": null,
        "_extractGenesisJsonNodeosDefault": null,
        "_extractGenesisJsonNodeosArg": "--extract-genesis-json",
        "printBuildInfo": null,
        "_printBuildInfoNodeosDefault": false,
        "_printBuildInfoNodeosArg": "--print-build-info",
        "extractBuildInfo": null,
        "_extractBuildInfoNodeosDefault": null,
        "_extractBuildInfoNodeosArg": "--extract-build-info",
        "forceAllChecks": null,
        "_forceAllChecksNodeosDefault": false,
        "_forceAllChecksNodeosArg": "--force-all-checks",
        "disableReplayOpts": null,
        "_disableReplayOptsNodeosDefault": false,
        "_disableReplayOptsNodeosArg": "--disable-replay-opts",
        "replayBlockchain": null,
        "_replayBlockchainNodeosDefault": false,
        "_replayBlockchainNodeosArg": "--replay-blockchain",
        "hardReplayBlockchain": null,
        "_hardReplayBlockchainNodeosDefault": false,
        "_hardReplayBlockchainNodeosArg": "--hard-replay-blockchain",
        "deleteAllBlocks": null,
        "_deleteAllBlocksNodeosDefault": false,
        "_deleteAllBlocksNodeosArg": "--delete-all-blocks",
        "truncateAtBlock": null,
        "_truncateAtBlockNodeosDefault": 0,
        "_truncateAtBlockNodeosArg": "--truncate-at-block",
        "terminateAtBlock": null,
        "_terminateAtBlockNodeosDefault": 0,
        "_terminateAtBlockNodeosArg": "--terminate-at-block",
        "snapshot": null,
        "_snapshotNodeosDefault": null,
        "_snapshotNodeosArg": "--snapshot"
      },
      "httpClientPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "http_client_plugin",
        "httpsClientRootCert": null,
        "_httpsClientRootCertNodeosDefault": null,
        "_httpsClientRootCertNodeosArg": "--https-client-root-cert",
        "httpsClientValidatePeers": null,
        "_httpsClientValidatePeersNodeosDefault": 1,
        "_httpsClientValidatePeersNodeosArg": "--https-client-validate-peers"
      },
      "httpPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "http_plugin",
        "unixSocketPath": null,
        "_unixSocketPathNodeosDefault": null,
        "_unixSocketPathNodeosArg": "--unix-socket-path",
        "httpServerAddress": null,
        "_httpServerAddressNodeosDefault": "127.0.0.1:8888",
        "_httpServerAddressNodeosArg": "--http-server-address",
        "httpsServerAddress": null,
        "_httpsServerAddressNodeosDefault": null,
        "_httpsServerAddressNodeosArg": "--https-server-address",
        "httpsCertificateChainFile": null,
        "_httpsCertificateChainFileNodeosDefault": null,
        "_httpsCertificateChainFileNodeosArg": "--https-certificate-chain-file",
        "httpsPrivateKeyFile": null,
        "_httpsPrivateKeyFileNodeosDefault": null,
        "_httpsPrivateKeyFileNodeosArg": "--https-private-key-file",
        "httpsEcdhCurve": null,
        "_httpsEcdhCurveNodeosDefault": "secp384r1",
        "_httpsEcdhCurveNodeosArg": "--https-ecdh-curve",
        "accessControlAllowOrigin": null,
        "_accessControlAllowOriginNodeosDefault": null,
        "_accessControlAllowOriginNodeosArg": "--access-control-allow-origin",
        "accessControlAllowHeaders": null,
        "_accessControlAllowHeadersNodeosDefault": null,
        "_accessControlAllowHeadersNodeosArg": "--access-control-allow-headers",
        "accessControlMaxAge": null,
        "_accessControlMaxAgeNodeosDefault": null,
        "_accessControlMaxAgeNodeosArg": "--access-control-max-age",
        "accessControlAllowCredentials": null,
        "_accessControlAllowCredentialsNodeosDefault": false,
        "_accessControlAllowCredentialsNodeosArg": "--access-control-allow-credentials",
        "maxBodySize": null,
        "_maxBodySizeNodeosDefault": 2097152,
        "_maxBodySizeNodeosArg": "--max-body-size",
        "httpMaxBytesInFlightMb": null,
        "_httpMaxBytesInFlightMbNodeosDefault": 500,
        "_httpMaxBytesInFlightMbNodeosArg": "--http-max-bytes-in-flight-mb",
        "httpMaxInFlightRequests": null,
        "_httpMaxInFlightRequestsNodeosDefault": -1,
        "_httpMaxInFlightRequestsNodeosArg": "--http-max-in-flight-requests",
        "httpMaxResponseTimeMs": 990000,
        "_httpMaxResponseTimeMsNodeosDefault": 30,
        "_httpMaxResponseTimeMsNodeosArg": "--http-max-response-time-ms",
        "verboseHttpErrors": null,
        "_verboseHttpErrorsNodeosDefault": false,
        "_verboseHttpErrorsNodeosArg": "--verbose-http-errors",
        "httpValidateHost": null,
        "_httpValidateHostNodeosDefault": 1,
        "_httpValidateHostNodeosArg": "--http-validate-host",
        "httpAlias": null,
        "_httpAliasNodeosDefault": null,
        "_httpAliasNodeosArg": "--http-alias",
        "httpThreads": null,
        "_httpThreadsNodeosDefault": 2,
        "_httpThreadsNodeosArg": "--http-threads",
        "httpKeepAlive": null,
        "_httpKeepAliveNodeosDefault": 1,
        "_httpKeepAliveNodeosArg": "--http-keep-alive"
      },
      "netPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "net_plugin",
        "p2pListenEndpoint": null,
        "_p2pListenEndpointNodeosDefault": "0.0.0.0:9876",
        "_p2pListenEndpointNodeosArg": "--p2p-listen-endpoint",
        "p2pServerAddress": null,
        "_p2pServerAddressNodeosDefault": null,
        "_p2pServerAddressNodeosArg": "--p2p-server-address",
        "p2pPeerAddress": null,
        "_p2pPeerAddressNodeosDefault": null,
        "_p2pPeerAddressNodeosArg": "--p2p-peer-address",
        "p2pMaxNodesPerHost": null,
        "_p2pMaxNodesPerHostNodeosDefault": 1,
        "_p2pMaxNodesPerHostNodeosArg": "--p2p-max-nodes-per-host",
        "p2pAcceptTransactions": null,
        "_p2pAcceptTransactionsNodeosDefault": 1,
        "_p2pAcceptTransactionsNodeosArg": "--p2p-accept-transactions",
        "agentName": null,
        "_agentNameNodeosDefault": "EOS Test Agent",
        "_agentNameNodeosArg": "--agent-name",
        "allowedConnection": null,
        "_allowedConnectionNodeosDefault": "any",
        "_allowedConnectionNodeosArg": "--allowed-connection",
        "peerKey": null,
        "_peerKeyNodeosDefault": null,
        "_peerKeyNodeosArg": "--peer-key",
        "peerPrivateKey": null,
        "_peerPrivateKeyNodeosDefault": null,
        "_peerPrivateKeyNodeosArg": "--peer-private-key",
        "maxClients": null,
        "_maxClientsNodeosDefault": 25,
        "_maxClientsNodeosArg": "--max-clients",
        "connectionCleanupPeriod": null,
        "_connectionCleanupPeriodNodeosDefault": 30,
        "_connectionCleanupPeriodNodeosArg": "--connection-cleanup-period",
        "maxCleanupTimeMsec": null,
        "_maxCleanupTimeMsecNodeosDefault": 10,
        "_maxCleanupTimeMsecNodeosArg": "--max-cleanup-time-msec",
        "p2pDedupCacheExpireTimeSec": null,
        "_p2pDedupCacheExpireTimeSecNodeosDefault": 10,
        "_p2pDedupCacheExpireTimeSecNodeosArg": "--p2p-dedup-cache-expire-time-sec",
        "netThreads": 4,
        "_netThreadsNodeosDefault": 4,
        "_netThreadsNodeosArg": "--net-threads",
        "syncFetchSpan": null,
        "_syncFetchSpanNodeosDefault": 100,
        "_syncFetchSpanNodeosArg": "--sync-fetch-span",
        "useSocketReadWatermark": null,
        "_useSocketReadWatermarkNodeosDefault": 0,
        "_useSocketReadWatermarkNodeosArg": "--use-socket-read-watermark",
        "peerLogFormat": null,
        "_peerLogFormatNodeosDefault": "[\"${_name}\" - ${_cid} ${_ip}:${_port}] ",
        "_peerLogFormatNodeosArg": "--peer-log-format",
        "p2pKeepaliveIntervalMs": null,
        "_p2pKeepaliveIntervalMsNodeosDefault": 10000,
        "_p2pKeepaliveIntervalMsNodeosArg": "--p2p-keepalive-interval-ms"
      },
      "producerPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "producer_plugin",
        "enableStaleProduction": null,
        "_enableStaleProductionNodeosDefault": false,
        "_enableStaleProductionNodeosArg": "--enable-stale-production",
        "pauseOnStartup": null,
        "_pauseOnStartupNodeosDefault": false,
        "_pauseOnStartupNodeosArg": "--pause-on-startup",
        "maxTransactionTime": null,
        "_maxTransactionTimeNodeosDefault": 30,
        "_maxTransactionTimeNodeosArg": "--max-transaction-time",
        "maxIrreversibleBlockAge": null,
        "_maxIrreversibleBlockAgeNodeosDefault": -1,
        "_maxIrreversibleBlockAgeNodeosArg": "--max-irreversible-block-age",
        "producerName": null,
        "_producerNameNodeosDefault": null,
        "_producerNameNodeosArg": "--producer-name",
        "privateKey": null,
        "_privateKeyNodeosDefault": null,
        "_privateKeyNodeosArg": "--private-key",
        "signatureProvider": null,
        "_signatureProviderNodeosDefault": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV=KEY:5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3",
        "_signatureProviderNodeosArg": "--signature-provider",
        "greylistAccount": null,
        "_greylistAccountNodeosDefault": null,
        "_greylistAccountNodeosArg": "--greylist-account",
        "greylistLimit": null,
        "_greylistLimitNodeosDefault": 1000,
        "_greylistLimitNodeosArg": "--greylist-limit",
        "produceTimeOffsetUs": 0,
        "_produceTimeOffsetUsNodeosDefault": 0,
        "_produceTimeOffsetUsNodeosArg": "--produce-time-offset-us",
        "lastBlockTimeOffsetUs": 0,
        "_lastBlockTimeOffsetUsNodeosDefault": -200000,
        "_lastBlockTimeOffsetUsNodeosArg": "--last-block-time-offset-us",
        "cpuEffortPercent": 100,
        "_cpuEffortPercentNodeosDefault": 80,
        "_cpuEffortPercentNodeosArg": "--cpu-effort-percent",
        "lastBlockCpuEffortPercent": 100,
        "_lastBlockCpuEffortPercentNodeosDefault": 80,
        "_lastBlockCpuEffortPercentNodeosArg": "--last-block-cpu-effort-percent",
        "maxBlockCpuUsageThresholdUs": null,
        "_maxBlockCpuUsageThresholdUsNodeosDefault": 5000,
        "_maxBlockCpuUsageThresholdUsNodeosArg": "--max-block-cpu-usage-threshold-us",
        "maxBlockNetUsageThresholdBytes": null,
        "_maxBlockNetUsageThresholdBytesNodeosDefault": 1024,
        "_maxBlockNetUsageThresholdBytesNodeosArg": "--max-block-net-usage-threshold-bytes",
        "maxScheduledTransactionTimePerBlockMs": null,
        "_maxScheduledTransactionTimePerBlockMsNodeosDefault": 100,
        "_maxScheduledTransactionTimePerBlockMsNodeosArg": "--max-scheduled-transaction-time-per-block-ms",
        "subjectiveCpuLeewayUs": null,
        "_subjectiveCpuLeewayUsNodeosDefault": 31000,
        "_subjectiveCpuLeewayUsNodeosArg": "--subjective-cpu-leeway-us",
        "subjectiveAccountMaxFailures": null,
        "_subjectiveAccountMaxFailuresNodeosDefault": 3,
        "_subjectiveAccountMaxFailuresNodeosArg": "--subjective-account-max-failures",
        "subjectiveAccountDecayTimeMinutes": null,
        "_subjectiveAccountDecayTimeMinutesNodeosDefault": 1440,
        "_subjectiveAccountDecayTimeMinutesNodeosArg": "--subjective-account-decay-time-minutes",
        "incomingDeferRatio": null,
        "_incomingDeferRatioNodeosDefault": 1,
        "_incomingDeferRatioNodeosArg": "--incoming-defer-ratio",
        "incomingTransactionQueueSizeMb": null,
        "_incomingTransactionQueueSizeMbNodeosDefault": 1024,
        "_incomingTransactionQueueSizeMbNodeosArg": "--incoming-transaction-queue-size-mb",
        "disableSubjectiveBilling": true,
        "_disableSubjectiveBillingNodeosDefault": 1,
        "_disableSubjectiveBillingNodeosArg": "--disable-subjective-billing",
        "disableSubjectiveAccountBilling": null,
        "_disableSubjectiveAccountBillingNodeosDefault": false,
        "_disableSubjectiveAccountBillingNodeosArg": "--disable-subjective-account-billing",
        "disableSubjectiveP2pBilling": null,
        "_disableSubjectiveP2pBillingNodeosDefault": 1,
        "_disableSubjectiveP2pBillingNodeosArg": "--disable-subjective-p2p-billing",
        "disableSubjectiveApiBilling": null,
        "_disableSubjectiveApiBillingNodeosDefault": 1,
        "_disableSubjectiveApiBillingNodeosArg": "--disable-subjective-api-billing",
        "producerThreads": 2,
        "_producerThreadsNodeosDefault": 2,
        "_producerThreadsNodeosArg": "--producer-threads",
        "snapshotsDir": null,
        "_snapshotsDirNodeosDefault": "\"snapshots\"",
        "_snapshotsDirNodeosArg": "--snapshots-dir"
      },
      "resourceMonitorPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "resource_monitor_plugin",
        "resourceMonitorIntervalSeconds": null,
        "_resourceMonitorIntervalSecondsNodeosDefault": 2,
        "_resourceMonitorIntervalSecondsNodeosArg": "--resource-monitor-interval-seconds",
        "resourceMonitorSpaceThreshold": null,
        "_resourceMonitorSpaceThresholdNodeosDefault": 90,
        "_resourceMonitorSpaceThresholdNodeosArg": "--resource-monitor-space-threshold",
        "resourceMonitorNotShutdownOnThresholdExceeded": null,
        "_resourceMonitorNotShutdownOnThresholdExceededNodeosDefault": false,
        "_resourceMonitorNotShutdownOnThresholdExceededNodeosArg": "--resource-monitor-not-shutdown-on-threshold-exceeded",
        "resourceMonitorWarningInterval": null,
        "_resourceMonitorWarningIntervalNodeosDefault": 30,
        "_resourceMonitorWarningIntervalNodeosArg": "--resource-monitor-warning-interval"
      },
      "signatureProviderPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "signature_provider_plugin",
        "keosdProviderTimeout": null,
        "_keosdProviderTimeoutNodeosDefault": 5,
        "_keosdProviderTimeoutNodeosArg": "--keosd-provider-timeout"
      },
      "stateHistoryPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "state_history_plugin",
        "stateHistoryDir": null,
        "_stateHistoryDirNodeosDefault": "\"state-history\"",
        "_stateHistoryDirNodeosArg": "--state-history-dir",
        "traceHistory": null,
        "_traceHistoryNodeosDefault": false,
        "_traceHistoryNodeosArg": "--trace-history",
        "chainStateHistory": null,
        "_chainStateHistoryNodeosDefault": false,
        "_chainStateHistoryNodeosArg": "--chain-state-history",
        "stateHistoryEndpoint": null,
        "_stateHistoryEndpointNodeosDefault": "127.0.0.1:8080",
        "_stateHistoryEndpointNodeosArg": "--state-history-endpoint",
        "stateHistoryUnixSocketPath": null,
        "_stateHistoryUnixSocketPathNodeosDefault": null,
        "_stateHistoryUnixSocketPathNodeosArg": "--state-history-unix-socket-path",
        "traceHistoryDebugMode": null,
        "_traceHistoryDebugModeNodeosDefault": false,
        "_traceHistoryDebugModeNodeosArg": "--trace-history-debug-mode",
        "stateHistoryLogRetainBlocks": null,
        "_stateHistoryLogRetainBlocksNodeosDefault": null,
        "_stateHistoryLogRetainBlocksNodeosArg": "--state-history-log-retain-blocks",
        "deleteStateHistory": null,
        "_deleteStateHistoryNodeosDefault": false,
        "_deleteStateHistoryNodeosArg": "--delete-state-history"
      },
      "traceApiPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "trace_api_plugin",
        "traceDir": null,
        "_traceDirNodeosDefault": "\"traces\"",
        "_traceDirNodeosArg": "--trace-dir",
        "traceSliceStride": null,
        "_traceSliceStrideNodeosDefault": 10000,
        "_traceSliceStrideNodeosArg": "--trace-slice-stride",
        "traceMinimumIrreversibleHistoryBlocks": null,
        "_traceMinimumIrreversibleHistoryBlocksNodeosDefault": -1,
        "_traceMinimumIrreversibleHistoryBlocksNodeosArg": "--trace-minimum-irreversible-history-blocks",
        "traceMinimumUncompressedIrreversibleHistoryBlocks": null,
        "_traceMinimumUncompressedIrreversibleHistoryBlocksNodeosDefault": -1,
        "_traceMinimumUncompressedIrreversibleHistoryBlocksNodeosArg": "--trace-minimum-uncompressed-irreversible-history-blocks",
        "traceRpcAbi": null,
        "_traceRpcAbiNodeosDefault": null,
        "_traceRpcAbiNodeosArg": "--trace-rpc-abi",
        "traceNoAbis": null,
        "_traceNoAbisNodeosDefault": false,
        "_traceNoAbisNodeosArg": "--trace-no-abis"
      }
    },
    "specifiedContract": {
      "accountName": "eosio",
      "ownerPrivateKey": "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3",
      "ownerPublicKey": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV",
      "activePrivateKey": "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3",
      "activePublicKey": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV",
      "contractDir": "unittests/contracts/eosio.system",
      "wasmFile": "eosio.system.wasm",
      "abiFile": "eosio.system.abi"
    },
    "useBiosBootFile": false,
    "genesisPath": "tests/performance_tests/genesis.json",
    "maximumP2pPerHost": 5000,
    "maximumClients": 0,
    "loggingLevel": "info",
    "loggingDict": {
      "bios": "off"
    },
    "prodsEnableTraceApi": false,
    "nodeosVers": "v4",
    "specificExtraNodeosArgs": {
      "1": "--plugin eosio::trace_api_plugin"
    },
    "_totalNodes": 2,
    "targetTps": 20,
    "testTrxGenDurationSec": 5,
    "tpsLimitPerGenerator": 10,
    "numAddlBlocksToPrune": 2,
    "logDirRoot": ".",
    "delReport": false,
    "quiet": false,
    "delPerfLogs": false,
    "expectedTransactionsSent": 100,
    "printMissingTransactions": false,
    "userTrxDataFile": null,
    "logDirBase": "p",
    "logDirTimestamp": "2023-02-15_22-32-36",
    "logDirTimestampedOptSuffix": "-20",
    "logDirPath": "p/2023-02-15_22-32-36-20"
  },
  "env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.10.16.3-microsoft-standard-WSL2",
    "logical_cpu_count": 16
  },
  "nodeosVersion": "v4.0.0-dev"
}
```
</details>
