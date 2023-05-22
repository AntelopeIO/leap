#!/usr/bin/env python3

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# cluster_launcher
#
# Smoke test for TestHarness launching and bootstrapping a cluster.
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-p","-d","-s","--keep-logs"
                            ,"--dump-error-details","-v"
                            ,"--leave-running","--unshared"})
pnodes=args.p
delay=args.d
topo=args.s
debug=args.v
total_nodes=pnodes
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, topology: {topo}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo=topo, delay=delay) is False:
        errorExit("Failed to stand up eos cluster.")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
