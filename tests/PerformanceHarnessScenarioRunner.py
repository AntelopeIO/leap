#!/usr/bin/env python3

import argparse
import sys

from PerformanceHarness import performance_test_basic, performance_test
from TestHarness import Utils

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

class ScenarioArgumentsHandler(object):

    @staticmethod
    def createArgumentParser():

        scenarioParser = argparse.ArgumentParser(add_help=True, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptbParser=performance_test_basic.PtbArgumentsHandler.createArgumentParser()
        ptParser=performance_test.PerfTestArgumentsHandler.createArgumentParser()

        #Let top level performance harness parser know there will be sub-commands, and that a scenario type sub-command is required
        scenarioTypeDesc=("Each Scenario Type sets up either a Performance Test Basic or a Performance Test scenario and allows further configuration of the scenario.")
        scenarioParserSubparsers = scenarioParser.add_subparsers(title="Scenario Types",
                                                     description=scenarioTypeDesc,
                                                     dest="scenario_type_sub_cmd",
                                                     required=True, help="Currently supported scenario type sub-commands.")


        #Create the Single Test Scenario Type Sub-Command and Parsers
        scenarioParserSubparsers.add_parser(name="singleTest", parents=[ptbParser], add_help=False, help="Run a single Performance Test Basic test scenario.")

        #Create the Find Max Test Scenario Type Sub-Command and Parsers
        scenarioParserSubparsers.add_parser(name="findMax", parents=[ptParser], add_help=False, help="Runs a Performance Test scenario that iteratively runs performance test basic test scenarios to determine a max tps.")

        return scenarioParser

    @staticmethod
    def parseArgs():
        scenarioParser=ScenarioArgumentsHandler.createArgumentParser()
        args=scenarioParser.parse_args()
        return args

def main():

    args = ScenarioArgumentsHandler.parseArgs()
    Utils.Debug = args.v

    testHelperConfig = performance_test_basic.PerformanceTestBasic.setupTestHelperConfig(args)
    testClusterConfig = performance_test_basic.PerformanceTestBasic.setupClusterConfig(args)

    if args.contracts_console and testClusterConfig.loggingLevel != "debug" and testClusterConfig.loggingLevel != "all":
        print("Enabling contracts-console will not print anything unless debug level is 'debug' or higher."
              f" Current debug level is: {testClusterConfig.loggingLevel}")

    if args.scenario_type_sub_cmd == "singleTest":
        ptbConfig = performance_test_basic.PerformanceTestBasic.PtbConfig(targetTps=args.target_tps,
                                                testTrxGenDurationSec=args.test_duration_sec,
                                                tpsLimitPerGenerator=args.tps_limit_per_generator,
                                                numAddlBlocksToPrune=args.num_blocks_to_prune,
                                                logDirRoot=".",
                                                delReport=args.del_report, quiet=args.quiet,
                                                delPerfLogs=args.del_perf_logs,
                                                printMissingTransactions=args.print_missing_transactions,
                                                userTrxDataFile=Path(args.user_trx_data_file) if args.user_trx_data_file is not None else None,
                                                endpointMode=args.endpoint_mode,
                                                trxGenerator=args.trx_generator,
                                                saveState=args.save_state)
        Utils.Print(f"testNamePath: {PurePath(PurePath(__file__).name).stem}")
        myTest = performance_test_basic.PerformanceTestBasic(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, ptbConfig=ptbConfig, testNamePath=f"{PurePath(PurePath(__file__).name).stem}")
    elif args.scenario_type_sub_cmd == "findMax":
        ptConfig = performance_test.PerformanceTest.PtConfig(testDurationSec=args.test_iteration_duration_sec,
                                            finalDurationSec=args.final_iterations_duration_sec,
                                            delPerfLogs=args.del_perf_logs,
                                            maxTpsToTest=args.max_tps_to_test,
                                            minTpsToTest=args.min_tps_to_test,
                                            testIterationMinStep=args.test_iteration_min_step,
                                            tpsLimitPerGenerator=args.tps_limit_per_generator,
                                            delReport=args.del_report,
                                            delTestReport=args.del_test_report,
                                            numAddlBlocksToPrune=args.num_blocks_to_prune,
                                            quiet=args.quiet,
                                            logDirRoot=Path("."),
                                            skipTpsTests=args.skip_tps_test,
                                            calcChainThreads=args.calc_chain_threads,
                                            calcNetThreads=args.calc_net_threads,
                                            userTrxDataFile=Path(args.user_trx_data_file) if args.user_trx_data_file is not None else None,
                                            endpointMode=args.endpoint_mode,
                                            opModeCmd=args.op_mode_sub_cmd,
                                            trxGenerator=args.trx_generator,
                                            saveState=args.save_state)

        myTest = performance_test.PerformanceTest(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, ptConfig=ptConfig)
    else:
        Utils.Print(f"Unknown Scenario Type: {args.scenario_type_sub_cmd}")
        exit(-1)

    testSuccessful = myTest.runTest()

    exitCode = 0 if testSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()