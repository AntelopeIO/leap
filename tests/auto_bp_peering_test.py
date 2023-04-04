#!/usr/bin/env python3

import re
import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, ReturnType

###############################################################
# auto_bp_peering_test
#
# This test sets up  a cluster with 21 producers nodeos, each nodeos is configured with only one producer and only connects to the bios node.
# Moreover, each producer nodeos is also configured with a list of p2p-auto-bp-peer so that each one can automatically establish p2p connections to
# their downstream two neighbors based on producer schedule on the chain and tear down the connections which are no longer in the scheduling neighborhood.
#
###############################################################

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
producerNodes = 21
producerCountInEachNode = 1
totalNodes = producerNodes

# Parse command line arguments
args = TestHelper.parse_args({
    "-v",
    "--clean-run",
    "--dump-error-details",
    "--leave-running",
    "--keep-logs",
    "--unshared"
})

Utils.Debug = args.v
killAll = args.clean_run
dumpErrorDetails = args.dump_error_details
dontKill = args.leave_running
killEosInstances = not dontKill
killWallet = not dontKill
keepLogs = args.keep_logs

# Setup cluster and it's wallet manager
walletMgr = WalletMgr(True)
cluster = Cluster(walletd=True)
cluster.setWalletMgr(walletMgr)


peer_names = {}

auto_bp_peer_args = ""
for nodeId in range(0, producerNodes):
    producer_name = "defproducer" + chr(ord('a') + nodeId)
    port = cluster.p2pBasePort + nodeId
    hostname = "localhost:" + str(port)
    peer_names[hostname] = producer_name
    auto_bp_peer_args += (" --p2p-auto-bp-peer " + producer_name + "," + hostname)


def neigbors_in_schedule(name, schedule):
    index = schedule.index(name)
    result = []
    num = len(schedule)
    result.append(schedule[(index+1) % num])
    result.append(schedule[(index+2) % num])
    result.append(schedule[(index-1) % num])
    result.append(schedule[(index-2) % num])
    return result.sort()


peer_names["localhost:9776"] = "bios"

testSuccessful = False
try:
    specificNodeosArgs = {}
    for nodeId in range(0, producerNodes):
        specificNodeosArgs[nodeId] = auto_bp_peer_args

    # Kill any existing instances and launch cluster
    TestHelper.printSystemInfo("BEGIN")
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    cluster.launch(
        prodCount=producerCountInEachNode,
        totalNodes=totalNodes,
        pnodes=producerNodes,
        totalProducers=producerNodes,
        topo="./tests/auto_bp_peering_test_shape.json",
        extraNodeosArgs=" --plugin eosio::net_api_plugin ",
        specificExtraNodeosArgs=specificNodeosArgs,
    )

    # wait until produceru is seen by every node
    for nodeId in range(0, producerNodes):
        print("Wait for node ", nodeId)
        cluster.nodes[nodeId].waitForProducer(
            "defproduceru", exitOnError=True, timeout=300)

    # retrive the producer stable producer schedule
    scheduled_producers = []
    schedule = cluster.nodes[0].processUrllibRequest(
        "chain", "get_producer_schedule")
    for prod in schedule["payload"]["active"]["producers"]:
        scheduled_producers.append(prod["producer_name"])

    connection_check_failures = 0
    for nodeId in range(0, producerNodes):
        # retrive the connections in each node and check if each only connects to their neighbors in the schedule
        connections = cluster.nodes[nodeId].processUrllibRequest(
            "net", "connections")
        peers = []
        for conn in connections["payload"]:
            peer_addr = conn["peer"]
            if len(peer_addr) == 0:
                peer_addr = conn["last_handshake"]["p2p_address"].split()[0]
            if peer_names[peer_addr] != "bios":
                peers.append(peer_names[peer_addr])
                if not conn["is_bp_peer"]:
                    Utils.Print("Error: expected connection to {} with is_bp_peer as true".format(peer_addr))
                    connection_check_failures = connection_check_failures+1

        peers = peers.sort()
        name = "defproducer" + chr(ord('a') + nodeId)
        expected_peers = neigbors_in_schedule(name, scheduled_producers)
        if peers != expected_peers:
            Utils.Print("ERROR: expect {} has connections to {}, got connections to {}".format(
                name, expected_peers, peers))
            connection_check_failures = connection_check_failures+1

    testSuccessful = (connection_check_failures == 0)

finally:
    TestHelper.shutdown(
        cluster,
        walletMgr,
        testSuccessful,
        killEosInstances,
        killWallet,
        keepLogs,
        killAll,
        dumpErrorDetails
    )

exitCode = 0 if testSuccessful else 1
exit(exitCode)
