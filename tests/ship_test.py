#!/usr/bin/env python3

from datetime import datetime
from datetime import timedelta
import time
import json
import os
import re
import shutil
import signal
import sys

from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs

###############################################################
# ship_test
# 
# This test sets up <-p> producing node(s) and <-n - -p>
#   non-producing node(s). One of the non-producing nodes
#   is configured with the state_history_plugin.  An instance
#   of node will be started with a client to exercise
#   the SHiP API.
#
###############################################################

Print=Utils.Print

appArgs = AppArgs()
extraArgs = appArgs.add(flag="--num-requests", type=int, help="How many requests that each ship_client requests", default=1)
extraArgs = appArgs.add(flag="--num-clients", type=int, help="How many ship_clients should be started", default=1)
extraArgs = appArgs.add_bool(flag="--unix-socket", help="Run ship over unix socket")
args = TestHelper.parse_args({"-p", "-n","--activate-if","--dump-error-details","--keep-logs","-v","--leave-running","--unshared"}, applicationSpecificArgs=appArgs)

Utils.Debug=args.v
totalProducerNodes=args.p
totalNodes=args.n
if totalNodes<=totalProducerNodes:
    totalNodes=totalProducerNodes+1
totalNonProducerNodes=totalNodes-totalProducerNodes
totalProducers=totalProducerNodes
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
activateIF=args.activate_if
dumpErrorDetails=args.dump_error_details
walletPort=TestHelper.DEFAULT_WALLET_PORT

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
shipTempDir=None

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    Print("Stand up cluster")
    specificExtraNodeosArgs={}
    # non-producing nodes are at the end of the cluster's nodes, so reserving the last one for state_history_plugin
    shipNodeNum = totalNodes - 1
    specificExtraNodeosArgs[shipNodeNum]="--plugin eosio::state_history_plugin --sync-fetch-span 200 --plugin eosio::net_api_plugin "

    if args.unix_socket:
        specificExtraNodeosArgs[shipNodeNum] += "--state-history-unix-socket-path ship.sock"

    if cluster.launch(pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducers, activateIF=activateIF,
                      specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")

    # ***   identify each node (producers and non-producing node)   ***

    shipNode = cluster.getNode(shipNodeNum)
    prodNode = cluster.getNode(0)

    #verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)
    Print("Cluster in Sync")

    shipClient = "tests/ship_client"
    cmd = "%s --num-requests %d" % (shipClient, args.num_requests)
    if args.unix_socket:
        cmd += " -a ws+unix:///%s" % (Utils.getNodeDataDir(shipNodeNum, "ship.sock"))
    if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
    clients = []
    files = []
    shipTempDir = os.path.join(Utils.DataDir, "ship")
    os.makedirs(shipTempDir, exist_ok = True)
    shipClientFilePrefix = os.path.join(shipTempDir, "client")

    starts = []
    for i in range(0, args.num_clients):
        start = time.perf_counter()
        outFile = open("%s%d.out" % (shipClientFilePrefix, i), "w")
        errFile = open("%s%d.err" % (shipClientFilePrefix, i), "w")
        Print("Start client %d" % (i))
        popen=Utils.delayedCheckOutput(cmd, stdout=outFile, stderr=errFile)
        starts.append(time.perf_counter())
        clients.append((popen, cmd))
        files.append((outFile, errFile))
        Print("Client %d started, Ship node head is: %s" % (i, shipNode.getBlockNum()))

    Print("Stopping all %d clients" % (args.num_clients))

    for index, (popen, _), (out, err), start in zip(range(len(clients)), clients, files, starts):
        popen.wait()
        Print("Stopped client %d.  Ran for %.3f seconds." % (index, time.perf_counter() - start))
        out.close()
        err.close()

    Print("Shutdown state_history_plugin nodeos")
    shipNode.kill(signal.SIGTERM)

    files = None

    maxFirstBN = -1
    minLastBN = sys.maxsize
    for index in range(0, len(clients)):
        done = False
        shipClientErrorFile = "%s%d.err" % (shipClientFilePrefix, i)
        with open(shipClientErrorFile, "r") as errFile:
            statuses = None
            lines = errFile.readlines()

            try:
                statuses = json.loads(" ".join(lines))
            except json.decoder.JSONDecodeError as er:
                Utils.errorExit("ship_client output was malformed in %s. Exception: %s" % (shipClientErrorFile, er))

            for status in statuses:
                statusDesc = status["status"]
                if statusDesc == "done":
                    done = True
                    firstBlockNum = status["first_block_num"]
                    lastBlockNum = status["last_block_num"]
                    maxFirstBN = max(maxFirstBN, firstBlockNum)
                    minLastBN = min(minLastBN, lastBlockNum)
                if statusDesc == "error":
                    Utils.errorExit("ship_client reporting error see: %s." % (shipClientErrorFile))

        assert done, Print("ERROR: Did not find a \"done\" status for client %d" % (i))

    Print("All clients active from block num: %s to block_num: %s." % (maxFirstBN, minLastBN))

    stderrFile=Utils.getNodeDataDir(shipNodeNum, "stderr.txt")
    biggestDelta = timedelta(seconds=0)
    totalDelta = timedelta(seconds=0)
    timeCount = 0
    with open(stderrFile, 'r') as f:
        line = f.readline()
        while line:
            match = re.search(r'info\s+([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3})\s.+Received\sblock\s+.+\s#([0-9]+)\s@\s([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3})',
                              line)
            if match:
                rcvTimeStr = match.group(1)
                prodTimeStr = match.group(3)
                blockNum = int(match.group(2))
                if blockNum > maxFirstBN:
                    # ship requests can only affect time after clients started
                    rcvTime = datetime.strptime(rcvTimeStr, Utils.TimeFmt)
                    prodTime = datetime.strptime(prodTimeStr, Utils.TimeFmt)
                    delta = rcvTime - prodTime
                    biggestDelta = max(delta, biggestDelta)

                    totalDelta += delta
                    timeCount += 1
                    limit = timedelta(seconds=0.500)
                    if delta >= limit:
                        actualProducerTimeStr=None
                        nodes = [node for node in cluster.getAllNodes() if node.nodeId != shipNodeNum]
                        for node in nodes:
                            threshold=-500   # set negative to guarantee the block analysis gets returned
                            blockAnalysis = node.analyzeProduction(specificBlockNum=blockNum, thresholdMs=threshold)
                            actualProducerTimeStr = blockAnalysis[blockNum]["prod"]
                            if actualProducerTimeStr is not None:
                                break
                        if actualProducerTimeStr is not None:
                            actualProducerTime = datetime.strptime(actualProducerTimeStr, Utils.TimeFmt)
                            if rcvTime - actualProducerTime >= limit:
                                actualProducerTime = None   # let it fail below

                        if actualProducerTimeStr is None:
                            Utils.errorExit("block_num: %s took %.3f seconds to be received." % (blockNum, delta.total_seconds()))

            line = f.readline()

    avg = totalDelta.total_seconds() / timeCount if timeCount > 0 else 0.0
    Print("Greatest delay time: %.3f seconds, average delay time: %.3f seconds" % (biggestDelta.total_seconds(), avg))

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)
    if shipTempDir is not None:
        if testSuccessful and not args.keep_logs:
            shutil.rmtree(shipTempDir, ignore_errors=True)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
