#!/usr/bin/env python3

import signal

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr

###############################################################
# liveness_test -- Test IF liveness loss and recovery. Only applicable to Instant Finality.
#
#  To save testing time, 2 producer nodes are set up in Mesh mode.
#  Quorum is 2. At the beginning, LIB advances on both nodes.
#  Kill one node to simulate loss of liveness because quorum cannot be met -- LIB stops advancing.
#  Relaunch the killed node to verify recovery of liveness -- LIB resumes advancing.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running",
                              "--wallet-port","--unshared"})
Utils.Debug=args.v
totalProducerNodes=2
totalNodes=totalProducerNodes
totalProducers=2
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
activateIF=True
dumpErrorDetails=args.dump_error_details
walletPort=args.wallet_port

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")

    # ***   setup topogrophy   ***
    # "mesh" shape connects nodeA and nodeA to each other
    if cluster.launch(topo="mesh", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducerNodes, loadSystemContract=False,
                      activateIF=activateIF, biosFinalizer=False) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")
    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    prodA = cluster.getNode(0)
    prodB = cluster.getNode(1)

    # verify nodes are in sync and advancing
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)

    prodA.kill(signal.SIGTERM)

    # verify node A is killed
    numPasses = 2
    blocksPerProducer = 12
    blocksPerRound = totalProducers * blocksPerProducer
    count = blocksPerRound * numPasses
    while prodA.verifyAlive() and count > 0:
        Print("Wait for next block")
        assert prodB.waitForNextBlock(timeout=6), "node B should continue to advance, even after node A is killed"
        count -= 1
    assert not prodA.verifyAlive(), "node A should have been killed"

    # verify head still advances but not LIB on node B
    assert prodB.waitForNextBlock(), "Head should continue to advance on node B without node A"
    assert not prodB.waitForLibToAdvance(10), "LIB should not advance on node B without node A"

    # relaunch node A so that quorum can be met
    Print("Relaunching node A to make quorum")
    if not prodA.relaunch():
        errorExit(f"Failure - node A should have restarted")

    # verify LIB advances on both nodes
    assert prodA.waitForNextBlock()
    assert prodA.waitForLibToAdvance()
    assert prodB.waitForLibToAdvance()

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
