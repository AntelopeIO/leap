#!/usr/bin/env python3

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# transition_to_if
#
# Transition to instant-finality with multiple producers (at least 4).
#
###############################################################


Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag="--plugin",action='append',type=str,help="Run nodes with additional plugins")

args=TestHelper.parse_args({"-d","-s","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=4
delay=args.d
topo=args.s
debug=args.v
prod_count = 1 # per node prod count
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
    if args.plugin:
        extraNodeosArgs = ''.join([i+j for i,j in zip([' --plugin '] * len(args.plugin), args.plugin)])
    else:
        extraNodeosArgs = ''
    # For now do not load system contract as it does not support setfinalizer
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, topo=topo, delay=delay, loadSystemContract=False,
                      activateIF=False, extraNodeosArgs=extraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    assert cluster.activateInstantFinality(biosFinalizer=False), "Activate instant finality failed"

    assert cluster.biosNode.waitForLibToAdvance(), "Lib should advance after instant finality activated"
    assert cluster.biosNode.waitForProducer("defproducera"), "Did not see defproducera"
    assert cluster.biosNode.waitForHeadToAdvance(blocksToAdvance=13) # into next producer
    assert cluster.biosNode.waitForLibToAdvance(), "Lib stopped advancing"

    info = cluster.biosNode.getInfo(exitOnError=True)
    assert (info["head_block_num"] - info["last_irreversible_block_num"]) < 9, "Instant finality enabled LIB diff should be small"

    # launch setup node_00 (defproducera - defproducerf), node_01 (defproducerg - defproducerk),
    #              node_02 (defproducerl - defproducerp), node_03 (defproducerq - defproduceru)
    # with setprods of (defproducera, defproducerg, defproducerl, defproducerq)
    assert cluster.biosNode.waitForProducer("defproducerq"), "defproducerq did not produce"

    # should take effect in first block of defproducerg slot (so defproducerh)
    assert cluster.setProds(["defproducerb", "defproducerh", "defproducerm", "defproducerr"]), "setprods failed"
    setProdsBlockNum = cluster.biosNode.getBlockNum()
    cluster.biosNode.waitForBlock(setProdsBlockNum+12+12+1)
    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] == "defproducerh", "setprods should have taken effect"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
