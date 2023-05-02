# Performance Harness Tests

The Performance Harness is configured and run through the main `performance_test.py` script.  The script's main goal is to measure current peak performance metrics through iteratively tuning and running basic performance tests. The current basic test works to determine the maximum throughput of Token Transfers the system can sustain.  It does this by conducting a binary search of possible Token Transfers Per Second (TPS) configurations, testing each configuration in a short duration test and scoring its result. The search algorithm iteratively configures and runs `performance_test_basic.py` tests and analyzes the output to determine a success metric used to continue the search.  When the search completes, a max TPS throughput value is reported (along with other performance metrics from that run).  The script then proceeds to conduct an additional search with longer duration test runs within a narrowed TPS configuration range to determine the sustainable max TPS. Finally it produces a report on the entire performance run, summarizing each individual test scenario, results, and full report details on the tests when maximum TPS was achieved ([Performance Test Report](#performance-test-report))

The `performance_test_basic.py` support script performs a single basic performance test that targets a configurable TPS target and, if successful, reports statistics on performance metrics measured during the test.  It configures and launches a blockchain test environment, creates wallets and accounts for testing, and configures and launches transaction generators for creating specific transaction load in the ecosystem.  Finally it analyzes the performance of the system under the configuration through log analysis and chain queries and produces a [Performance Test Basic Report](#performance-test-basic-report).

The `launch_generators.py` support script provides a means to easily calculate and spawn the number of transaction generator instances to generate a given target TPS, distributing generation load between the instances in a fair manner such that the aggregate load meets the requested test load.

The `log_reader.py` support script is used primarily to analyze `nodeos` log files to glean information about generated blocks and transactions within those blocks after a test has concluded.  This information is used to produce the performance test report.

# Getting Started
## Prerequisites

Please refer to [Leap: Build and Install from Source](https://github.com/AntelopeIO/leap/#build-and-install-from-source) for a full list of prerequisites.

## Steps

1. Build Leap. For complete instructions on building from source please refer to [Leap: Build and Install from Source](https://github.com/AntelopeIO/leap/#build-and-install-from-source) For older compatible nodeos versions, such as 2.X, the following binaries need to be replaced with the older version: `build/programs/nodeos/nodeos`, `build/programs/cleos/cleos`, `bin/nodeos`, and `bin/cleos`.
2. Run Performance Tests
    1. Full Performance Harness Test Run (Standard):
        ``` bash
        ./build/tests/performance_tests/performance_test.py testBpOpMode
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
        └── 2023-04-05_14-35-59
            ├── pluginThreadOptRunLogs
            │   ├── chainThreadResults.txt
            │   ├── netThreadResults.txt
            │   ├── performance_test
            │   │   ├── 2023-04-05_14-35-59-50000
            │   │   │   ├── blockDataLogs
            │   │   │   │   ├── blockData.txt
            │   │   │   │   ├── blockTrxData.txt
            │   │   │   │   └── transaction_metrics.csv
            │   │   │   ├── etc
            │   │   │   │   └── eosio
            │   │   │   │       ├── node_00
            │   │   │   │       │   ├── config.ini
            │   │   │   │       │   ├── genesis.json
            │   │   │   │       │   ├── logging.json
            │   │   │   │       │   └── protocol_features
            │   │   │   │       │       ├── BUILTIN-ACTION_RETURN_VALUE.json
            │   │   │   │       │       ├── BUILTIN-BLOCKCHAIN_PARAMETERS.json
            │   │   │   │       │       ├── BUILTIN-CONFIGURABLE_WASM_LIMITS2.json
            │   │   │   │       │       ├── BUILTIN-CRYPTO_PRIMITIVES.json
            │   │   │   │       │       ├── BUILTIN-DISALLOW_EMPTY_PRODUCER_SCHEDULE.json
            │   │   │   │       │       ├── BUILTIN-FIX_LINKAUTH_RESTRICTION.json
            │   │   │   │       │       ├── BUILTIN-FORWARD_SETCODE.json
            │   │   │   │       │       ├── BUILTIN-GET_BLOCK_NUM.json
            │   │   │   │       │       ├── BUILTIN-GET_CODE_HASH.json
            │   │   │   │       │       ├── BUILTIN-GET_SENDER.json
            │   │   │   │       │       ├── BUILTIN-NO_DUPLICATE_DEFERRED_ID.json
            │   │   │   │       │       ├── BUILTIN-ONLY_BILL_FIRST_AUTHORIZER.json
            │   │   │   │       │       ├── BUILTIN-ONLY_LINK_TO_EXISTING_PERMISSION.json
            │   │   │   │       │       ├── BUILTIN-PREACTIVATE_FEATURE.json
            │   │   │   │       │       ├── BUILTIN-RAM_RESTRICTIONS.json
            │   │   │   │       │       ├── BUILTIN-REPLACE_DEFERRED.json
            │   │   │   │       │       ├── BUILTIN-RESTRICT_ACTION_TO_SELF.json
            │   │   │   │       │       ├── BUILTIN-WEBAUTHN_KEY.json
            │   │   │   │       │       └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
            │   │   │   │       ├── node_01
            │   │   │   │       │   ├── config.ini
            │   │   │   │       │   ├── genesis.json
            │   │   │   │       │   ├── logging.json
            │   │   │   │       │   └── protocol_features
            │   │   │   │       │       ├── BUILTIN-ACTION_RETURN_VALUE.json
            │   │   │   │       |       .
            │   │   │   │       |       .
            │   │   │   │       |       .
            │   │   │   │       │       └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
            │   │   │   │       └── node_bios
            │   │   │   │           ├── config.ini
            │   │   │   │           ├── genesis.json
            │   │   │   │           ├── logging.json
            │   │   │   │           └── protocol_features
            │   │   │   │               ├── BUILTIN-ACTION_RETURN_VALUE.json
            │   │   │   │               .
            │   │   │   │               .
            │   │   │   │               .
            │   │   │   │               └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
            │   │   │   ├── trxGenLogs
            │   │   │   │   ├── first_trx_9486.txt
            │   │   │   │   .
            │   │   │   │   .
            │   │   │   │   .
            │   │   │   │   ├── first_trx_9498.txt
            │   │   │   │   ├── trx_data_output_9486.txt
            │   │   │   │   .
            │   │   │   │   .
            │   │   │   │   .
            │   │   │   │   └── trx_data_output_9498.txt
            │   │   │   └── var
            │   │   │       └── performance_test8480
            │   │   │           ├── node_00
            │   │   │           │   ├── blocks
            │   │   │           │   │   ├── blocks.index
            │   │   │           │   │   ├── blocks.log
            │   │   │           │   │   └── reversible
            │   │   │           │   ├── nodeos.pid
            │   │   │           │   ├── snapshots
            │   │   │           │   ├── state
            │   │   │           │   │   └── shared_memory.bin
            │   │   │           │   ├── stderr.2023_04_05_09_35_59.txt
            │   │   │           │   ├── stderr.txt -> stderr.2023_04_05_09_35_59.txt
            │   │   │           │   └── stdout.txt
            │   │   │           ├── node_01
            │   │   │           │   ├── blocks
            │   │   │           │   │   ├── blocks.index
            │   │   │           │   │   ├── blocks.log
            │   │   │           │   │   └── reversible
            │   │   │           │   ├── nodeos.pid
            │   │   │           │   ├── snapshots
            │   │   │           │   ├── state
            │   │   │           │   │   └── shared_memory.bin
            │   │   │           │   ├── stderr.2023_04_05_09_35_59.txt
            │   │   │           │   ├── stderr.txt -> stderr.2023_04_05_09_35_59.txt
            │   │   │           │   ├── stdout.txt
            │   │   │           │   └── traces
            │   │   │           │       ├── trace_0000000000-0000010000.log
            │   │   │           │       ├── trace_index_0000000000-0000010000.log
            │   │   │           │       └── trace_trx_id_0000000000-0000010000.log
            │   │   │           └── node_bios
            │   │   │               ├── blocks
            │   │   │               │   ├── blocks.index
            │   │   │               │   ├── blocks.log
            │   │   │               │   └── reversible
            │   │   │               │       └── fork_db.dat
            │   │   │               ├── nodeos.pid
            │   │   │               ├── snapshots
            │   │   │               ├── state
            │   │   │               │   └── shared_memory.bin
            │   │   │               ├── stderr.2023_04_05_09_35_59.txt
            │   │   │               ├── stderr.txt -> stderr.2023_04_05_09_35_59.txt
            │   │   │               ├── stdout.txt
            │   │   │               └── traces
            │   │   │                   ├── trace_0000000000-0000010000.log
            │   │   │                   ├── trace_index_0000000000-0000010000.log
            │   │   │                   └── trace_trx_id_0000000000-0000010000.log
            │   │   ├── 2023-04-05_14-37-31-25001
            │   │   ├── 2023-04-05_14-38-59-12501
            │   │   .
            │   │   .
            │   │   .
            │   │   └── 2023-04-05_16-13-11-13001
            │   └── producerThreadResults.txt
            ├── report.json
            └── testRunLogs
                └── performance_test
                    ├── 2023-04-05_16-14-31-50000
                    │   ├── blockDataLogs
                    │   │   ├── blockData.txt
                    │   │   ├── blockTrxData.txt
                    │   │   └── transaction_metrics.csv
                    │   ├── data.json
                    │   ├── etc
                    │   │   └── eosio
                    │   │       ├── node_00
                    │   │       │   ├── config.ini
                    │   │       │   ├── genesis.json
                    │   │       │   ├── logging.json
                    │   │       │   └── protocol_features
                    │   │       │       ├── BUILTIN-ACTION_RETURN_VALUE.json
                    │   │       │       ├── BUILTIN-BLOCKCHAIN_PARAMETERS.json
                    │   │       │       ├── BUILTIN-CONFIGURABLE_WASM_LIMITS2.json
                    │   │       │       ├── BUILTIN-CRYPTO_PRIMITIVES.json
                    │   │       │       ├── BUILTIN-DISALLOW_EMPTY_PRODUCER_SCHEDULE.json
                    │   │       │       ├── BUILTIN-FIX_LINKAUTH_RESTRICTION.json
                    │   │       │       ├── BUILTIN-FORWARD_SETCODE.json
                    │   │       │       ├── BUILTIN-GET_BLOCK_NUM.json
                    │   │       │       ├── BUILTIN-GET_CODE_HASH.json
                    │   │       │       ├── BUILTIN-GET_SENDER.json
                    │   │       │       ├── BUILTIN-NO_DUPLICATE_DEFERRED_ID.json
                    │   │       │       ├── BUILTIN-ONLY_BILL_FIRST_AUTHORIZER.json
                    │   │       │       ├── BUILTIN-ONLY_LINK_TO_EXISTING_PERMISSION.json
                    │   │       │       ├── BUILTIN-PREACTIVATE_FEATURE.json
                    │   │       │       ├── BUILTIN-RAM_RESTRICTIONS.json
                    │   │       │       ├── BUILTIN-REPLACE_DEFERRED.json
                    │   │       │       ├── BUILTIN-RESTRICT_ACTION_TO_SELF.json
                    │   │       │       ├── BUILTIN-WEBAUTHN_KEY.json
                    │   │       │       └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
                    │   │       ├── node_01
                    │   │       │   ├── config.ini
                    │   │       │   ├── genesis.json
                    │   │       │   ├── logging.json
                    │   │       │   └── protocol_features
                    │   │       │       ├── BUILTIN-ACTION_RETURN_VALUE.json
                    │   │       |       .
                    │   │       |       .
                    │   │       |       .
                    │   │       │       └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
                    │   │       └── node_bios
                    │   │           ├── config.ini
                    │   │           ├── genesis.json
                    │   │           ├── logging.json
                    │   │           └── protocol_features
                    │   │               ├── BUILTIN-ACTION_RETURN_VALUE.json
                    │   │               .
                    │   │               .
                    │   │               .
                    │   │               └── BUILTIN-WTMSIG_BLOCK_SIGNATURES.json
                    │   ├── trxGenLogs
                    │   │   ├── first_trx_20199.txt
                    │   │   .
                    │   │   .
                    │   │   .
                    │   │   ├── first_trx_20211.txt
                    │   │   ├── trx_data_output_20199.txt
                    │   │   .
                    │   │   .
                    │   │   .
                    │   │   └── trx_data_output_20211.txt
                    │   └── var
                    │       └── performance_test8480
                    │           ├── node_00
                    │           │   ├── blocks
                    │           │   │   ├── blocks.index
                    │           │   │   ├── blocks.log
                    │           │   │   └── reversible
                    │           │   ├── nodeos.pid
                    │           │   ├── snapshots
                    │           │   ├── state
                    │           │   │   └── shared_memory.bin
                    │           │   ├── stderr.2023_04_05_11_14_31.txt
                    │           │   ├── stderr.txt -> stderr.2023_04_05_11_14_31.txt
                    │           │   └── stdout.txt
                    │           ├── node_01
                    │           │   ├── blocks
                    │           │   │   ├── blocks.index
                    │           │   │   ├── blocks.log
                    │           │   │   └── reversible
                    │           │   ├── nodeos.pid
                    │           │   ├── snapshots
                    │           │   ├── state
                    │           │   │   └── shared_memory.bin
                    │           │   ├── stderr.2023_04_05_11_14_31.txt
                    │           │   ├── stderr.txt -> stderr.2023_04_05_11_14_31.txt
                    │           │   ├── stdout.txt
                    │           │   └── traces
                    │           │       ├── trace_0000000000-0000010000.log
                    │           │       ├── trace_index_0000000000-0000010000.log
                    │           │       └── trace_trx_id_0000000000-0000010000.log
                    │           └── node_bios
                    │               ├── blocks
                    │               │   ├── blocks.index
                    │               │   ├── blocks.log
                    │               │   └── reversible
                    │               │       └── fork_db.dat
                    │               ├── nodeos.pid
                    │               ├── snapshots
                    │               ├── state
                    │               │   └── shared_memory.bin
                    │               ├── stderr.2023_04_05_11_14_31.txt
                    │               ├── stderr.txt -> stderr.2023_04_05_11_14_31.txt
                    │               ├── stdout.txt
                    │               └── traces
                    │                   ├── trace_0000000000-0000010000.log
                    │                   ├── trace_index_0000000000-0000010000.log
                    │                   └── trace_trx_id_0000000000-0000010000.log
                    ├── 2023-04-05_16-16-03-25001
                    ├── 2023-04-05_16-17-34-12501
                    .
                    .
                    .
                    └── 2023-04-05_16-25-39-13501
        ```
        </details>

# Configuring Performance Harness Tests

## Performance Test

The Performance Harness main script `performance_test.py` can be configured using the following command line arguments:

<details open>
    <summary>Usage</summary>

```
usage: performance_test.py [-h] {testBpOpMode} ...
```

</details>

<details open>
    <summary>Expand Operational Mode Sub-Command List</summary>

```
optional arguments:
-h, --help      show this help message and exit

Operational Modes:
  Each Operational Mode sets up a known node operator configuration and
  performs load testing and analysis catered to the expectations of that
  specific operational mode. For additional configuration options for each
  operational mode use, pass --help to the sub-command. Eg:
  performance_test.py testBpOpMode --help

  {testBpOpMode}  Currently supported operational mode sub-commands.
    testBpOpMode  Test the Block Producer Operational Mode.
```

</details>

### Operational Modes

#### Block Producer Mode

<details>
    <summary>Usage</summary>

```
usage: performance_test.py testBpOpMode [--skip-tps-test]
                                        [--calc-producer-threads {none,lmax,full}]
                                        [--calc-chain-threads {none,lmax,full}]
                                        [--calc-net-threads {none,lmax,full}]
                                        [--del-test-report]
                                        [--max-tps-to-test MAX_TPS_TO_TEST]
                                        [--test-iteration-duration-sec TEST_ITERATION_DURATION_SEC]
                                        [--test-iteration-min-step TEST_ITERATION_MIN_STEP]
                                        [--final-iterations-duration-sec FINAL_ITERATIONS_DURATION_SEC]
                                        [-h]
                                        {overrideBasicTestConfig} ...
```

</details>

<details>
    <summary>Expand Block Producer Mode Argument List</summary>

```
optional arguments:
  -h, --help            show this help message and exit

Performance Harness:
  Performance Harness testing configuration items.

  --skip-tps-test       Determines whether to skip the max TPS measurement
                        tests
  --calc-producer-threads {none,lmax,full}
                        Determines whether to calculate number of worker
                        threads to use in producer thread pool ("none",
                        "lmax", or "full"). In "none" mode, the default, no
                        calculation will be attempted and the configured
                        --producer-threads value will be used. In "lmax" mode,
                        producer threads will incrementally be tested,
                        starting at plugin default, until the performance rate
                        ceases to increase with the addition of additional
                        threads. In "full" mode producer threads will
                        incrementally be tested from plugin default..num
                        logical processors, recording each performance and
                        choosing the local max performance (same value as
                        would be discovered in "lmax" mode). Useful for
                        graphing the full performance impact of each available
                        thread.
  --calc-chain-threads {none,lmax,full}
                        Determines whether to calculate number of worker
                        threads to use in chain thread pool ("none", "lmax",
                        or "full"). In "none" mode, the default, no
                        calculation will be attempted and the configured
                        --chain-threads value will be used. In "lmax" mode,
                        producer threads will incrementally be tested,
                        starting at plugin default, until the performance rate
                        ceases to increase with the addition of additional
                        threads. In "full" mode producer threads will
                        incrementally be tested from plugin default..num
                        logical processors, recording each performance and
                        choosing the local max performance (same value as
                        would be discovered in "lmax" mode). Useful for
                        graphing the full performance impact of each available
                        thread.
  --calc-net-threads {none,lmax,full}
                        Determines whether to calculate number of worker
                        threads to use in net thread pool ("none", "lmax", or
                        "full"). In "none" mode, the default, no calculation
                        will be attempted and the configured --net-threads
                        value will be used. In "lmax" mode, producer threads
                        will incrementally be tested, starting at plugin
                        default, until the performance rate ceases to increase
                        with the addition of additional threads. In "full"
                        mode producer threads will incrementally be tested
                        from plugin default..num logical processors, recording
                        each performance and choosing the local max
                        performance (same value as would be discovered in
                        "lmax" mode). Useful for graphing the full performance
                        impact of each available thread.
  --del-test-report     Whether to save json reports from each test scenario.

Performance Harness - TPS Test Config:
  TPS Performance Test configuration items.

  --max-tps-to-test MAX_TPS_TO_TEST
                        The max target transfers realistic as ceiling of test
                        range
  --test-iteration-duration-sec TEST_ITERATION_DURATION_SEC
                        The duration of transfer trx generation for each
                        iteration of the test during the initial search
                        (seconds)
  --test-iteration-min-step TEST_ITERATION_MIN_STEP
                        The step size determining granularity of tps result
                        during initial search
  --final-iterations-duration-sec FINAL_ITERATIONS_DURATION_SEC
                        The duration of transfer trx generation for each final
                        longer run iteration of the test during the final
                        search (seconds)

Advanced Configuration Options:
  Block Producer Operational Mode Advanced Configuration Options allow low
  level adjustments to the basic test configuration as well as the node
  topology being tested. For additional information on available advanced
  configuration options, pass --help to the sub-command. Eg:
  performance_test.py testBpOpMode overrideBasicTestConfig --help

  {overrideBasicTestConfig}
                        sub-command to allow overriding advanced configuration
                        options
    overrideBasicTestConfig
                        Use this sub-command to override low level controls
                        for basic test, logging, node topology, etc.
```

</details>

#### Advance Configuration: OverrideBasicTestConfig sub-command

<details>
    <summary>Usage</summary>

```
usage: performance_test.py testBpOpMode overrideBasicTestConfig
       [-h] [-d D] [--dump-error-details] [-v] [--leave-running] [--clean-run] [--unshared]
       [--producer-nodes PRODUCER_NODES] [--validation-nodes VALIDATION_NODES] [--api-nodes API_NODES]
       [--tps-limit-per-generator TPS_LIMIT_PER_GENERATOR]
       [--genesis GENESIS] [--num-blocks-to-prune NUM_BLOCKS_TO_PRUNE]
       [--signature-cpu-billable-pct SIGNATURE_CPU_BILLABLE_PCT]
       [--chain-threads CHAIN_THREADS]
       [--database-map-mode {mapped,heap,locked}]
       [--cluster-log-lvl {all,debug,info,warn,error,off}]
       [--net-threads NET_THREADS]
       [--disable-subjective-billing DISABLE_SUBJECTIVE_BILLING]
       [--cpu-effort-percent CPU_EFFORT_PERCENT]
       [--producer-threads PRODUCER_THREADS]
       [--http-max-in-flight-requests HTTP_MAX_IN_FLIGHT_REQUESTS]
       [--http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS]
       [--http-max-bytes-in-flight-mb HTTP_MAX_BYTES_IN_FLIGHT_MB]
       [--del-perf-logs] [--del-report] [--quiet] [--prods-enable-trace-api]
       [--print-missing-transactions] [--account-name ACCOUNT_NAME]
       [--contract-dir CONTRACT_DIR] [--wasm-file WASM_FILE]
       [--abi-file ABI_FILE] [--user-trx-data-file USER_TRX_DATA_FILE]
       [--wasm-runtime {eos-vm-jit,eos-vm}] [--contracts-console]
       [--eos-vm-oc-cache-size-mb EOS_VM_OC_CACHE_SIZE_MB]
       [--eos-vm-oc-compile-threads EOS_VM_OC_COMPILE_THREADS]
       [--non-prods-eos-vm-oc-enable]
       [--block-log-retain-blocks BLOCK_LOG_RETAIN_BLOCKS]
       [--http-threads HTTP_THREADS]
       [--chain-state-db-size-mb CHAIN_STATE_DB_SIZE_MB]
```

</details>

<details>
    <summary>Expand Override Basic Test Config Argument List</summary>

```
optional arguments:
  -h, --help            show this help message and exit

Test Helper Arguments:
  Test Helper configuration items used to configure and spin up the regression test framework and blockchain environment.

  -d D                  delay between nodes startup
  --dump-error-details  Upon error print etc/eosio/node_*/config.ini and <test_name><pid>/node_*/stderr.log to stdout
  -v                    verbose logging
  --leave-running       Leave cluster running after test finishes
  --clean-run           Kill all nodeos and keosd instances
  --unshared            Run test in isolated network namespace

Performance Test Basic Base:
  Performance Test Basic base configuration items.

  --producer-nodes PRODUCER_NODES
                        Producing nodes count
  --validation-nodes VALIDATION_NODES
                        Validation nodes count
  --api-nodes API_NODES
                        API nodes count
  --tps-limit-per-generator TPS_LIMIT_PER_GENERATOR
                        Maximum amount of transactions per second a single generator can have.
  --genesis GENESIS     Path to genesis.json
  --num-blocks-to-prune NUM_BLOCKS_TO_PRUNE
                        The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks,
                        to prune from the beginning and end of the range of blocks of interest for evaluation.
  --signature-cpu-billable-pct SIGNATURE_CPU_BILLABLE_PCT
                        Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50%
  --chain-threads CHAIN_THREADS
                        Number of worker threads in controller thread pool
  --database-map-mode {mapped,heap,locked}
                        Database map mode ("mapped", "heap", or "locked").
                        In "mapped" mode database is memory mapped as a file.
                        In "heap" mode database is preloaded in to swappable memory and will use huge pages if available.
                        In "locked" mode database is preloaded, locked in to memory, and will use huge pages if available.
  --cluster-log-lvl {all,debug,info,warn,error,off}
                        Cluster log level ("all", "debug", "info", "warn", "error", or "off").
                        Performance Harness Test Basic relies on some logging at "info" level,
                        so it is recommended lowest logging level to use.
                        However, there are instances where more verbose logging can be useful.
  --net-threads NET_THREADS
                        Number of worker threads in net_plugin thread pool
  --disable-subjective-billing DISABLE_SUBJECTIVE_BILLING
                        Disable subjective CPU billing for API/P2P transactions
  --cpu-effort-percent CPU_EFFORT_PERCENT
                        Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%
  --producer-threads PRODUCER_THREADS
                        Number of worker threads in producer thread pool
  --http-max-in-flight-requests HTTP_MAX_IN_FLIGHT_REQUESTS
                        Maximum number of requests http_plugin should use for processing http requests. 429 error response when exceeded. -1 for unlimited
  --http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS
                        Maximum time for processing a request, -1 for unlimited
  --http-max-bytes-in-flight-mb HTTP_MAX_BYTES_IN_FLIGHT_MB
                        Maximum size in megabytes http_plugin should use for processing http requests. -1 for unlimited. 429 error response when exceeded.
  --del-perf-logs       Whether to delete performance test specific logs.
  --del-report          Whether to delete overarching performance run report.
  --quiet               Whether to quiet printing intermediate results and reports to stdout
  --prods-enable-trace-api
                        Determines whether producer nodes should have eosio::trace_api_plugin enabled
  --print-missing-transactions
                        Toggles if missing transactions are be printed upon test completion.
  --account-name ACCOUNT_NAME
                        Name of the account to create and assign a contract to
  --contract-dir CONTRACT_DIR
                        Path to contract dir
  --wasm-file WASM_FILE
                        WASM file name for contract
  --abi-file ABI_FILE   ABI file name for contract
  --user-trx-data-file USER_TRX_DATA_FILE
                        Path to transaction data JSON file
  --wasm-runtime {eos-vm-jit,eos-vm}
                        Override default WASM runtime ("eos-vm-jit", "eos-vm")
                        "eos-vm-jit" : A WebAssembly runtime that compiles WebAssembly code to native x86 code prior to execution.
                        "eos-vm" : A WebAssembly interpreter.
  --contracts-console   print contract's output to console
  --eos-vm-oc-cache-size-mb EOS_VM_OC_CACHE_SIZE_MB
                        Maximum size (in MiB) of the EOS VM OC code cache
  --eos-vm-oc-compile-threads EOS_VM_OC_COMPILE_THREADS
                        Number of threads to use for EOS VM OC tier-up
  --non-prods-eos-vm-oc-enable
                        Enable EOS VM OC tier-up runtime on non producer nodes
  --block-log-retain-blocks BLOCK_LOG_RETAIN_BLOCKS
                        If set to greater than 0, periodically prune the block log to store only configured number of most recent blocks.
                        If set to 0, no blocks are be written to the block log; block log file is removed after startup.
  --http-threads HTTP_THREADS
                        Number of worker threads in http thread pool
  --chain-state-db-size-mb CHAIN_STATE_DB_SIZE_MB
                        Maximum size (in MiB) of the chain state database
```

</details>

# Support Scripts

The following scripts are typically used by the Performance Harness main script `performance_test.py` to perform specific tasks as delegated and configured by the main script.  However, there may be applications in certain use cases where running a single one-off test or transaction generator is desired.  In those situations, the following argument details might be useful to understanding how to run these utilities in stand-alone mode.  The argument breakdown may also be useful in understanding how the Performance Harness main script's arguments are being passed through to configure lower-level entities.

## Performance Test Basic

`performance_test_basic.py` can be configured using the following command line arguments:

<details>
    <summary>Usage</summary>

  ```
  usage: performance_test_basic.py [-h] [-d D]
                                  [--dump-error-details] [-v] [--leave-running]
                                  [--clean-run] [--unshared]
                                  [--producer-nodes PRODUCER_NODES]
                                  [--validation-nodes VALIDATION_NODES]
                                  [--api-nodes API_NODES]
                                  [--tps-limit-per-generator TPS_LIMIT_PER_GENERATOR]
                                  [--genesis GENESIS]
                                  [--num-blocks-to-prune NUM_BLOCKS_TO_PRUNE]
                                  [--signature-cpu-billable-pct SIGNATURE_CPU_BILLABLE_PCT]
                                  [--chain-threads CHAIN_THREADS]
                                  [--database-map-mode {mapped,heap,locked}]
                                  [--cluster-log-lvl {all,debug,info,warn,error,off}]
                                  [--net-threads NET_THREADS]
                                  [--disable-subjective-billing DISABLE_SUBJECTIVE_BILLING]
                                  [--cpu-effort-percent CPU_EFFORT_PERCENT]
                                  [--producer-threads PRODUCER_THREADS]
                                  [--http-max-in-flight-requests HTTP_MAX_IN_FLIGHT_REQUESTS]
                                  [--http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS]
                                  [--http-max-bytes-in-flight-mb HTTP_MAX_BYTES_IN_FLIGHT_MB]
                                  [--del-perf-logs] [--del-report] [--quiet]
                                  [--prods-enable-trace-api]
                                  [--print-missing-transactions]
                                  [--account-name ACCOUNT_NAME]
                                  [--contract-dir CONTRACT_DIR]
                                  [--wasm-file WASM_FILE] [--abi-file ABI_FILE]
                                  [--user-trx-data-file USER_TRX_DATA_FILE]
                                  [--wasm-runtime {eos-vm-jit,eos-vm}]
                                  [--contracts-console]
                                  [--eos-vm-oc-cache-size-mb EOS_VM_OC_CACHE_SIZE_MB]
                                  [--eos-vm-oc-compile-threads EOS_VM_OC_COMPILE_THREADS]
                                  [--non-prods-eos-vm-oc-enable]
                                  [--block-log-retain-blocks BLOCK_LOG_RETAIN_BLOCKS]
                                  [--http-threads HTTP_THREADS]
                                  [--chain-state-db-size-mb CHAIN_STATE_DB_SIZE_MB]
                                  [--target-tps TARGET_TPS]
                                  [--test-duration-sec TEST_DURATION_SEC]
  ```

</details>

<details>
    <summary>Expand Argument List</summary>

```
optional arguments:
  -h, --help            show this help message and exit

Test Helper Arguments:
  Test Helper configuration items used to configure and spin up the regression test framework and blockchain environment.

  -d D                  delay between nodes startup (default: 1)
  --dump-error-details  Upon error print etc/eosio/node_*/config.ini and <test_name><pid>/node_*/stderr.log to stdout (default: False)
  -v                    verbose logging (default: False)
  --leave-running       Leave cluster running after test finishes (default: False)
  --clean-run           Kill all nodeos and keosd instances (default: False)
  --unshared            Run test in isolated network namespace (default: False)

Performance Test Basic Base:
  Performance Test Basic base configuration items.

  --producer-nodes PRODUCER_NODES
                        Producing nodes count (default: 1)
  --validation-nodes VALIDATION_NODES
                        Validation nodes count (default: 1)
  --api-nodes API_NODES
                        API nodes count (default: 0)
  --tps-limit-per-generator TPS_LIMIT_PER_GENERATOR
                        Maximum amount of transactions per second a single generator can have. (default: 4000)
  --genesis GENESIS     Path to genesis.json (default: tests/performance_tests/genesis.json)
  --num-blocks-to-prune NUM_BLOCKS_TO_PRUNE
                        The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks,
                        to prune from the beginning and end of the range of blocks of interest for evaluation. (default: 2)
  --signature-cpu-billable-pct SIGNATURE_CPU_BILLABLE_PCT
                        Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50% (default: 0)
  --chain-threads CHAIN_THREADS
                        Number of worker threads in controller thread pool (default: 2)
  --database-map-mode {mapped,heap,locked}
                        Database map mode ("mapped", "heap", or "locked").
                        In "mapped" mode database is memory mapped as a file.
                        In "heap" mode database is preloaded in to swappable memory and will use huge pages if available.
                        In "locked" mode database is preloaded, locked in to memory, and will use huge pages if available. (default: mapped)
  --cluster-log-lvl {all,debug,info,warn,error,off}
                        Cluster log level ("all", "debug", "info", "warn", "error", or "off").
                        Performance Harness Test Basic relies on some logging at "info" level, so it is recommended lowest logging level to use.
                        However, there are instances where more verbose logging can be useful. (default: info)
  --net-threads NET_THREADS
                        Number of worker threads in net_plugin thread pool (default: 4)
  --disable-subjective-billing DISABLE_SUBJECTIVE_BILLING
                        Disable subjective CPU billing for API/P2P transactions (default: True)
  --cpu-effort-percent CPU_EFFORT_PERCENT
                        Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80% (default: 100)
  --producer-threads PRODUCER_THREADS
                        Number of worker threads in producer thread pool (default: 2)
  --http-max-in-flight-requests HTTP_MAX_IN_FLIGHT_REQUESTS
                        Maximum number of requests http_plugin should use for processing http requests. 429 error response when exceeded. -1 for unlimited (default: -1)
  --http-max-response-time-ms HTTP_MAX_RESPONSE_TIME_MS
                        Maximum time for processing a request, -1 for unlimited (default: -1)
  --http-max-bytes-in-flight-mb HTTP_MAX_BYTES_IN_FLIGHT_MB
                        Maximum size in megabytes http_plugin should use for processing http requests. -1 for unlimited. 429 error response when exceeded. (default: -1)
  --del-perf-logs       Whether to delete performance test specific logs. (default: False)
  --del-report          Whether to delete overarching performance run report. (default: False)
  --quiet               Whether to quiet printing intermediate results and reports to stdout (default: False)
  --prods-enable-trace-api
                        Determines whether producer nodes should have eosio::trace_api_plugin enabled (default: False)
  --print-missing-transactions
                        Toggles if missing transactions are be printed upon test completion. (default: False)
  --account-name ACCOUNT_NAME
                        Name of the account to create and assign a contract to (default: eosio)
  --contract-dir CONTRACT_DIR
                        Path to contract dir (default: unittests/contracts/eosio.system)
  --wasm-file WASM_FILE
                        WASM file name for contract (default: eosio.system.wasm)
  --abi-file ABI_FILE   ABI file name for contract (default: eosio.system.abi)
  --user-trx-data-file USER_TRX_DATA_FILE
                        Path to transaction data JSON file (default: None)
  --wasm-runtime {eos-vm-jit,eos-vm}
                        Override default WASM runtime ("eos-vm-jit", "eos-vm")
                        "eos-vm-jit" : A WebAssembly runtime that compiles WebAssembly code to native x86 code prior to execution.
                        "eos-vm" : A WebAssembly interpreter. (default: eos-vm-jit)
  --contracts-console   print contract's output to console (default: False)
  --eos-vm-oc-cache-size-mb EOS_VM_OC_CACHE_SIZE_MB
                        Maximum size (in MiB) of the EOS VM OC code cache (default: 1024)
  --eos-vm-oc-compile-threads EOS_VM_OC_COMPILE_THREADS
                        Number of threads to use for EOS VM OC tier-up (default: 1)
  --non-prods-eos-vm-oc-enable
                        Enable EOS VM OC tier-up runtime on non producer nodes (default: False)
  --block-log-retain-blocks BLOCK_LOG_RETAIN_BLOCKS
                        If set to greater than 0, periodically prune the block log to store only configured number of most recent blocks.
                        If set to 0, no blocks are be written to the block log; block log file is removed after startup. (default: None)
  --http-threads HTTP_THREADS
                        Number of worker threads in http thread pool (default: 2)
  --chain-state-db-size-mb CHAIN_STATE_DB_SIZE_MB
                        Maximum size (in MiB) of the chain state database (default: 25600)

Performance Test Basic Single Test:
  Performance Test Basic single test configuration items. Useful for running a single test directly.
  These items may not be directly configurable from higher level scripts as the scripts themselves may configure these internally.

  --target-tps TARGET_TPS
                        The target transfers per second to send during test (default: 8000)
  --test-duration-sec TEST_DURATION_SEC
                        The duration of transfer trx generation for the test in seconds (default: 90)
```

</details>

## Transaction Generator
`./build/tests/trx_generator/trx_generator` can be configured using the following command line arguments:

<details>
    <summary>Expand Argument List</summary>

```
Transaction Generator command line options.:
  --generator-id arg (=0)               Id for the transaction generator.
                                        Allowed range (0-960). Defaults to 0.
  --chain-id arg                        set the chain id
  --contract-owner-account arg          Account name of the contract account
                                        for the transaction actions
  --accounts arg                        comma-separated list of accounts that
                                        will be used for transfers. Minimum
                                        required accounts: 2.
  --priv-keys arg                       comma-separated list of private keys in
                                        same order of accounts list that will
                                        be used to sign transactions. Minimum
                                        required: 2.
  --trx-expiration arg (=3600)          transaction expiration time in seconds.
                                        Defaults to 3,600. Maximum allowed:
                                        3,600
  --trx-gen-duration arg (=60)          Transaction generation duration
                                        (seconds). Defaults to 60 seconds.
  --target-tps arg (=1)                 Target transactions per second to
                                        generate/send. Defaults to 1
                                        transaction per second.
  --last-irreversible-block-id arg      Current last-irreversible-block-id (LIB
                                        ID) to use for transactions.
  --monitor-spinup-time-us arg (=1000000)
                                        Number of microseconds to wait before
                                        monitoring TPS. Defaults to 1000000
                                        (1s).
  --monitor-max-lag-percent arg (=5)    Max percentage off from expected
                                        transactions sent before being in
                                        violation. Defaults to 5.
  --monitor-max-lag-duration-us arg (=1000000)
                                        Max microseconds that transaction
                                        generation can be in violation before
                                        quitting. Defaults to 1000000 (1s).
  --log-dir arg                         set the logs directory
  --stop-on-trx-failed arg (=1)         stop transaction generation if sending
                                        fails.
  --abi-file arg                        The path to the contract abi file to
                                        use for the supplied transaction action
                                        data
  --actions-data arg                    The json actions data file or json
                                        actions data description string to use
  --actions-auths arg                   The json actions auth file or json
                                        actions auths description string to
                                        use, containting authAcctName to
                                        activePrivateKey pairs.
  --peer-endpoint arg (=127.0.0.1)      set the peer endpoint to send
                                        transactions to
  --port arg (=9876)                    set the peer endpoint port to send
                                        transactions to
  -h [ --help ]                         print this list
```

</details>

# Result Reports

## Performance Test Report

The Performance Harness generates a report to summarize results of test scenarios as well as overarching results of the performance harness run.  By default the report described below will be written to the top level timestamped directory for the performance run with the file name `report.json`. To omit final report, use `--del-report`.

Command used to run test and generate report:

``` bash
.build/tests/performance_tests/performance_test.py testBpOpMode --test-iteration-duration-sec 10 --final-iterations-duration-sec 30 --calc-producer-threads lmax --calc-chain-threads lmax --calc-net-threads lmax
```

### Report Breakdown
The report begins by delivering the max TPS results of the performance run.

* `InitialMaxTpsAchieved` - the max TPS throughput achieved during initial, short duration test scenarios to narrow search window
* `LongRunningMaxTpsAchieved` - the max TPS throughput achieved during final, longer duration test scenarios to zero in on sustainable max TPS

Next, a summary of the search scenario conducted and respective results is included.  Each summary includes information on the current state of the overarching search as well as basic results of the individual test that are used to determine whether the basic test was considered successful. The list of summary results are included in `InitialSearchResults` and `LongRunningSearchResults`. The number of entries in each list will vary depending on the TPS range tested (`--max-tps-to-test`) and the configured `--test-iteration-min-step`.
<details>
    <summary>Expand Search Scenario Summary Example</summary>

``` json
    "0": {
      "success": true,
      "searchTarget": 13501,
      "searchFloor": 1,
      "searchCeiling": 13501,
      "basicTestResult": {
        "targetTPS": 13501,
        "resultAvgTps": 13555.25,
        "expectedTxns": 135010,
        "resultTxns": 135010,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-25-39-13501",
        "testStart": "2023-04-05T16:25:39.588359",
        "testEnd": "2023-04-05T16:26:58.326668"
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
  "perfTestsBegin": "2023-04-05T14:35:59.067345",
  "perfTestsFinish": "2023-04-05T16:26:58.466421",
  "InitialMaxTpsAchieved": 13501,
  "LongRunningMaxTpsAchieved": 13501,
  "tpsTestStart": "2023-04-05T16:14:31.272616",
  "tpsTestFinish": "2023-04-05T16:26:58.466404",
  "InitialSearchResults": {
    "0": {
      "success": false,
      "searchTarget": 50000,
      "searchFloor": 1,
      "searchCeiling": 50000,
      "basicTestResult": {
        "targetTPS": 50000,
        "resultAvgTps": 10022.914285714285,
        "expectedTxns": 500000,
        "resultTxns": 196920,
        "tpsExpectMet": false,
        "trxExpectMet": false,
        "basicTestSuccess": false,
        "testAnalysisBlockCnt": 36,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-14-31-50000",
        "testStart": "2023-04-05T16:14:31.272670",
        "testEnd": "2023-04-05T16:16:03.534429"
      }
    },
    "1": {
      "success": false,
      "searchTarget": 25001,
      "searchFloor": 1,
      "searchCeiling": 49500,
      "basicTestResult": {
        "targetTPS": 25001,
        "resultAvgTps": 13099.29411764706,
        "expectedTxns": 250010,
        "resultTxns": 250010,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 35,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-16-03-25001",
        "testStart": "2023-04-05T16:16:03.661754",
        "testEnd": "2023-04-05T16:17:34.071307"
      }
    },
    "2": {
      "success": true,
      "searchTarget": 12501,
      "searchFloor": 1,
      "searchCeiling": 24501,
      "basicTestResult": {
        "targetTPS": 12501,
        "resultAvgTps": 12541.1875,
        "expectedTxns": 125010,
        "resultTxns": 125010,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-17-34-12501",
        "testStart": "2023-04-05T16:17:34.183148",
        "testEnd": "2023-04-05T16:18:52.175133"
      }
    },
    "3": {
      "success": false,
      "searchTarget": 19001,
      "searchFloor": 13001,
      "searchCeiling": 24501,
      "basicTestResult": {
        "targetTPS": 19001,
        "resultAvgTps": 13541.0,
        "expectedTxns": 190010,
        "resultTxns": 190010,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 25,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-18-52-19001",
        "testStart": "2023-04-05T16:18:52.284361",
        "testEnd": "2023-04-05T16:20:17.939051"
      }
    },
    "4": {
      "success": false,
      "searchTarget": 16001,
      "searchFloor": 13001,
      "searchCeiling": 18501,
      "basicTestResult": {
        "targetTPS": 16001,
        "resultAvgTps": 13426.8,
        "expectedTxns": 160010,
        "resultTxns": 160010,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 21,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-20-18-16001",
        "testStart": "2023-04-05T16:20:18.060543",
        "testEnd": "2023-04-05T16:21:40.127977"
      }
    },
    "5": {
      "success": false,
      "searchTarget": 14501,
      "searchFloor": 13001,
      "searchCeiling": 15501,
      "basicTestResult": {
        "targetTPS": 14501,
        "resultAvgTps": 13151.166666666666,
        "expectedTxns": 145010,
        "resultTxns": 145010,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 19,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-21-40-14501",
        "testStart": "2023-04-05T16:21:40.237483",
        "testEnd": "2023-04-05T16:23:00.432464"
      }
    },
    "6": {
      "success": true,
      "searchTarget": 13501,
      "searchFloor": 13001,
      "searchCeiling": 14001,
      "basicTestResult": {
        "targetTPS": 13501,
        "resultAvgTps": 13555.0625,
        "expectedTxns": 135010,
        "resultTxns": 135010,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-23-00-13501",
        "testStart": "2023-04-05T16:23:00.540367",
        "testEnd": "2023-04-05T16:24:19.418664"
      }
    },
    "7": {
      "success": false,
      "searchTarget": 14001,
      "searchFloor": 14001,
      "searchCeiling": 14001,
      "basicTestResult": {
        "targetTPS": 14001,
        "resultAvgTps": 13482.823529411764,
        "expectedTxns": 140010,
        "resultTxns": 140010,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 18,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-24-19-14001",
        "testStart": "2023-04-05T16:24:19.526361",
        "testEnd": "2023-04-05T16:25:39.445419"
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
      "success": true,
      "searchTarget": 13501,
      "searchFloor": 1,
      "searchCeiling": 13501,
      "basicTestResult": {
        "targetTPS": 13501,
        "resultAvgTps": 13555.25,
        "expectedTxns": 135010,
        "resultTxns": 135010,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "testAnalysisBlockCnt": 17,
        "logsDir": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-25-39-13501",
        "testStart": "2023-04-05T16:25:39.588359",
        "testEnd": "2023-04-05T16:26:58.326668"
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
    "recommendedThreadCount": 5,
    "threadToMaxTpsDict": {
      "2": 14501,
      "3": 15501,
      "4": 18001,
      "5": 19001,
      "6": 18501
    },
    "analysisStart": "2023-04-05T14:35:59.130340",
    "analysisFinish": "2023-04-05T15:30:15.071101"
  },
  "ChainThreadAnalysis": {
    "recommendedThreadCount": 2,
    "threadToMaxTpsDict": {
      "2": 15001,
      "3": 14501
    },
    "analysisStart": "2023-04-05T15:30:15.072156",
    "analysisFinish": "2023-04-05T15:52:23.942065"
  },
  "NetThreadAnalysis": {
    "recommendedThreadCount": 4,
    "threadToMaxTpsDict": {
      "4": 13501,
      "5": 13001
    },
    "analysisStart": "2023-04-05T15:52:23.943254",
    "analysisFinish": "2023-04-05T16:14:31.271855"
  },
  "args": {
    "rawCmdLine ": "./tests/performance_tests/performance_test.py testBpOpMode --test-iteration-duration-sec 10 --final-iterations-duration-sec 30 --calc-producer-threads lmax --calc-chain-threads lmax --calc-net-threads lmax",
    "killAll": false,
    "dontKill": false,
    "keepLogs": true,
    "dumpErrorDetails": false,
    "delay": 1,
    "nodesFile": null,
    "verbose": false,
    "unshared": false,
    "_killEosInstances": true,
    "_killWallet": true,
    "pnodes": 1,
    "totalNodes": 0,
    "topo": "mesh",
    "extraNodeosArgs": {
      "chainPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "chain_plugin",
        "blocksDir": null,
        "_blocksDirNodeosDefault": "\"blocks\"",
        "_blocksDirNodeosArg": "--blocks-dir",
        "blocksLogStride": null,
        "_blocksLogStrideNodeosDefault": null,
        "_blocksLogStrideNodeosArg": "--blocks-log-stride",
        "maxRetainedBlockFiles": null,
        "_maxRetainedBlockFilesNodeosDefault": null,
        "_maxRetainedBlockFilesNodeosArg": "--max-retained-block-files",
        "blocksRetainedDir": null,
        "_blocksRetainedDirNodeosDefault": null,
        "_blocksRetainedDirNodeosArg": "--blocks-retained-dir",
        "blocksArchiveDir": null,
        "_blocksArchiveDirNodeosDefault": null,
        "_blocksArchiveDirNodeosArg": "--blocks-archive-dir",
        "stateDir": null,
        "_stateDirNodeosDefault": "\"state\"",
        "_stateDirNodeosArg": "--state-dir",
        "protocolFeaturesDir": null,
        "_protocolFeaturesDirNodeosDefault": "\"protocol_features\"",
        "_protocolFeaturesDirNodeosArg": "--protocol-features-dir",
        "checkpoint": null,
        "_checkpointNodeosDefault": null,
        "_checkpointNodeosArg": "--checkpoint",
        "wasmRuntime": "eos-vm-jit",
        "_wasmRuntimeNodeosDefault": "eos-vm-jit",
        "_wasmRuntimeNodeosArg": "--wasm-runtime",
        "profileAccount": null,
        "_profileAccountNodeosDefault": null,
        "_profileAccountNodeosArg": "--profile-account",
        "abiSerializerMaxTimeMs": 990000,
        "_abiSerializerMaxTimeMsNodeosDefault": 15,
        "_abiSerializerMaxTimeMsNodeosArg": "--abi-serializer-max-time-ms",
        "chainStateDbSizeMb": 25600,
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
        "contractsConsole": false,
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
        "eosVmOcCacheSizeMb": 1024,
        "_eosVmOcCacheSizeMbNodeosDefault": 1024,
        "_eosVmOcCacheSizeMbNodeosArg": "--eos-vm-oc-cache-size-mb",
        "eosVmOcCompileThreads": 1,
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
      "httpPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "http_plugin",
        "unixSocketPath": null,
        "_unixSocketPathNodeosDefault": null,
        "_unixSocketPathNodeosArg": "--unix-socket-path",
        "httpServerAddress": null,
        "_httpServerAddressNodeosDefault": "127.0.0.1:8888",
        "_httpServerAddressNodeosArg": "--http-server-address",
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
        "httpMaxBytesInFlightMb": -1,
        "_httpMaxBytesInFlightMbNodeosDefault": 500,
        "_httpMaxBytesInFlightMbNodeosArg": "--http-max-bytes-in-flight-mb",
        "httpMaxInFlightRequests": null,
        "_httpMaxInFlightRequestsNodeosDefault": -1,
        "_httpMaxInFlightRequestsNodeosArg": "--http-max-in-flight-requests",
        "httpMaxResponseTimeMs": -1,
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
        "httpThreads": 2,
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
        "p2pAutoBpPeer": null,
        "_p2pAutoBpPeerNodeosDefault": null,
        "_p2pAutoBpPeerNodeosArg": "--p2p-auto-bp-peer",
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
        "maxClients": 0,
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
        "maxTransactionTime": -1,
        "_maxTransactionTimeNodeosDefault": 30,
        "_maxTransactionTimeNodeosArg": "--max-transaction-time",
        "maxIrreversibleBlockAge": null,
        "_maxIrreversibleBlockAgeNodeosDefault": -1,
        "_maxIrreversibleBlockAgeNodeosArg": "--max-irreversible-block-age",
        "producerName": null,
        "_producerNameNodeosDefault": null,
        "_producerNameNodeosArg": "--producer-name",
        "signatureProvider": null,
        "_signatureProviderNodeosDefault": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV=KEY:5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3",
        "_signatureProviderNodeosArg": "--signature-provider",
        "greylistAccount": null,
        "_greylistAccountNodeosDefault": null,
        "_greylistAccountNodeosArg": "--greylist-account",
        "greylistLimit": null,
        "_greylistLimitNodeosDefault": 1000,
        "_greylistLimitNodeosArg": "--greylist-limit",
        "cpuEffortPercent": 100,
        "_cpuEffortPercentNodeosDefault": 80,
        "_cpuEffortPercentNodeosArg": "--cpu-effort-percent",
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
        "subjectiveAccountMaxFailuresWindowSize": null,
        "_subjectiveAccountMaxFailuresWindowSizeNodeosDefault": 1,
        "_subjectiveAccountMaxFailuresWindowSizeNodeosArg": "--subjective-account-max-failures-window-size",
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
        "_snapshotsDirNodeosArg": "--snapshots-dir",
        "readOnlyThreads": null,
        "_readOnlyThreadsNodeosDefault": null,
        "_readOnlyThreadsNodeosArg": "--read-only-threads",
        "readOnlyWriteWindowTimeUs": null,
        "_readOnlyWriteWindowTimeUsNodeosDefault": 200000,
        "_readOnlyWriteWindowTimeUsNodeosArg": "--read-only-write-window-time-us",
        "readOnlyReadWindowTimeUs": null,
        "_readOnlyReadWindowTimeUsNodeosDefault": 60000,
        "_readOnlyReadWindowTimeUsNodeosArg": "--read-only-read-window-time-us"
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
        "resourceMonitorSpaceAbsoluteGb": null,
        "_resourceMonitorSpaceAbsoluteGbNodeosDefault": null,
        "_resourceMonitorSpaceAbsoluteGbNodeosArg": "--resource-monitor-space-absolute-gb",
        "resourceMonitorNotShutdownOnThresholdExceeded": true,
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
        "stateHistoryRetainedDir": null,
        "_stateHistoryRetainedDirNodeosDefault": null,
        "_stateHistoryRetainedDirNodeosArg": "--state-history-retained-dir",
        "stateHistoryArchiveDir": null,
        "_stateHistoryArchiveDirNodeosDefault": null,
        "_stateHistoryArchiveDirNodeosArg": "--state-history-archive-dir",
        "stateHistoryStride": null,
        "_stateHistoryStrideNodeosDefault": null,
        "_stateHistoryStrideNodeosArg": "--state-history-stride",
        "maxRetainedHistoryFiles": null,
        "_maxRetainedHistoryFilesNodeosDefault": null,
        "_maxRetainedHistoryFilesNodeosArg": "--max-retained-history-files",
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
      "contractDir": "unittests/contracts/eosio.system",
      "wasmFile": "eosio.system.wasm",
      "abiFile": "eosio.system.abi",
      "account": "Name: eosio"
    },
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
      "1": "--plugin eosio::trace_api_plugin "
    },
    "_totalNodes": 2,
    "nonProdsEosVmOcEnable": false,
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
    "userTrxDataFile": null,
    "logDirBase": "performance_test",
    "logDirTimestamp": "2023-04-05_14-35-59",
    "logDirPath": "performance_test/2023-04-05_14-35-59",
    "ptbLogsDirPath": "performance_test/2023-04-05_14-35-59/testRunLogs",
    "pluginThreadOptLogsDirPath": "performance_test/2023-04-05_14-35-59/pluginThreadOptRunLogs"
  },
  "env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.15.90.1-microsoft-standard-WSL2",
    "logical_cpu_count": 16
  },
  "nodeosVersion": "v4"
}
```
</details>


## Performance Test Basic Report

The Performance Test Basic generates, by default, a report that details results of the test, statistics around metrics of interest, as well as diagnostic information about the test run.  If `performance_test.py` is run with `--del-test-report`, or `performance_test_basic.py` is run with `--del-report`, the report described below will not be written.  Otherwise the report will be written to the timestamped directory within the `performance_test_basic` log directory for the test run with the file name `data.json`.

<details>
    <summary>Expand for full sample report</summary>

``` json
{
  "completedRun": true,
  "testStart": "2023-04-05T16:25:39.588359",
  "testFinish": "2023-04-05T16:26:58.326668",
  "Analysis": {
    "BlockSize": {
      "min": 139000,
      "max": 182475,
      "avg": 168440.0588235294,
      "sigma": 11024.182010314407,
      "emptyBlocks": 0,
      "numBlocks": 17
    },
    "BlocksGuide": {
      "firstBlockNum": 113,
      "lastBlockNum": 145,
      "totalBlocks": 33,
      "testStartBlockNum": 113,
      "testEndBlockNum": 145,
      "setupBlocksCnt": 0,
      "tearDownBlocksCnt": 0,
      "leadingEmptyBlocksCnt": 1,
      "trailingEmptyBlocksCnt": 11,
      "configAddlDropCnt": 2,
      "testAnalysisBlockCnt": 17
    },
    "TPS": {
      "min": 12390,
      "max": 14454,
      "avg": 13555.25,
      "sigma": 551.2613377518869,
      "emptyBlocks": 0,
      "numBlocks": 17,
      "configTps": 13501,
      "configTestDuration": 10,
      "tpsPerGenerator": [
        3375,
        3375,
        3375,
        3376
      ],
      "generatorCount": 4
    },
    "TrxCPU": {
      "min": 8.0,
      "max": 1629.0,
      "avg": 27.252588697133547,
      "sigma": 23.105861916437814,
      "samples": 135010
    },
    "TrxLatency": {
      "min": 0.0009999275207519531,
      "max": 0.5899999141693115,
      "avg": 0.2691181839364483,
      "sigma": 0.1464077116092879,
      "samples": 135010
    },
    "TrxNet": {
      "min": 24.0,
      "max": 25.0,
      "avg": 24.851892452410933,
      "sigma": 0.35520656234959913,
      "samples": 135010
    },
    "DroppedTransactions": 0,
    "ProductionWindowsTotal": 0,
    "ProductionWindowsAverageSize": 0,
    "ProductionWindowsMissed": 0,
    "ForkedBlocks": {
      "00": [],
      "01": []
    },
    "ForksCount": {
      "00": 0,
      "01": 0
    },
    "DroppedBlocks": {
      "00": {},
      "01": {}
    },
    "DroppedBlocksCount": {
      "00": 0,
      "01": 0
    }
  },
  "args": {
    "rawCmdLine ": "./tests/performance_tests/performance_test.py testBpOpMode --test-iteration-duration-sec 10 --final-iterations-duration-sec 30 --calc-producer-threads lmax --calc-chain-threads lmax --calc-net-threads lmax",
    "killAll": false,
    "dontKill": false,
    "keepLogs": true,
    "dumpErrorDetails": false,
    "delay": 1,
    "nodesFile": null,
    "verbose": false,
    "unshared": false,
    "_killEosInstances": true,
    "_killWallet": true,
    "pnodes": 1,
    "totalNodes": 0,
    "topo": "mesh",
    "extraNodeosArgs": {
      "chainPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "chain_plugin",
        "blocksDir": null,
        "_blocksDirNodeosDefault": "\"blocks\"",
        "_blocksDirNodeosArg": "--blocks-dir",
        "blocksLogStride": null,
        "_blocksLogStrideNodeosDefault": null,
        "_blocksLogStrideNodeosArg": "--blocks-log-stride",
        "maxRetainedBlockFiles": null,
        "_maxRetainedBlockFilesNodeosDefault": null,
        "_maxRetainedBlockFilesNodeosArg": "--max-retained-block-files",
        "blocksRetainedDir": null,
        "_blocksRetainedDirNodeosDefault": null,
        "_blocksRetainedDirNodeosArg": "--blocks-retained-dir",
        "blocksArchiveDir": null,
        "_blocksArchiveDirNodeosDefault": null,
        "_blocksArchiveDirNodeosArg": "--blocks-archive-dir",
        "stateDir": null,
        "_stateDirNodeosDefault": "\"state\"",
        "_stateDirNodeosArg": "--state-dir",
        "protocolFeaturesDir": null,
        "_protocolFeaturesDirNodeosDefault": "\"protocol_features\"",
        "_protocolFeaturesDirNodeosArg": "--protocol-features-dir",
        "checkpoint": null,
        "_checkpointNodeosDefault": null,
        "_checkpointNodeosArg": "--checkpoint",
        "wasmRuntime": "eos-vm-jit",
        "_wasmRuntimeNodeosDefault": "eos-vm-jit",
        "_wasmRuntimeNodeosArg": "--wasm-runtime",
        "profileAccount": null,
        "_profileAccountNodeosDefault": null,
        "_profileAccountNodeosArg": "--profile-account",
        "abiSerializerMaxTimeMs": 990000,
        "_abiSerializerMaxTimeMsNodeosDefault": 15,
        "_abiSerializerMaxTimeMsNodeosArg": "--abi-serializer-max-time-ms",
        "chainStateDbSizeMb": 25600,
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
        "contractsConsole": false,
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
        "eosVmOcCacheSizeMb": 1024,
        "_eosVmOcCacheSizeMbNodeosDefault": 1024,
        "_eosVmOcCacheSizeMbNodeosArg": "--eos-vm-oc-cache-size-mb",
        "eosVmOcCompileThreads": 1,
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
      "httpPluginArgs": {
        "_pluginNamespace": "eosio",
        "_pluginName": "http_plugin",
        "unixSocketPath": null,
        "_unixSocketPathNodeosDefault": null,
        "_unixSocketPathNodeosArg": "--unix-socket-path",
        "httpServerAddress": null,
        "_httpServerAddressNodeosDefault": "127.0.0.1:8888",
        "_httpServerAddressNodeosArg": "--http-server-address",
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
        "httpMaxBytesInFlightMb": -1,
        "_httpMaxBytesInFlightMbNodeosDefault": 500,
        "_httpMaxBytesInFlightMbNodeosArg": "--http-max-bytes-in-flight-mb",
        "httpMaxInFlightRequests": null,
        "_httpMaxInFlightRequestsNodeosDefault": -1,
        "_httpMaxInFlightRequestsNodeosArg": "--http-max-in-flight-requests",
        "httpMaxResponseTimeMs": -1,
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
        "httpThreads": 2,
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
        "p2pAutoBpPeer": null,
        "_p2pAutoBpPeerNodeosDefault": null,
        "_p2pAutoBpPeerNodeosArg": "--p2p-auto-bp-peer",
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
        "maxClients": 0,
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
        "maxTransactionTime": -1,
        "_maxTransactionTimeNodeosDefault": 30,
        "_maxTransactionTimeNodeosArg": "--max-transaction-time",
        "maxIrreversibleBlockAge": null,
        "_maxIrreversibleBlockAgeNodeosDefault": -1,
        "_maxIrreversibleBlockAgeNodeosArg": "--max-irreversible-block-age",
        "producerName": null,
        "_producerNameNodeosDefault": null,
        "_producerNameNodeosArg": "--producer-name",
        "signatureProvider": null,
        "_signatureProviderNodeosDefault": "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV=KEY:5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3",
        "_signatureProviderNodeosArg": "--signature-provider",
        "greylistAccount": null,
        "_greylistAccountNodeosDefault": null,
        "_greylistAccountNodeosArg": "--greylist-account",
        "greylistLimit": null,
        "_greylistLimitNodeosDefault": 1000,
        "_greylistLimitNodeosArg": "--greylist-limit",
        "cpuEffortPercent": 100,
        "_cpuEffortPercentNodeosDefault": 80,
        "_cpuEffortPercentNodeosArg": "--cpu-effort-percent",
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
        "subjectiveAccountMaxFailuresWindowSize": null,
        "_subjectiveAccountMaxFailuresWindowSizeNodeosDefault": 1,
        "_subjectiveAccountMaxFailuresWindowSizeNodeosArg": "--subjective-account-max-failures-window-size",
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
        "_snapshotsDirNodeosArg": "--snapshots-dir",
        "readOnlyThreads": null,
        "_readOnlyThreadsNodeosDefault": null,
        "_readOnlyThreadsNodeosArg": "--read-only-threads",
        "readOnlyWriteWindowTimeUs": null,
        "_readOnlyWriteWindowTimeUsNodeosDefault": 200000,
        "_readOnlyWriteWindowTimeUsNodeosArg": "--read-only-write-window-time-us",
        "readOnlyReadWindowTimeUs": null,
        "_readOnlyReadWindowTimeUsNodeosDefault": 60000,
        "_readOnlyReadWindowTimeUsNodeosArg": "--read-only-read-window-time-us"
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
        "resourceMonitorSpaceAbsoluteGb": null,
        "_resourceMonitorSpaceAbsoluteGbNodeosDefault": null,
        "_resourceMonitorSpaceAbsoluteGbNodeosArg": "--resource-monitor-space-absolute-gb",
        "resourceMonitorNotShutdownOnThresholdExceeded": true,
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
        "stateHistoryRetainedDir": null,
        "_stateHistoryRetainedDirNodeosDefault": null,
        "_stateHistoryRetainedDirNodeosArg": "--state-history-retained-dir",
        "stateHistoryArchiveDir": null,
        "_stateHistoryArchiveDirNodeosDefault": null,
        "_stateHistoryArchiveDirNodeosArg": "--state-history-archive-dir",
        "stateHistoryStride": null,
        "_stateHistoryStrideNodeosDefault": null,
        "_stateHistoryStrideNodeosArg": "--state-history-stride",
        "maxRetainedHistoryFiles": null,
        "_maxRetainedHistoryFilesNodeosDefault": null,
        "_maxRetainedHistoryFilesNodeosArg": "--max-retained-history-files",
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
      "contractDir": "unittests/contracts/eosio.system",
      "wasmFile": "eosio.system.wasm",
      "abiFile": "eosio.system.abi",
      "account": "Name: eosio"
    },
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
      "1": "--plugin eosio::trace_api_plugin "
    },
    "_totalNodes": 2,
    "nonProdsEosVmOcEnable": false,
    "targetTps": 13501,
    "testTrxGenDurationSec": 10,
    "tpsLimitPerGenerator": 4000,
    "numAddlBlocksToPrune": 2,
    "logDirRoot": "performance_test/2023-04-05_14-35-59/testRunLogs",
    "delReport": false,
    "quiet": false,
    "delPerfLogs": false,
    "expectedTransactionsSent": 135010,
    "printMissingTransactions": false,
    "userTrxDataFile": null,
    "logDirBase": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test",
    "logDirTimestamp": "2023-04-05_16-25-39",
    "logDirTimestampedOptSuffix": "-13501",
    "logDirPath": "performance_test/2023-04-05_14-35-59/testRunLogs/performance_test/2023-04-05_16-25-39-13501"
  },
  "env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.15.90.1-microsoft-standard-WSL2",
    "logical_cpu_count": 16
  },
  "nodeosVersion": "v4"
}
```
</details>
