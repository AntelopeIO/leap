#!/usr/bin/env python3

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# transition_to_if
#
# Transition to instant-finality with multiple producers
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag="--plugin",action='append',type=str,help="Run nodes with additional plugins")

args=TestHelper.parse_args({"-p","-n","-d","-s","--keep-logs","--prod-count",
                            "--dump-error-details","-v","--leave-running"
                            ,"--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=args.p
delay=args.d
topo=args.s
debug=args.v
prod_count = args.prod_count
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
    # For now do not load system contract as it does not support setfinalizer
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, topo=topo, delay=delay, loadSystemContract=False,
                      activateIF=False, extraNodeosArgs=extraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    cluster.activateInstantFinality(biosFinalizer=False)

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
