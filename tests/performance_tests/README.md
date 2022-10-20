# Performance Harness Tests

The Performance Harness is configured and run through the main `performance_test.py` script.  The script's main goal is to measure current peak performance metrics through iteratively tuning and running basic performance tests. The current basic test works to determine the maximum throughput of EOS Token Transfers the system can sustain.  It does this by conducting a binary search of possible EOS Token Transfers Per Second (TPS) configurations, testing each configuration in a short duration test and scoring its result. The search algorithm iteratively configures and runs `performance_test_basic.py` tests and analyzes the output to determine a success metric used to continue the search.  When the search completes, a max TPS throughput value is reported (along with other performance metrics from that run).  The script then proceeds to conduct an additional search with longer duration test runs within a narrowed TPS configuration range to determine the sustainable max TPS. Finally it produces a report on the entire performance run, summarizing each individual test scenario, results, and full report details on the tests when maximum TPS was achieved ([Performance Test Report](#performance-test))

The `performance_test_basic.py` support script performs a single basic performance test that targets a configurable TPS target and, if successful, reports statistics on performance metrics measured during the test.  It configures and launches a blockchain test environment, creates wallets and accounts for testing, and configures and launches transaction generators for creating specific transaction load in the ecosystem.  Finally it analyzes the performance of the system under the configuration through log analysis and chain queries and produces a [Performance Test Basic Report](#performance-test-basic).

The `launch_generators.py` support script provides a means to easily calculate and spawn the number of transaction generator instances to generate a given target TPS, distributing generation load between the instances in a fair manner such that the aggregate load meets the requested test load.

The `log_reader.py` support script is used primarily to analyze `nodeos` log files to glean information about generated blocks and transactions within those blocks after a test has concluded.  This information is used to produce the performance test report. In similar fashion, `read_log_data.py` allows for recreating a report from the configuration and log files without needing to rerun the test.

## Prerequisites

Please refer to [Leap: Building From Source](https://github.com/AntelopeIO/leap#building-from-source) for a full list of prerequisites.

## Steps

1. Build Leap. For complete instructions on building from source please refer to [Leap: Building From Source](https://github.com/AntelopeIO/leap#building-from-source)
2. Run Performance Tests
    1. Full Performance Harness Test Run (Standard):
        ``` bash
        ./build/tests/performance_tests/performance_test.py
        ```
    2. Single Performance Test Basic Run (Manually run one-off test):
        ```bash
        ./build/tests/performance_tests/performance_test_basic.py
        ```
3. Collect Results - If specifying `--keep-logs` and/or `--save-json` and/or `--save-test-json`
    1. Navigate to performance test logs directory
        ```bash
        cd ./build/performance_test/
        ```
    2. Log Directory Structure is hierarchical with each run of the `performance_test.py` reporting into a timestamped directory where it includes the full performance report as well as a directory containing output from each test type run (here, `performance_test_basic.py`) and each individual test run outputs into a timestamped directory that may contain block data logs and transaction generator logs as well as the test's basic report.  An example directory structure follows:
        ``` bash
        performance_test/
        └── 2022-10-19_10-23-10
            ├── report.json
            └── testRunLogs
                └── performance_test_basic
                    ├── 2022-10-19_10-23-10
                    │   ├── blockDataLogs
                    │   │   ├── blockData.txt
                    │   │   └── blockTrxData.txt
                    │   ├── data.json
                    │   └── trxGenLogs
                    │       └── trx_data_output_7612.txt
                    └── 2022-10-19_10-29-07
                        ├── blockDataLogs
                        │   ├── blockData.txt
                        │   └── blockTrxData.txt
                        ├── data.json
                        └── trxGenLogs
                            ├── trx_data_output_10744.txt
                            └── trx_data_output_10745.txt
        ```

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
* `--dump-error-details`  Upon error print `etc/eosio/node_*/config.ini` and `var/lib/node_*/stderr.log` to stdout (default:
                    False)
* `--keep-logs`           Don't delete `var/lib/node_*` folders, or other test specific log directories, upon test
                    completion (default: False)
* `-v`                    verbose logging (default: False)
* `--leave-running`       Leave cluster running after test finishes (default: False)
* `--clean-run`           Kill all nodeos and kleos instances (default: False)
* `--max-tps-to-test MAX_TPS_TO_TEST`
                    The max target transfers realistic as ceiling of test range (default: 50000)
* `--test-iteration-duration-sec TEST_ITERATION_DURATION_SEC`
                    The duration of transfer trx generation for each iteration of the test during the initial
                    search (seconds) (default: 30)
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
* `--save-json SAVE_JSON`
                    Whether to save overarching performance run report. (default: False)
* `--save-test-json SAVE_TEST_JSON`
                    Whether to save json reports from each test scenario. (default: False)
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
* `--keep-logs`           Don't delete `var/lib/node_*` folders, or other test specific log directories, upon test completion (default: False)
* `-v`                    verbose logging (default: False)
* `--leave-running`       Leave cluster running after test finishes (default: False)
* `--clean-run`           Kill all nodeos and kleos instances (default: False)
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
* `--save-json SAVE_JSON`
                    Whether to save json output of stats (default: False)
</details>

#### Launch Transaction Generators

`launch_transaction_generators.py` can be configured using the following command line arguments:

<details>
    <summary>Expand Argument List</summary>

* `chain_id`                    set the chain id
* `last_irreversible_block_id`  Current last-irreversible-block-id (LIB ID) to use for transactions.
* `handler_account`             Account name of the handler account for the transfer actions
* `account_1_name`              First accounts that will be used for transfers.
* `account_2_name`              Second accounts that will be used for transfers.
* `account_1_priv_key`          First account's private key that will be used to sign transactions
* `account_2_priv_key`          Second account's private key that will be used to sign transactions
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

### Performance Test

The Performance Harness generates a report to summarize results of test scenarios as well as overarching results of the performance harness run.  If run with `--save-json` the report described below will be written to the top level timestamped directory for the performance run with the file name `report.json`.

Command used to run test and generate report:

``` bash
.build/tests/performance_tests/performance_test.py --test-iteration-duration-sec 10 --final-iterations-duration-sec 30 --save-json True
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
        "resultAvgTps": 15382.714285714286,
        "expectedTxns": 250000,
        "resultTxns": 250000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_16-55-27"
    }
    }
```
</details>

Finally, the full detail test report for each of the determined max TPS throughput (`InitialMaxTpsAchieved` and `LongRunningMaxTpsAchieved`) runs is included after each scenario summary list in the full report.  **Note:** In the example full report below, these have been truncated as they are single performance test basic run reports as detailed in the following section [Performance Test Basic Report](#performance-test-basic).  Herein these truncated reports appear as:

<details>
    <summary>Expand Truncated Report Example</summary>

``` json
"InitialMaxTpsReport": {
    "Analysis": {
<truncated>
    },
    "args": {
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
"InitialMaxTpsAchieved": 16200,
"LongRunningMaxTpsAchieved": 15400,
"InitialSearchResults": {
    "0": {
    "success": false,
    "searchTarget": 25000,
    "searchFloor": 0,
    "searchCeiling": 50000,
    "basicTestResult": {
        "targetTPS": 25000,
        "resultAvgTps": 15382.714285714286,
        "expectedTxns": 250000,
        "resultTxns": 250000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_16-55-27"
    }
    },
    "1": {
    "success": true,
    "searchTarget": 12500,
    "searchFloor": 0,
    "searchCeiling": 25000,
    "basicTestResult": {
        "targetTPS": 12500,
        "resultAvgTps": 12499.375,
        "expectedTxns": 125000,
        "resultTxns": 125000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_16-57-15"
    }
    },
    "2": {
    "success": false,
    "searchTarget": 18800,
    "searchFloor": 12500,
    "searchCeiling": 25000,
    "basicTestResult": {
        "targetTPS": 18800,
        "resultAvgTps": 16209.105263157895,
        "expectedTxns": 188000,
        "resultTxns": 188000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_16-58-53"
    }
    },
    "3": {
    "success": true,
    "searchTarget": 15600,
    "searchFloor": 12500,
    "searchCeiling": 18800,
    "basicTestResult": {
        "targetTPS": 15600,
        "resultAvgTps": 15623.1875,
        "expectedTxns": 156000,
        "resultTxns": 156000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-00-35"
    }
    },
    "4": {
    "success": false,
    "searchTarget": 17200,
    "searchFloor": 15600,
    "searchCeiling": 18800,
    "basicTestResult": {
        "targetTPS": 17200,
        "resultAvgTps": 16264.64705882353,
        "expectedTxns": 172000,
        "resultTxns": 172000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-02-15"
    }
    },
    "5": {
    "success": false,
    "searchTarget": 16400,
    "searchFloor": 15600,
    "searchCeiling": 17200,
    "basicTestResult": {
        "targetTPS": 16400,
        "resultAvgTps": 16263.235294117647,
        "expectedTxns": 164000,
        "resultTxns": 164000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-03-55"
    }
    },
    "6": {
    "success": true,
    "searchTarget": 16000,
    "searchFloor": 15600,
    "searchCeiling": 16400,
    "basicTestResult": {
        "targetTPS": 16000,
        "resultAvgTps": 16098.9375,
        "expectedTxns": 160000,
        "resultTxns": 160000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-05-36"
    }
    },
    "7": {
    "success": true,
    "searchTarget": 16200,
    "searchFloor": 16000,
    "searchCeiling": 16400,
    "basicTestResult": {
        "targetTPS": 16200,
        "resultAvgTps": 16135.5625,
        "expectedTxns": 162000,
        "resultTxns": 162000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-07-16"
    }
    }
},
"InitialMaxTpsReport": {
    "Analysis": {
<truncated>
    },
    "args": {
<truncated>
    },
<truncated>
},
"LongRunningSearchResults": {
    "0": {
    "success": false,
    "searchTarget": 16200,
    "searchFloor": 14700,
    "searchCeiling": 17700,
    "basicTestResult": {
        "targetTPS": 16200,
        "resultAvgTps": 15782.413793103447,
        "expectedTxns": 486000,
        "resultTxns": 486000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-08-56"
    }
    },
    "1": {
    "success": true,
    "searchTarget": 15400,
    "searchFloor": 14700,
    "searchCeiling": 16200,
    "basicTestResult": {
        "targetTPS": 15400,
        "resultAvgTps": 15343.875,
        "expectedTxns": 462000,
        "resultTxns": 462000,
        "tpsExpectMet": true,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-11-11"
    }
    },
    "2": {
    "success": false,
    "searchTarget": 15800,
    "searchFloor": 15400,
    "searchCeiling": 16200,
    "basicTestResult": {
        "targetTPS": 15800,
        "resultAvgTps": 15523.30357142857,
        "expectedTxns": 474000,
        "resultTxns": 474000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-13-24"
    }
    },
    "3": {
    "success": false,
    "searchTarget": 15600,
    "searchFloor": 15400,
    "searchCeiling": 15800,
    "basicTestResult": {
        "targetTPS": 15600,
        "resultAvgTps": 15464.589285714286,
        "expectedTxns": 468000,
        "resultTxns": 468000,
        "tpsExpectMet": false,
        "trxExpectMet": true,
        "basicTestSuccess": true,
        "logsDir": "performance_test/2022-10-12_16-55-27/testRunLogs/performance_test_basic/2022-10-12_17-15-38"
    }
    }
},
"LongRunningMaxTpsReport": {
    "Analysis": {
<truncated>
    },
    "args": {
<truncated>
    },
<truncated>
},
"args": {
    "killAll": false,
    "dontKill": false,
    "keepLogs": false,
    "dumpErrorDetails": false,
    "delay": 1,
    "nodesFile": null,
    "verbose": false,
    "_killEosInstances": true,
    "_killWallet": true,
    "pnodes": 1,
    "totalNodes": 0,
    "topo": "mesh",
    "extraNodeosArgs": " --http-max-response-time-ms 990000 --disable-subjective-api-billing true ",
    "useBiosBootFile": false,
    "genesisPath": "tests/performance_tests/genesis.json",
    "maximumP2pPerHost": 5000,
    "maximumClients": 0,
    "_totalNodes": 2,
    "testDurationSec": 10,
    "finalDurationSec": 30,
    "maxTpsToTest": 50000,
    "testIterationMinStep": 500,
    "tpsLimitPerGenerator": 4000,
    "saveJsonReport": true,
    "saveTestJsonReports": false,
    "numAddlBlocksToPrune": 2,
    "logsDir": "performance_test/2022-10-12_16-55-27"
},
"env": {
    "system": "Linux",
    "os": "posix",
    "release": "5.10.102.1-microsoft-standard-WSL2"
},
"nodeosVersion": "v3.2.0-dev"
}
```
</details>


### Performance Test Basic

The Performance Test Basic generates a report that details results of the test, statistics around metrics of interest, as well as diagnostic information about the test run.  If `performance_test.py` is run with `--save-test-json`, or `performance_test_basic.py` is run with `--save-json`, the report described below will be written to the timestamped directory within the `performance_test_basic` log directory for the test run with the file name `data.json`.

<details>
    <summary>Expand for full sample report</summary>
    
``` json
{
    "Analysis": {
      "BlockSize": {
        "avg": 1507950.0350877193,
        "emptyBlocks": 0,
        "max": 1897400,
        "min": 1184064,
        "numBlocks": 57,
        "sigma": 140462.7045683851
      },
      "BlocksGuide": {
        "configAddlDropCnt": 2,
        "firstBlockNum": 2,
        "lastBlockNum": 259,
        "leadingEmptyBlocksCnt": 1,
        "setupBlocksCnt": 127,
        "tearDownBlocksCnt": 37,
        "testAnalysisBlockCnt": 57,
        "testEndBlockNum": 222,
        "testStartBlockNum": 129,
        "totalBlocks": 258,
        "trailingEmptyBlocksCnt": 32
      },
      "TPS": {
        "avg": 15343.875,
        "configTestDuration": 30,
        "configTps": 15400,
        "emptyBlocks": 0,
        "max": 17218,
        "min": 13555,
        "numBlocks": 57,
        "sigma": 695.6516488285334
      },
      "TrxCPU": {
        "avg": 43.294225108225106,
        "max": 1389.0,
        "min": 24.0,
        "samples": 462000,
        "sigma": 15.451334956307504
      },
      "TrxLatency": {
        "avg": 0.4002558766164821,
        "max": 0.806999921798706,
        "min": 0.10100007057189941,
        "samples": 462000,
        "sigma": 0.15376674034615292
      },
      "TrxNet": {
        "avg": 24.567108225108225,
        "max": 25.0,
        "min": 24.0,
        "samples": 462000,
        "sigma": 0.4954760197252983
      }
    },
    "args": {
      "_killEosInstances": true,
      "_killWallet": true,
      "_totalNodes": 2,
      "delay": 1,
      "dontKill": false,
      "dumpErrorDetails": false,
      "expectedTransactionsSent": 462000,
      "extraNodeosArgs": " --http-max-response-time-ms 990000 --disable-subjective-api-billing true ",
      "genesisPath": "tests/performance_tests/genesis.json",
      "keepLogs": false,
      "killAll": false,
      "maximumClients": 0,
      "maximumP2pPerHost": 5000,
      "nodesFile": null,
      "numAddlBlocksToPrune": 2,
      "pnodes": 1,
      "saveJsonReport": false,
      "targetTps": 15400,
      "testTrxGenDurationSec": 30,
      "topo": "mesh",
      "totalNodes": 0,
      "tpsLimitPerGenerator": 4000,
      "useBiosBootFile": false,
      "verbose": false
    },
    "completedRun": true,
    "env": {
      "os": "posix",
      "release": "5.10.102.1-microsoft-standard-WSL2",
      "system": "Linux"
    },
    "nodeosVersion": "v3.2.0-dev"
  }
```
</details>