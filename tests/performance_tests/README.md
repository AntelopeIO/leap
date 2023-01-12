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
</details>

#### Launch Transaction Generators

`launch_transaction_generators.py` can be configured using the following command line arguments:

<details>
    <summary>Expand Argument List</summary>

* `chain_id`                    set the chain id
* `last_irreversible_block_id`  Current last-irreversible-block-id (LIB ID) to use for transactions.
* `handler_account`             Account name of the handler account for the transfer actions
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
  "Analysis": {
    "BlockSize": {
      "avg": 1920.0,
      "emptyBlocks": 0,
      "max": 1920,
      "min": 1920,
      "numBlocks": 177,
      "sigma": 0.0
    },
    "BlocksGuide": {
      "configAddlDropCnt": 2,
      "firstBlockNum": 2,
      "lastBlockNum": 301,
      "leadingEmptyBlocksCnt": 1,
      "setupBlocksCnt": 112,
      "tearDownBlocksCnt": 0,
      "testAnalysisBlockCnt": 177,
      "testEndBlockNum": 301,
      "testStartBlockNum": 114,
      "totalBlocks": 300,
      "trailingEmptyBlocksCnt": 6
    },
    "DroppedBlocks": {},
    "DroppedBlocksCount": 0,
    "ForkedBlocks": [],
    "ForksCount": 0,
    "ProductionWindowsAverageSize": 0,
    "ProductionWindowsMissed": 0,
    "ProductionWindowsTotal": 0,
    "TPS": {
      "avg": 20.0,
      "configTestDuration": 90,
      "configTps": 20,
      "emptyBlocks": 0,
      "generatorCount": 2,
      "max": 20,
      "min": 20,
      "numBlocks": 177,
      "sigma": 0.0,
      "tpsPerGenerator": [
        10,
        10
      ]
    },
    "TrxCPU": {
      "avg": 89.22111111111111,
      "max": 404.0,
      "min": 7.0,
      "samples": 1800,
      "sigma": 52.66483117992383
    },
    "TrxLatency": {
      "avg": 0.47760056018829344,
      "max": 0.6789999008178711,
      "min": 0.2760000228881836,
      "samples": 1800,
      "sigma": 0.14143152148157506
    },
    "TrxNet": {
      "avg": 24.0,
      "max": 24.0,
      "min": 24.0,
      "samples": 1800,
      "sigma": 0.0
    }
  },
  "args": {
    "_killEosInstances": true,
    "_killWallet": true,
    "_totalNodes": 2,
    "delPerfLogs": false,
    "delReport": false,
    "delay": 1,
    "dontKill": false,
    "dumpErrorDetails": false,
    "expectedTransactionsSent": 1800,
    "extraNodeosArgs": {
      "chainPluginArgs": {
        "_abiSerializerMaxTimeMsNodeosArg": "--abi-serializer-max-time-ms",
        "_abiSerializerMaxTimeMsNodeosDefault": 15,
        "_actionBlacklistNodeosArg": "--action-blacklist",
        "_actionBlacklistNodeosDefault": null,
        "_actorBlacklistNodeosArg": "--actor-blacklist",
        "_actorBlacklistNodeosDefault": null,
        "_actorWhitelistNodeosArg": "--actor-whitelist",
        "_actorWhitelistNodeosDefault": null,
        "_apiAcceptTransactionsNodeosArg": "--api-accept-transactions",
        "_apiAcceptTransactionsNodeosDefault": 1,
        "_blockLogRetainBlocksNodeosArg": "--block-log-retain-blocks",
        "_blockLogRetainBlocksNodeosDefault": null,
        "_blocksDirNodeosArg": "--blocks-dir",
        "_blocksDirNodeosDefault": "\"blocks\"",
        "_chainStateDbGuardSizeMbNodeosArg": "--chain-state-db-guard-size-mb",
        "_chainStateDbGuardSizeMbNodeosDefault": 128,
        "_chainStateDbSizeMbNodeosArg": "--chain-state-db-size-mb",
        "_chainStateDbSizeMbNodeosDefault": 1024,
        "_chainThreadsNodeosArg": "--chain-threads",
        "_chainThreadsNodeosDefault": 2,
        "_checkpointNodeosArg": "--checkpoint",
        "_checkpointNodeosDefault": null,
        "_contractBlacklistNodeosArg": "--contract-blacklist",
        "_contractBlacklistNodeosDefault": null,
        "_contractWhitelistNodeosArg": "--contract-whitelist",
        "_contractWhitelistNodeosDefault": null,
        "_contractsConsoleNodeosArg": "--contracts-console",
        "_contractsConsoleNodeosDefault": false,
        "_databaseMapModeNodeosArg": "--database-map-mode",
        "_databaseMapModeNodeosDefault": "mapped",
        "_deepMindNodeosArg": "--deep-mind",
        "_deepMindNodeosDefault": false,
        "_deleteAllBlocksNodeosArg": "--delete-all-blocks",
        "_deleteAllBlocksNodeosDefault": false,
        "_disableRamBillingNotifyChecksNodeosArg": "--disable-ram-billing-notify-checks",
        "_disableRamBillingNotifyChecksNodeosDefault": false,
        "_disableReplayOptsNodeosArg": "--disable-replay-opts",
        "_disableReplayOptsNodeosDefault": false,
        "_enableAccountQueriesNodeosArg": "--enable-account-queries",
        "_enableAccountQueriesNodeosDefault": 0,
        "_eosVmOcCacheSizeMbNodeosArg": "--eos-vm-oc-cache-size-mb",
        "_eosVmOcCacheSizeMbNodeosDefault": 1024,
        "_eosVmOcCompileThreadsNodeosArg": "--eos-vm-oc-compile-threads",
        "_eosVmOcCompileThreadsNodeosDefault": 1,
        "_eosVmOcEnableNodeosArg": "--eos-vm-oc-enable",
        "_eosVmOcEnableNodeosDefault": false,
        "_extractBuildInfoNodeosArg": "--extract-build-info",
        "_extractBuildInfoNodeosDefault": null,
        "_extractGenesisJsonNodeosArg": "--extract-genesis-json",
        "_extractGenesisJsonNodeosDefault": null,
        "_forceAllChecksNodeosArg": "--force-all-checks",
        "_forceAllChecksNodeosDefault": false,
        "_genesisJsonNodeosArg": "--genesis-json",
        "_genesisJsonNodeosDefault": null,
        "_genesisTimestampNodeosArg": "--genesis-timestamp",
        "_genesisTimestampNodeosDefault": null,
        "_hardReplayBlockchainNodeosArg": "--hard-replay-blockchain",
        "_hardReplayBlockchainNodeosDefault": false,
        "_integrityHashOnStartNodeosArg": "--integrity-hash-on-start",
        "_integrityHashOnStartNodeosDefault": false,
        "_integrityHashOnStopNodeosArg": "--integrity-hash-on-stop",
        "_integrityHashOnStopNodeosDefault": false,
        "_keyBlacklistNodeosArg": "--key-blacklist",
        "_keyBlacklistNodeosDefault": null,
        "_maxNonprivilegedInlineActionSizeNodeosArg": "--max-nonprivileged-inline-action-size",
        "_maxNonprivilegedInlineActionSizeNodeosDefault": 4096,
        "_maximumVariableSignatureLengthNodeosArg": "--maximum-variable-signature-length",
        "_maximumVariableSignatureLengthNodeosDefault": 16384,
        "_pluginName": "chain_plugin",
        "_pluginNamespace": "eosio",
        "_printBuildInfoNodeosArg": "--print-build-info",
        "_printBuildInfoNodeosDefault": false,
        "_printGenesisJsonNodeosArg": "--print-genesis-json",
        "_printGenesisJsonNodeosDefault": false,
        "_profileAccountNodeosArg": "--profile-account",
        "_profileAccountNodeosDefault": null,
        "_protocolFeaturesDirNodeosArg": "--protocol-features-dir",
        "_protocolFeaturesDirNodeosDefault": "\"protocol_features\"",
        "_readModeNodeosArg": "--read-mode",
        "_readModeNodeosDefault": "head",
        "_replayBlockchainNodeosArg": "--replay-blockchain",
        "_replayBlockchainNodeosDefault": false,
        "_senderBypassWhiteblacklistNodeosArg": "--sender-bypass-whiteblacklist",
        "_senderBypassWhiteblacklistNodeosDefault": null,
        "_signatureCpuBillablePctNodeosArg": "--signature-cpu-billable-pct",
        "_signatureCpuBillablePctNodeosDefault": 50,
        "_snapshotNodeosArg": "--snapshot",
        "_snapshotNodeosDefault": null,
        "_stateDirNodeosArg": "--state-dir",
        "_stateDirNodeosDefault": "\"state\"",
        "_terminateAtBlockNodeosArg": "--terminate-at-block",
        "_terminateAtBlockNodeosDefault": 0,
        "_transactionFinalityStatusFailureDurationSecNodeosArg": "--transaction-finality-status-failure-duration-sec",
        "_transactionFinalityStatusFailureDurationSecNodeosDefault": 180,
        "_transactionFinalityStatusMaxStorageSizeGbNodeosArg": "--transaction-finality-status-max-storage-size-gb",
        "_transactionFinalityStatusMaxStorageSizeGbNodeosDefault": null,
        "_transactionFinalityStatusSuccessDurationSecNodeosArg": "--transaction-finality-status-success-duration-sec",
        "_transactionFinalityStatusSuccessDurationSecNodeosDefault": 180,
        "_transactionRetryIntervalSecNodeosArg": "--transaction-retry-interval-sec",
        "_transactionRetryIntervalSecNodeosDefault": 20,
        "_transactionRetryMaxExpirationSecNodeosArg": "--transaction-retry-max-expiration-sec",
        "_transactionRetryMaxExpirationSecNodeosDefault": 120,
        "_transactionRetryMaxStorageSizeGbNodeosArg": "--transaction-retry-max-storage-size-gb",
        "_transactionRetryMaxStorageSizeGbNodeosDefault": null,
        "_truncateAtBlockNodeosArg": "--truncate-at-block",
        "_truncateAtBlockNodeosDefault": 0,
        "_trustedProducerNodeosArg": "--trusted-producer",
        "_trustedProducerNodeosDefault": null,
        "_validationModeNodeosArg": "--validation-mode",
        "_validationModeNodeosDefault": "full",
        "_wasmRuntimeNodeosArg": "--wasm-runtime",
        "_wasmRuntimeNodeosDefault": "eos-vm-jit",
        "abiSerializerMaxTimeMs": null,
        "actionBlacklist": null,
        "actorBlacklist": null,
        "actorWhitelist": null,
        "apiAcceptTransactions": null,
        "blockLogRetainBlocks": null,
        "blocksDir": null,
        "chainStateDbGuardSizeMb": null,
        "chainStateDbSizeMb": 10240,
        "chainThreads": 2,
        "checkpoint": null,
        "contractBlacklist": null,
        "contractWhitelist": null,
        "contractsConsole": null,
        "databaseMapMode": "mapped",
        "deepMind": null,
        "deleteAllBlocks": null,
        "disableRamBillingNotifyChecks": null,
        "disableReplayOpts": null,
        "enableAccountQueries": null,
        "eosVmOcCacheSizeMb": null,
        "eosVmOcCompileThreads": null,
        "eosVmOcEnable": null,
        "extractBuildInfo": null,
        "extractGenesisJson": null,
        "forceAllChecks": null,
        "genesisJson": null,
        "genesisTimestamp": null,
        "hardReplayBlockchain": null,
        "integrityHashOnStart": null,
        "integrityHashOnStop": null,
        "keyBlacklist": null,
        "maxNonprivilegedInlineActionSize": null,
        "maximumVariableSignatureLength": null,
        "printBuildInfo": null,
        "printGenesisJson": null,
        "profileAccount": null,
        "protocolFeaturesDir": null,
        "readMode": null,
        "replayBlockchain": null,
        "senderBypassWhiteblacklist": null,
        "signatureCpuBillablePct": 0,
        "snapshot": null,
        "stateDir": null,
        "terminateAtBlock": null,
        "transactionFinalityStatusFailureDurationSec": null,
        "transactionFinalityStatusMaxStorageSizeGb": null,
        "transactionFinalityStatusSuccessDurationSec": null,
        "transactionRetryIntervalSec": null,
        "transactionRetryMaxExpirationSec": null,
        "transactionRetryMaxStorageSizeGb": null,
        "truncateAtBlock": null,
        "trustedProducer": null,
        "validationMode": null,
        "wasmRuntime": null
      },
      "httpClientPluginArgs": {
        "_httpsClientRootCertNodeosArg": "--https-client-root-cert",
        "_httpsClientRootCertNodeosDefault": null,
        "_httpsClientValidatePeersNodeosArg": "--https-client-validate-peers",
        "_httpsClientValidatePeersNodeosDefault": 1,
        "_pluginName": "http_client_plugin",
        "_pluginNamespace": "eosio",
        "httpsClientRootCert": null,
        "httpsClientValidatePeers": null
      },
      "httpPluginArgs": {
        "_accessControlAllowCredentialsNodeosArg": "--access-control-allow-credentials",
        "_accessControlAllowCredentialsNodeosDefault": false,
        "_accessControlAllowHeadersNodeosArg": "--access-control-allow-headers",
        "_accessControlAllowHeadersNodeosDefault": null,
        "_accessControlAllowOriginNodeosArg": "--access-control-allow-origin",
        "_accessControlAllowOriginNodeosDefault": null,
        "_accessControlMaxAgeNodeosArg": "--access-control-max-age",
        "_accessControlMaxAgeNodeosDefault": null,
        "_httpAliasNodeosArg": "--http-alias",
        "_httpAliasNodeosDefault": null,
        "_httpKeepAliveNodeosArg": "--http-keep-alive",
        "_httpKeepAliveNodeosDefault": 1,
        "_httpMaxBytesInFlightMbNodeosArg": "--http-max-bytes-in-flight-mb",
        "_httpMaxBytesInFlightMbNodeosDefault": 500,
        "_httpMaxInFlightRequestsNodeosArg": "--http-max-in-flight-requests",
        "_httpMaxInFlightRequestsNodeosDefault": -1,
        "_httpMaxResponseTimeMsNodeosArg": "--http-max-response-time-ms",
        "_httpMaxResponseTimeMsNodeosDefault": 30,
        "_httpServerAddressNodeosArg": "--http-server-address",
        "_httpServerAddressNodeosDefault": "127.0.0.1:8888",
        "_httpThreadsNodeosArg": "--http-threads",
        "_httpThreadsNodeosDefault": 2,
        "_httpValidateHostNodeosArg": "--http-validate-host",
        "_httpValidateHostNodeosDefault": 1,
        "_httpsCertificateChainFileNodeosArg": "--https-certificate-chain-file",
        "_httpsCertificateChainFileNodeosDefault": null,
        "_httpsEcdhCurveNodeosArg": "--https-ecdh-curve",
        "_httpsEcdhCurveNodeosDefault": "secp384r1",
        "_httpsPrivateKeyFileNodeosArg": "--https-private-key-file",
        "_httpsPrivateKeyFileNodeosDefault": null,
        "_httpsServerAddressNodeosArg": "--https-server-address",
        "_httpsServerAddressNodeosDefault": null,
        "_maxBodySizeNodeosArg": "--max-body-size",
        "_maxBodySizeNodeosDefault": 2097152,
        "_pluginName": "http_plugin",
        "_pluginNamespace": "eosio",
        "_unixSocketPathNodeosArg": "--unix-socket-path",
        "_unixSocketPathNodeosDefault": null,
        "_verboseHttpErrorsNodeosArg": "--verbose-http-errors",
        "_verboseHttpErrorsNodeosDefault": false,
        "accessControlAllowCredentials": null,
        "accessControlAllowHeaders": null,
        "accessControlAllowOrigin": null,
        "accessControlMaxAge": null,
        "httpAlias": null,
        "httpKeepAlive": null,
        "httpMaxBytesInFlightMb": null,
        "httpMaxInFlightRequests": null,
        "httpMaxResponseTimeMs": 990000,
        "httpServerAddress": null,
        "httpThreads": null,
        "httpValidateHost": null,
        "httpsCertificateChainFile": null,
        "httpsEcdhCurve": null,
        "httpsPrivateKeyFile": null,
        "httpsServerAddress": null,
        "maxBodySize": null,
        "unixSocketPath": null,
        "verboseHttpErrors": null
      },
      "netPluginArgs": {
        "_agentNameNodeosArg": "--agent-name",
        "_agentNameNodeosDefault": "EOS Test Agent",
        "_allowedConnectionNodeosArg": "--allowed-connection",
        "_allowedConnectionNodeosDefault": "any",
        "_connectionCleanupPeriodNodeosArg": "--connection-cleanup-period",
        "_connectionCleanupPeriodNodeosDefault": 30,
        "_maxCleanupTimeMsecNodeosArg": "--max-cleanup-time-msec",
        "_maxCleanupTimeMsecNodeosDefault": 10,
        "_maxClientsNodeosArg": "--max-clients",
        "_maxClientsNodeosDefault": 25,
        "_netThreadsNodeosArg": "--net-threads",
        "_netThreadsNodeosDefault": 2,
        "_p2pAcceptTransactionsNodeosArg": "--p2p-accept-transactions",
        "_p2pAcceptTransactionsNodeosDefault": 1,
        "_p2pDedupCacheExpireTimeSecNodeosArg": "--p2p-dedup-cache-expire-time-sec",
        "_p2pDedupCacheExpireTimeSecNodeosDefault": 10,
        "_p2pKeepaliveIntervalMsNodeosArg": "--p2p-keepalive-interval-ms",
        "_p2pKeepaliveIntervalMsNodeosDefault": 10000,
        "_p2pListenEndpointNodeosArg": "--p2p-listen-endpoint",
        "_p2pListenEndpointNodeosDefault": "0.0.0.0:9876",
        "_p2pMaxNodesPerHostNodeosArg": "--p2p-max-nodes-per-host",
        "_p2pMaxNodesPerHostNodeosDefault": 1,
        "_p2pPeerAddressNodeosArg": "--p2p-peer-address",
        "_p2pPeerAddressNodeosDefault": null,
        "_p2pServerAddressNodeosArg": "--p2p-server-address",
        "_p2pServerAddressNodeosDefault": null,
        "_peerKeyNodeosArg": "--peer-key",
        "_peerKeyNodeosDefault": null,
        "_peerLogFormatNodeosArg": "--peer-log-format",
        "_peerLogFormatNodeosDefault": "[\"${_name}\" - ${_cid} ${_ip}:${_port}] ",
        "_peerPrivateKeyNodeosArg": "--peer-private-key",
        "_peerPrivateKeyNodeosDefault": null,
        "_pluginName": "net_plugin",
        "_pluginNamespace": "eosio",
        "_syncFetchSpanNodeosArg": "--sync-fetch-span",
        "_syncFetchSpanNodeosDefault": 100,
        "_useSocketReadWatermarkNodeosArg": "--use-socket-read-watermark",
        "_useSocketReadWatermarkNodeosDefault": 0,
        "agentName": null,
        "allowedConnection": null,
        "connectionCleanupPeriod": null,
        "maxCleanupTimeMsec": null,
        "maxClients": null,
        "netThreads": 2,
        "p2pAcceptTransactions": null,
        "p2pDedupCacheExpireTimeSec": null,
        "p2pKeepaliveIntervalMs": null,
        "p2pListenEndpoint": null,
        "p2pMaxNodesPerHost": null,
        "p2pPeerAddress": null,
        "p2pServerAddress": null,
        "peerKey": null,
        "peerLogFormat": null,
        "peerPrivateKey": null,
        "syncFetchSpan": null,
        "useSocketReadWatermark": null
      },
      "producerPluginArgs": {
        "_cpuEffortPercentNodeosArg": "--cpu-effort-percent",
        "_cpuEffortPercentNodeosDefault": 80,
        "_disableSubjectiveAccountBillingNodeosArg": "--disable-subjective-account-billing",
        "_disableSubjectiveAccountBillingNodeosDefault": false,
        "_disableSubjectiveApiBillingNodeosArg": "--disable-subjective-api-billing",
        "_disableSubjectiveApiBillingNodeosDefault": 1,
        "_disableSubjectiveBillingNodeosArg": "--disable-subjective-billing",
        "_disableSubjectiveBillingNodeosDefault": 1,
        "_disableSubjectiveP2pBillingNodeosArg": "--disable-subjective-p2p-billing",
        "_disableSubjectiveP2pBillingNodeosDefault": 1,
        "_enableStaleProductionNodeosArg": "--enable-stale-production",
        "_enableStaleProductionNodeosDefault": false,
        "_greylistAccountNodeosArg": "--greylist-account",
        "_greylistAccountNodeosDefault": null,
        "_greylistLimitNodeosArg": "--greylist-limit",
        "_greylistLimitNodeosDefault": 1000,
        "_incomingDeferRatioNodeosArg": "--incoming-defer-ratio",
        "_incomingDeferRatioNodeosDefault": 1,
        "_incomingTransactionQueueSizeMbNodeosArg": "--incoming-transaction-queue-size-mb",
        "_incomingTransactionQueueSizeMbNodeosDefault": 1024,
        "_lastBlockCpuEffortPercentNodeosArg": "--last-block-cpu-effort-percent",
        "_lastBlockCpuEffortPercentNodeosDefault": 80,
        "_lastBlockTimeOffsetUsNodeosArg": "--last-block-time-offset-us",
        "_lastBlockTimeOffsetUsNodeosDefault": -200000,
        "_maxBlockCpuUsageThresholdUsNodeosArg": "--max-block-cpu-usage-threshold-us",
        "_maxBlockCpuUsageThresholdUsNodeosDefault": 5000,
        "_maxBlockNetUsageThresholdBytesNodeosArg": "--max-block-net-usage-threshold-bytes",
        "_maxBlockNetUsageThresholdBytesNodeosDefault": 1024,
        "_maxIrreversibleBlockAgeNodeosArg": "--max-irreversible-block-age",
        "_maxIrreversibleBlockAgeNodeosDefault": -1,
        "_maxScheduledTransactionTimePerBlockMsNodeosArg": "--max-scheduled-transaction-time-per-block-ms",
        "_maxScheduledTransactionTimePerBlockMsNodeosDefault": 100,
        "_maxTransactionTimeNodeosArg": "--max-transaction-time",
        "_maxTransactionTimeNodeosDefault": 30,
        "_pauseOnStartupNodeosArg": "--pause-on-startup",
        "_pauseOnStartupNodeosDefault": false,
        "_pluginName": "producer_plugin",
        "_pluginNamespace": "eosio",
        "_privateKeyNodeosArg": "--private-key",
        "_privateKeyNodeosDefault": null,
        "_produceTimeOffsetUsNodeosArg": "--produce-time-offset-us",
        "_produceTimeOffsetUsNodeosDefault": 0,
        "_producerNameNodeosArg": "--producer-name",
        "_producerNameNodeosDefault": null,
        "_producerThreadsNodeosArg": "--producer-threads",
        "_producerThreadsNodeosDefault": 2,
        "_signatureProviderNodeosArg": "--signature-provider",
        "_signatureProviderNodeosDefault": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV=KEY:5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3",
        "_snapshotsDirNodeosArg": "--snapshots-dir",
        "_snapshotsDirNodeosDefault": "\"snapshots\"",
        "_subjectiveAccountDecayTimeMinutesNodeosArg": "--subjective-account-decay-time-minutes",
        "_subjectiveAccountDecayTimeMinutesNodeosDefault": 1440,
        "_subjectiveAccountMaxFailuresNodeosArg": "--subjective-account-max-failures",
        "_subjectiveAccountMaxFailuresNodeosDefault": 3,
        "_subjectiveCpuLeewayUsNodeosArg": "--subjective-cpu-leeway-us",
        "_subjectiveCpuLeewayUsNodeosDefault": 31000,
        "cpuEffortPercent": 80,
        "disableSubjectiveAccountBilling": null,
        "disableSubjectiveApiBilling": null,
        "disableSubjectiveBilling": true,
        "disableSubjectiveP2pBilling": null,
        "enableStaleProduction": null,
        "greylistAccount": null,
        "greylistLimit": null,
        "incomingDeferRatio": null,
        "incomingTransactionQueueSizeMb": null,
        "lastBlockCpuEffortPercent": 80,
        "lastBlockTimeOffsetUs": -200000,
        "maxBlockCpuUsageThresholdUs": null,
        "maxBlockNetUsageThresholdBytes": null,
        "maxIrreversibleBlockAge": null,
        "maxScheduledTransactionTimePerBlockMs": null,
        "maxTransactionTime": null,
        "pauseOnStartup": null,
        "privateKey": null,
        "produceTimeOffsetUs": -200000,
        "producerName": null,
        "producerThreads": 2,
        "signatureProvider": null,
        "snapshotsDir": null,
        "subjectiveAccountDecayTimeMinutes": null,
        "subjectiveAccountMaxFailures": null,
        "subjectiveCpuLeewayUs": null
      },
      "resourceMonitorPluginArgs": {
        "_pluginName": "resource_monitor_plugin",
        "_pluginNamespace": "eosio",
        "_resourceMonitorIntervalSecondsNodeosArg": "--resource-monitor-interval-seconds",
        "_resourceMonitorIntervalSecondsNodeosDefault": 2,
        "_resourceMonitorNotShutdownOnThresholdExceededNodeosArg": "--resource-monitor-not-shutdown-on-threshold-exceeded",
        "_resourceMonitorNotShutdownOnThresholdExceededNodeosDefault": false,
        "_resourceMonitorSpaceThresholdNodeosArg": "--resource-monitor-space-threshold",
        "_resourceMonitorSpaceThresholdNodeosDefault": 90,
        "_resourceMonitorWarningIntervalNodeosArg": "--resource-monitor-warning-interval",
        "_resourceMonitorWarningIntervalNodeosDefault": 30,
        "resourceMonitorIntervalSeconds": null,
        "resourceMonitorNotShutdownOnThresholdExceeded": null,
        "resourceMonitorSpaceThreshold": null,
        "resourceMonitorWarningInterval": null
      },
      "signatureProviderPluginArgs": {
        "_keosdProviderTimeoutNodeosArg": "--keosd-provider-timeout",
        "_keosdProviderTimeoutNodeosDefault": 5,
        "_pluginName": "signature_provider_plugin",
        "_pluginNamespace": "eosio",
        "keosdProviderTimeout": null
      },
      "stateHistoryPluginArgs": {
        "_chainStateHistoryNodeosArg": "--chain-state-history",
        "_chainStateHistoryNodeosDefault": false,
        "_deleteStateHistoryNodeosArg": "--delete-state-history",
        "_deleteStateHistoryNodeosDefault": false,
        "_pluginName": "state_history_plugin",
        "_pluginNamespace": "eosio",
        "_stateHistoryDirNodeosArg": "--state-history-dir",
        "_stateHistoryDirNodeosDefault": "\"state-history\"",
        "_stateHistoryEndpointNodeosArg": "--state-history-endpoint",
        "_stateHistoryEndpointNodeosDefault": "127.0.0.1:8080",
        "_stateHistoryLogRetainBlocksNodeosArg": "--state-history-log-retain-blocks",
        "_stateHistoryLogRetainBlocksNodeosDefault": null,
        "_stateHistoryUnixSocketPathNodeosArg": "--state-history-unix-socket-path",
        "_stateHistoryUnixSocketPathNodeosDefault": null,
        "_traceHistoryDebugModeNodeosArg": "--trace-history-debug-mode",
        "_traceHistoryDebugModeNodeosDefault": false,
        "_traceHistoryNodeosArg": "--trace-history",
        "_traceHistoryNodeosDefault": false,
        "chainStateHistory": null,
        "deleteStateHistory": null,
        "stateHistoryDir": null,
        "stateHistoryEndpoint": null,
        "stateHistoryLogRetainBlocks": null,
        "stateHistoryUnixSocketPath": null,
        "traceHistory": null,
        "traceHistoryDebugMode": null
      },
      "traceApiPluginArgs": {
        "_pluginName": "trace_api_plugin",
        "_pluginNamespace": "eosio",
        "_traceDirNodeosArg": "--trace-dir",
        "_traceDirNodeosDefault": "\"traces\"",
        "_traceMinimumIrreversibleHistoryBlocksNodeosArg": "--trace-minimum-irreversible-history-blocks",
        "_traceMinimumIrreversibleHistoryBlocksNodeosDefault": -1,
        "_traceMinimumUncompressedIrreversibleHistoryBlocksNodeosArg": "--trace-minimum-uncompressed-irreversible-history-blocks",
        "_traceMinimumUncompressedIrreversibleHistoryBlocksNodeosDefault": -1,
        "_traceNoAbisNodeosArg": "--trace-no-abis",
        "_traceNoAbisNodeosDefault": false,
        "_traceRpcAbiNodeosArg": "--trace-rpc-abi",
        "_traceRpcAbiNodeosDefault": null,
        "_traceSliceStrideNodeosArg": "--trace-slice-stride",
        "_traceSliceStrideNodeosDefault": 10000,
        "traceDir": null,
        "traceMinimumIrreversibleHistoryBlocks": null,
        "traceMinimumUncompressedIrreversibleHistoryBlocks": null,
        "traceNoAbis": null,
        "traceRpcAbi": null,
        "traceSliceStride": null
      }
    },
    "genesisPath": "tests/performance_tests/genesis.json",
    "keepLogs": true,
    "killAll": true,
    "logDirBase": "p",
    "logDirPath": "p/2023-01-11_20-01-22-20",
    "logDirRoot": ".",
    "logDirTimestamp": "2023-01-11_20-01-22",
    "logDirTimestampedOptSuffix": "-20",
    "loggingDict": {
      "bios": "off"
    },
    "maximumClients": 0,
    "maximumP2pPerHost": 5000,
    "nodeosVers": "v4",
    "nodesFile": null,
    "numAddlBlocksToPrune": 2,
    "pnodes": 1,
    "printMissingTransactions": false,
    "prodsEnableTraceApi": false,
    "quiet": false,
    "specificExtraNodeosArgs": {
      "1": "--plugin eosio::trace_api_plugin"
    },
    "targetTps": 20,
    "testTrxGenDurationSec": 90,
    "topo": "mesh",
    "totalNodes": 1,
    "tpsLimitPerGenerator": 10,
    "useBiosBootFile": false,
    "verbose": true
  },
  "completedRun": true,
  "env": {
    "logical_cpu_count": 16,
    "os": "posix",
    "release": "5.10.16.3-microsoft-standard-WSL2",
    "system": "Linux"
  },
  "nodeosVersion": "v4.0.0-dev",
  "testFinish": "2023-01-11T20:03:53.210082",
  "testStart": "2023-01-11T20:01:22.768422"
}
```
</details>
