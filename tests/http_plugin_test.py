#!/usr/bin/env python3

from TestHarness import Cluster, Node, ReturnType, TestHelper, Utils

###############################################################
# http_plugin_tests.py
#
# Integration tests for HTTP plugin
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit
cmdError=Utils.cmdError

args = TestHelper.parse_args({"-v", "--dump-error-details","--keep-logs","--unshared"})
debug=args.v
keepLogs = args.keep_logs
dumpErrorDetails = dumpErrorDetails=args.dump_error_details


Utils.Debug=debug
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)

testSuccessful=False

ClientName="cleos"
timeout = .5 * 12 * 2 + 60 # time for finalization with 1 producer + 60 seconds padding
Utils.setIrreversibleTimeout(timeout)

try:
    TestHelper.printSystemInfo("BEGIN")

    Print("Stand up cluster")

    if cluster.launch(dontBootstrap=True, loadSystemContract=False) is False:
        cmdError("launcher")
        errorExit("Failed to stand up eos cluster.")

    Print("Getting cluster info")
    cluster.getInfos()
    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, None, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
        