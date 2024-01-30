#!/usr/bin/env python3

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# cluster_launcher
#
# Smoke test for TestHarness launching and bootstrapping a cluster.
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag="--plugin",action='append',type=str,help="Run nodes with additional plugins")

args=TestHelper.parse_args({"-p","-n","-d","-s","--keep-logs","--prod-count",
                            "--activate-if","--dump-error-details","-v","--leave-running"
                            ,"--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=args.p
delay=args.d
topo=args.s
debug=args.v
prod_count = args.prod_count
activateIF=args.activate_if
total_nodes=args.n if args.n > 0 else pnodes
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
    if args.plugin:
        extraNodeosArgs = ''.join([i+j for i,j in zip([' --plugin '] * len(args.plugin), args.plugin)])
    else:
        extraNodeosArgs = ''
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, topo=topo, delay=delay, 
                      activateIF=activateIF, extraNodeosArgs=extraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
