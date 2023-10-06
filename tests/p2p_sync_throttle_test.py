#!/usr/bin/env python3

import math
import re
import signal
import sys
import time
import urllib

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys, ReturnType
from TestHarness.TestHelper import AppArgs

###############################################################
# p2p_sync_throttle_test
#
# Test throttling of a peer during block syncing.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs = AppArgs()
appArgs.add(flag='--plugin',action='append',type=str,help='Run nodes with additional plugins')
appArgs.add(flag='--connection-cleanup-period',type=int,help='Interval in whole seconds to run the connection reaper and metric collection')

args=TestHelper.parse_args({"-p","-d","--keep-logs","--prod-count"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--unshared"},
                            applicationSpecificArgs=appArgs)
pnodes=args.p
delay=args.d
debug=args.v
prod_count = args.prod_count
total_nodes=4
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

def extractPrometheusMetric(connID: str, metric: str, text: str):
    searchStr = f'nodeos_p2p_connections{{connid_{connID}="{metric}"}} '
    begin = text.find(searchStr) + len(searchStr)
    return int(text[begin:text.find('\n', begin)])

prometheusHostPortPattern = re.compile(r'^nodeos_p2p_connections.connid_([0-9])="localhost:([0-9]*)', re.MULTILINE)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    extraNodeosArgs = '--plugin eosio::prometheus_plugin --connection-cleanup-period 3'
    # Custom topology is a line of singlely connected nodes from highest node number in sequence to lowest,
    # the reverse of the usual TestHarness line topology.
    if cluster.launch(pnodes=pnodes, unstartedNodes=2, totalNodes=total_nodes, prodCount=prod_count, 
                      topo='./tests/p2p_sync_throttle_test_shape.json', delay=delay, 
                      extraNodeosArgs=extraNodeosArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    prodNode = cluster.getNode(0)
    nonProdNode = cluster.getNode(1)

    accounts=createAccountKeys(2)
    if accounts is None:
        Utils.errorExit("FAILURE - create keys")

    accounts[0].name="tester111111"
    accounts[1].name="tester222222"

    account1PrivKey = accounts[0].activePrivateKey
    account2PrivKey = accounts[1].activePrivateKey

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, [cluster.eosioAccount,accounts[0],accounts[1]])

    # create accounts via eosio as otherwise a bid is needed
    for account in accounts:
        Print("Create new account %s via %s" % (account.name, cluster.eosioAccount.name))
        trans=nonProdNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=True, stakeNet=1000, stakeCPU=1000, buyRAM=1000, exitOnError=True)
        transferAmount="100000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        nonProdNode.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=True)
        trans=nonProdNode.delegatebw(account, 20000000.0000, 20000000.0000, waitForTransBlock=True, exitOnError=True)

    beginLargeBlocksHeadBlock = nonProdNode.getHeadBlockNum()

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 200
    testTrxGenDurationSec=60
    trxGeneratorCnt=1
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[accounts[0].name,accounts[1].name],
                                acctPrivKeysList=[account1PrivKey,account2PrivKey], nodeId=prodNode.nodeId, tpsPerGenerator=targetTpsPerGenerator,
                                numGenerators=trxGeneratorCnt, durationSec=testTrxGenDurationSec, waitToComplete=True)

    endLargeBlocksHeadBlock = nonProdNode.getHeadBlockNum()

    throttlingNode = cluster.unstartedNodes[0]
    i = throttlingNode.cmd.index('--p2p-listen-endpoint')
    throttleListenAddr = throttlingNode.cmd[i+1]
    # Using 5000 bytes per second to allow syncing of ~100 transaction blocks resulting from
    # the trx generators in a reasonable amount of time, while still being able to capture
    # throttling state within the Prometheus update window (3 seconds in this test).
    throttlingNode.cmd[i+1] = throttlingNode.cmd[i+1] + ':5000B/s'
    throttleListenIP, throttleListenPort = throttleListenAddr.split(':')
    throttlingNode.cmd.append('--p2p-listen-endpoint')
    throttlingNode.cmd.append(f'{throttleListenIP}:{int(throttleListenPort)+100}:1TB/s')

    cluster.biosNode.kill(signal.SIGTERM)
    clusterStart = time.time()
    cluster.launchUnstarted(2)

    throttledNode = cluster.getNode(3)
    while time.time() < clusterStart + 30:
        try:
            response = throttlingNode.processUrllibRequest('prometheus', 'metrics', returnType=ReturnType.raw, printReturnLimit=16).decode()
        except urllib.error.URLError:
            # catch ConnectionRefusedEror waiting for node to finish startup and respond
            time.sleep(0.5)
            continue
        else:
            if len(response) < 100:
                # tolerate HTTPError as well (method returns only the exception code)
                continue
            connPorts = prometheusHostPortPattern.findall(response)
            if len(connPorts) < 3:
                # wait for node to be connected
                time.sleep(0.5)
                continue
            Print('Throttling Node Start State')
            throttlingNodePortMap = {port: id for id, port in connPorts}
            startSyncThrottlingBytesSent = extractPrometheusMetric(throttlingNodePortMap['9879'],
                                                                    'block_sync_bytes_sent',
                                                                    response)
            startSyncThrottlingState = extractPrometheusMetric(throttlingNodePortMap['9879'],
                                                               'block_sync_throttling',
                                                               response)
            Print(f'Start sync throttling bytes sent: {startSyncThrottlingBytesSent}')
            Print(f'Start sync throttling node throttling: {"True" if startSyncThrottlingState else "False"}')
            break
    else:
        errorExit('Timed out')

    while time.time() < clusterStart + 30:
        try:
            response = throttledNode.processUrllibRequest('prometheus', 'metrics', returnType=ReturnType.raw, printReturnLimit=16).decode()
        except urllib.error.URLError:
            # catch ConnectionRefusedError waiting for node to finish startup and respond
            time.sleep(0.5)
            continue
        else:
            if len(response) < 100:
                # tolerate HTTPError as well (method returns only the exception code)
                time.sleep(0.5)
                continue
            connPorts = prometheusHostPortPattern.findall(response)
            if len(connPorts) < 2:
                # wait for sending node to be connected
                continue
            Print('Throttled Node Start State')
            throttledNodePortMap = {port: id for id, port in connPorts}
            startSyncThrottledBytesReceived = extractPrometheusMetric(throttledNodePortMap['9878'],
                                                                        'block_sync_bytes_received',
                                                                        response)
            Print(f'Start sync throttled bytes received: {startSyncThrottledBytesReceived}')
            break
    else:
        errorExit('Timed out')

    # Throttling node was offline during block generation and once online receives blocks as fast as possible while
    # transmitting blocks to the next node in line at the above throttle setting.
    assert throttlingNode.waitForBlock(endLargeBlocksHeadBlock), f'wait for block {endLargeBlocksHeadBlock}  on throttled node timed out'
    endThrottlingSync = time.time()
    response = throttlingNode.processUrllibRequest('prometheus', 'metrics', exitOnError=True, returnType=ReturnType.raw, printReturnLimit=16).decode()
    Print('Throttling Node End State')
    endSyncThrottlingBytesSent = extractPrometheusMetric(throttlingNodePortMap['9879'],
                                                            'block_sync_bytes_sent',
                                                            response)
    Print(f'End sync throttling bytes sent: {endSyncThrottlingBytesSent}')
    # Throttled node is connecting to a listen port with a block sync throttle applied so it will receive
    # blocks more slowly during syncing than an unthrottled node.
    wasThrottled = False
    while time.time() < endThrottlingSync + 30:
        response = throttlingNode.processUrllibRequest('prometheus', 'metrics', exitOnError=True,
                                                       returnType=ReturnType.raw, printReturnLimit=16).decode()
        throttledState = extractPrometheusMetric(throttlingNodePortMap['9879'],
                                                 'block_sync_throttling',
                                                 response)
        if throttledState:
            wasThrottled = True
            break
    assert throttledNode.waitForBlock(endLargeBlocksHeadBlock, timeout=90), f'Wait for block {endLargeBlocksHeadBlock} on sync node timed out'
    endThrottledSync = time.time()
    response = throttledNode.processUrllibRequest('prometheus', 'metrics', exitOnError=True, returnType=ReturnType.raw, printReturnLimit=16).decode()
    Print('Throttled Node End State')
    endSyncThrottledBytesReceived = extractPrometheusMetric(throttledNodePortMap['9878'],
                                                            'block_sync_bytes_received',
                                                            response)
    Print(f'End sync throttled bytes received: {endSyncThrottledBytesReceived}')
    throttlingElapsed = endThrottlingSync - clusterStart
    throttledElapsed = endThrottledSync - clusterStart
    Print(f'Unthrottled sync time: {throttlingElapsed} seconds')
    Print(f'Throttled sync time: {throttledElapsed} seconds')
    # Sanity check
    assert throttledElapsed > throttlingElapsed + 10, 'Throttled sync time must be at least 10 seconds greater than unthrottled'
    assert wasThrottled, 'Throttling node never reported throttling its transmission rate'

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
