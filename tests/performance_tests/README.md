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
* `--http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS`
                          Maximum time for processing a request, -1 for unlimited (default: 990000)
* `--del-perf-logs`       Whether to delete performance test specific logs. (default: False)
* `--del-report`          Whether to delete overarching performance run report. (default: False)
* `--del-test-report`     Whether to save json reports from each test scenario. (default: False)
* `--quiet`               Whether to quiet printing intermediate results and reports to stdout (default: False)
* `--prods-enable-trace-api`
                          Determines whether producer nodes should have eosio::trace_api_plugin enabled (default: False)
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
* `--http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS`
                          Maximum time for processing a request, -1 for unlimited (default: 990000)
* `--del-perf-logs`       Whether to delete performance test specific logs. (default: False)
* `--del-report`          Whether to delete overarching performance run report. (default: False)
* `--quiet`               Whether to quiet printing intermediate results and reports to stdout (default: False)
* `--prods-enable-trace-api`
                          Determines whether producer nodes should have eosio::trace_api_plugin enabled (default: False)
</details>

#### Launch Transaction Generators (TestHarness)

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
.build/tests/performance_tests/performance_test.py --test-iteration-duration-sec 10 --final-iterations-duration-sec 30
```

#### Report Breakdown
The report begins by delivering the max TPS results of the performance run.

* `InitialMaxTpsAchieved` - the max TPS throughput achieved during initial, short duration test scenarios to narrow search window
* `LongRunningMaxTpsAchieved` - the max TPS throughput achieved during final, longer duration test scenarios to zero in on sustainable max TPS

Next, a summary of the search scenario conducted and respective results is included.  Each summary includes information on the current state of the overarching search as well as basic results of the individual test that are used to determine whether the basic test was considered successful. The list of summary results are included in `InitialSearchResults` and `LongRunningSearchResults`. The number of entries in each list will vary depending on the TPS range tested (`--max-tps-to-test`) and the configured `--test-iteration-min-step`.
<details>
    <summary>Expand Search Scenario Summary Example</summary>

``` json
    "0": {
      "success": false,
      "searchTarget": 25000,
      "searchFloor": 0,
      "searchCeiling": 50000,
      "basicTestResult": {
        "targetTPS": 25000,
        "resultAvgTps": 17160.4,
        "expectedTxns": 250000,
        "resultTxns": 250000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 26,
        "logsDir": "performance_test/2022-10-26_15-01-51/testRunLogs/performance_test_basic/2022-10-26_15-01-51",
        "testStart": "2022-10-26T15:03:37.764242",
        "testEnd": "2022-10-26T15:01:51.128328"
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
  "InitialMaxTpsAchieved": 16000,
  "LongRunningMaxTpsAchieved": 15000,
  "testStart": "2022-11-04T19:31:40.539240",
  "testFinish": "2022-11-04T19:48:53.096915",
  "InitialSearchResults": {
    "0": {
      "success": false,
      "searchTarget": 50000,
      "searchFloor": 0,
      "searchCeiling": 50000,
      "basicTestResult": {
        "targetTPS": 50000,
        "resultAvgTps": 15312.09090909091,
        "expectedTxns": 500000,
        "resultTxns": 362075,
        "tpsExpectMet": false,
        "trxExpectMet": false,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 45,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-31-40",
        "testStart": "2022-11-04T19:31:40.539927",
        "testEnd": "2022-11-04T19:33:16.377065"
      }
    },
    "1": {
      "success": false,
      "searchTarget": 25000,
      "searchFloor": 0,
      "searchCeiling": 49500,
      "basicTestResult": {
        "targetTPS": 25000,
        "resultAvgTps": 15098.241379310344,
        "expectedTxns": 250000,
        "resultTxns": 250000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 30,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-33-16",
        "testStart": "2022-11-04T19:33:16.471198",
        "testEnd": "2022-11-04T19:34:45.441319"
      }
    },
    "2": {
      "success": true,
      "searchTarget": 12500,
      "searchFloor": 0,
      "searchCeiling": 24500,
      "basicTestResult": {
        "targetTPS": 12500,
        "resultAvgTps": 12500.0625,
        "expectedTxns": 125000,
        "resultTxns": 125000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-34-45",
        "testStart": "2022-11-04T19:34:45.507994",
        "testEnd": "2022-11-04T19:36:01.234060"
      }
    },
    "3": {
      "success": false,
      "searchTarget": 19000,
      "searchFloor": 13000,
      "searchCeiling": 24500,
      "basicTestResult": {
        "targetTPS": 19000,
        "resultAvgTps": 15454.0,
        "expectedTxns": 190000,
        "resultTxns": 190000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 22,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-36-01",
        "testStart": "2022-11-04T19:36:01.277926",
        "testEnd": "2022-11-04T19:37:23.028124"
      }
    },
    "4": {
      "success": true,
      "searchTarget": 16000,
      "searchFloor": 13000,
      "searchCeiling": 18500,
      "basicTestResult": {
        "targetTPS": 16000,
        "resultAvgTps": 15900.625,
        "expectedTxns": 160000,
        "resultTxns": 160000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-37-23",
        "testStart": "2022-11-04T19:37:23.085923",
        "testEnd": "2022-11-04T19:38:41.744418"
      }
    },
    "5": {
      "success": false,
      "searchTarget": 17500,
      "searchFloor": 16500,
      "searchCeiling": 18500,
      "basicTestResult": {
        "targetTPS": 17500,
        "resultAvgTps": 15271.526315789473,
        "expectedTxns": 175000,
        "resultTxns": 175000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 20,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-38-41",
        "testStart": "2022-11-04T19:38:41.796745",
        "testEnd": "2022-11-04T19:40:02.097920"
      }
    },
    "6": {
      "success": false,
      "searchTarget": 17000,
      "searchFloor": 16500,
      "searchCeiling": 17000,
      "basicTestResult": {
        "targetTPS": 17000,
        "resultAvgTps": 15876.176470588236,
        "expectedTxns": 170000,
        "resultTxns": 170000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 18,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-40-02",
        "testStart": "2022-11-04T19:40:02.150305",
        "testEnd": "2022-11-04T19:41:21.802272"
      }
    },
    "7": {
      "success": false,
      "searchTarget": 16500,
      "searchFloor": 16500,
      "searchCeiling": 16500,
      "basicTestResult": {
        "targetTPS": 16500,
        "resultAvgTps": 16096.823529411764,
        "expectedTxns": 165000,
        "resultTxns": 165000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 18,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-41-21",
        "testStart": "2022-11-04T19:41:21.851918",
        "testEnd": "2022-11-04T19:42:40.991794"
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
      "searchTarget": 16000,
      "searchFloor": 0,
      "searchCeiling": 16000,
      "basicTestResult": {
        "targetTPS": 16000,
        "resultAvgTps": 14954.266666666666,
        "expectedTxns": 480000,
        "resultTxns": 480000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 61,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-42-41",
        "testStart": "2022-11-04T19:42:41.051468",
        "testEnd": "2022-11-04T19:44:47.365905"
      }
    },
    "1": {
      "success": false,
      "searchTarget": 15500,
      "searchFloor": 0,
      "searchCeiling": 16000,
      "basicTestResult": {
        "targetTPS": 15500,
        "resultAvgTps": 15001.827586206897,
        "expectedTxns": 465000,
        "resultTxns": 465000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 59,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-44-47",
        "testStart": "2022-11-04T19:44:47.472961",
        "testEnd": "2022-11-04T19:46:52.818564"
      }
    },
    "2": {
      "success": true,
      "searchTarget": 15000,
      "searchFloor": 0,
      "searchCeiling": 16000,
      "basicTestResult": {
        "targetTPS": 15000,
        "resultAvgTps": 15023.464285714286,
        "expectedTxns": 450000,
        "resultTxns": 450000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 57,
        "logsDir": "performance_test/2022-11-04_19-31-40/testRunLogs/performance_test_basic/2022-11-04_19-46-52",
        "testStart": "2022-11-04T19:46:52.960531",
        "testEnd": "2022-11-04T19:48:52.989694"
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
        "signatureCpuBillablePct": 0
      },
      "producerPluginArgs": {
        "disableSubjectiveBilling": true,
        "lastBlockTimeOffsetUs": 0,
        "produceTimeOffsetUs": 0,
        "cpuEffortPercent": 100,
        "lastBlockCpuEffortPercent": 100
      },
      "httpPluginArgs": {
        "httpMaxResponseTimeMs": 990000
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
    "logsDir": "performance_test/2022-11-04_19-31-40",
    "maxTpsToTest": 50000,
    "testIterationMinStep": 500,
    "tpsLimitPerGenerator": 4000,
    "delReport": false,
    "delTestReport": false,
    "numAddlBlocksToPrune": 2,
    "quiet": false,
    "delPerfLogs": false
  },
  "env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.10.102.1-microsoft-standard-WSL2"
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
  "testStart": "2022-11-04T19:46:52.960531",
  "testFinish": "2022-11-04T19:48:52.989694",
  "Analysis": {
    "BlockSize": {
      "min": 1389312,
      "max": 1575800,
      "avg": 1474814.3157894737,
      "sigma": 40921.65290309434,
      "emptyBlocks": 0,
      "numBlocks": 57
    },
    "BlocksGuide": {
      "firstBlockNum": 2,
      "lastBlockNum": 232,
      "totalBlocks": 231,
      "testStartBlockNum": 105,
      "testEndBlockNum": 199,
      "setupBlocksCnt": 103,
      "tearDownBlocksCnt": 33,
      "leadingEmptyBlocksCnt": 1,
      "trailingEmptyBlocksCnt": 33,
      "configAddlDropCnt": 2,
      "testAnalysisBlockCnt": 57
    },
    "TPS": {
      "min": 14532,
      "max": 15477,
      "avg": 15023.464285714286,
      "sigma": 178.66938384762454,
      "emptyBlocks": 0,
      "numBlocks": 57,
      "configTps": 15000,
      "configTestDuration": 30,
      "tpsPerGenerator": [
        3750,
        3750,
        3750,
        3750
      ],
      "generatorCount": 4
    },
    "TrxCPU": {
      "min": 7.0,
      "max": 2647.0,
      "avg": 23.146035555555557,
      "sigma": 11.415769514864671,
      "samples": 450000
    },
    "TrxLatency": {
      "min": 0.0009999275207519531,
      "max": 0.5539999008178711,
      "avg": 0.2614889088874393,
      "sigma": 0.1450651327531534,
      "samples": 450000
    },
    "TrxNet": {
      "min": 24.0,
      "max": 25.0,
      "avg": 24.555564444444446,
      "sigma": 0.49690300111146485,
      "samples": 450000
    }
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
        "signatureCpuBillablePct": 0
      },
      "producerPluginArgs": {
        "disableSubjectiveBilling": true,
        "lastBlockTimeOffsetUs": 0,
        "produceTimeOffsetUs": 0,
        "cpuEffortPercent": 100,
        "lastBlockCpuEffortPercent": 100
      },
      "httpPluginArgs": {
        "httpMaxResponseTimeMs": 990000
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
    "delPerfLogs": false,
    "delReport": false,
    "expectedTransactionsSent": 450000,
    "numAddlBlocksToPrune": 2,
    "quiet": false,
    "targetTps": 15000,
    "testTrxGenDurationSec": 30,
    "tpsLimitPerGenerator": 4000
  },
  "env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.10.102.1-microsoft-standard-WSL2",
    "logical_cpu_count": 16
  },
  "nodeosVersion": "v4.0.0-dev"
}
```
</details>
