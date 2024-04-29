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
args=TestHelper.parse_args({"-p","-d","-s","--keep-logs","--dump-error-details","-v","--leave-running","--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=args.p if args.p > 4 else 4
delay=args.d
topo=args.s
debug=args.v
prod_count = 1 # per node prod count
total_nodes=pnodes+1
irreversibleNodeId=pnodes
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, topology: {topo}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    numTrxGenerators=2
    Print("Stand up cluster")
    # For now do not load system contract as it does not support setfinalizer
    specificExtraNodeosArgs = { irreversibleNodeId: "--read-mode irreversible"}
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, prodCount=prod_count, maximumP2pPerHost=total_nodes+numTrxGenerators, topo=topo, delay=delay, loadSystemContract=False,
                      activateIF=False, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio", "launch should have waited for production to change"

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 10
    testTrxGenDurationSec=60*60
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[cluster.defproduceraAccount.name, cluster.defproducerbAccount.name],
                                acctPrivKeysList=[cluster.defproduceraAccount.activePrivateKey,cluster.defproducerbAccount.activePrivateKey], nodeId=cluster.getNode(0).nodeId,
                                tpsPerGenerator=targetTpsPerGenerator, numGenerators=numTrxGenerators, durationSec=testTrxGenDurationSec,
                                waitToComplete=False)

    status = cluster.waitForTrxGeneratorsSpinup(nodeId=cluster.getNode(0).nodeId, numGenerators=numTrxGenerators)
    assert status is not None and status is not False, "ERROR: Failed to spinup Transaction Generators"

    assert cluster.activateInstantFinality(biosFinalizer=False), "Activate instant finality failed"

    assert cluster.biosNode.waitForLibToAdvance(), "Lib should advance after instant finality activated"
    assert cluster.biosNode.waitForProducer("defproducera"), "Did not see defproducera"
    assert cluster.biosNode.waitForHeadToAdvance(blocksToAdvance=13), "Head did not advance 13 blocks to next producer"
    assert cluster.biosNode.waitForLibToAdvance(), "Lib stopped advancing on biosNode"
    assert cluster.getNode(1).waitForLibToAdvance(), "Lib stopped advancing on Node 1"
    assert cluster.getNode(irreversibleNodeId).waitForLibToAdvance(), f"Lib stopped advancing on Node {irreversibleNodeId}, irreversible node"

    info = cluster.biosNode.getInfo(exitOnError=True)
    assert (info["head_block_num"] - info["last_irreversible_block_num"]) < 9, "Instant finality enabled LIB diff should be small"

    # launch setup node_00 (defproducera - defproducerf), node_01 (defproducerg - defproducerk),
    #              node_02 (defproducerl - defproducerp), node_03 (defproducerq - defproduceru)
    # with setprods of (defproducera, defproducerg, defproducerl, defproducerq)
    assert cluster.biosNode.waitForProducer("defproducerq"), "defproducerq did not produce"

    # should take effect in first block of defproducerg slot (so defproducerh)
    assert cluster.setProds(["defproducerb", "defproducerh", "defproducerm", "defproducerr"]), "setprods failed"
    setProdsBlockNum = cluster.biosNode.getBlockNum()
    assert cluster.biosNode.waitForBlock(setProdsBlockNum+12+12+1), "Block of new producers not reached"
    assert cluster.biosNode.getInfo(exitOnError=True)["head_block_producer"] == "defproducerh", "setprods should have taken effect"
    assert cluster.getNode(4).waitForBlock(setProdsBlockNum + 12 + 12 + 1), "Block of new producers not reached on irreversible node"

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
