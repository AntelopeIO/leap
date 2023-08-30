#!/usr/bin/env python3

import signal
import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys
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

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    if args.plugin:
        extraNodeosArgs = ''.join([i+j for i,j in zip([' --plugin '] * len(args.plugin), args.plugin)])
    else:
        extraNodeosArgs = ''
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
    targetTpsPerGenerator = 100
    testTrxGenDurationSec=60
    trxGeneratorCnt=1
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[accounts[0].name,accounts[1].name],
                                acctPrivKeysList=[account1PrivKey,account2PrivKey], nodeId=prodNode.nodeId, tpsPerGenerator=targetTpsPerGenerator,
                                numGenerators=trxGeneratorCnt, durationSec=testTrxGenDurationSec, waitToComplete=True)

    endLargeBlocksHeadBlock = nonProdNode.getHeadBlockNum()

    throttlingNode = cluster.unstartedNodes[0]
    i = throttlingNode.cmd.index('--p2p-listen-endpoint')
    throttleListenAddr = throttlingNode.cmd[i+1]
    # Using 40000 bytes per second to allow syncing of 10,000 byte blocks resulting from
    # the trx generators in a reasonable amount of time, while still being reliably
    # distinguishable from unthrottled throughput.
    throttlingNode.cmd[i+1] = throttlingNode.cmd[i+1] + ':40000B/s'
    throttleListenIP, throttleListenPort = throttleListenAddr.split(':')
    throttlingNode.cmd.append('--p2p-listen-endpoint')
    throttlingNode.cmd.append(f'{throttleListenIP}:{int(throttleListenPort)+100}:1TB/s')

    cluster.biosNode.kill(signal.SIGTERM)
    clusterStart = time.time()
    cluster.launchUnstarted(2)

    throttledNode = cluster.getNode(3)
    time.sleep(15)
    # Throttling node was offline during block generation and once online receives blocks as fast as possible while
    # transmitting blocks to the next node in line at the above throttle setting.
    assert throttlingNode.waitForBlock(endLargeBlocksHeadBlock), f'wait for block {endLargeBlocksHeadBlock}  on throttled node timed out'
    endThrottlingSync = time.time()
    # Throttled node is connecting to a listen port with a block sync throttle applied so it will receive
    # blocks more slowly during syncing than an unthrottled node.
    assert throttledNode.waitForBlock(endLargeBlocksHeadBlock, timeout=90), f'Wait for block {endLargeBlocksHeadBlock} on sync node timed out'
    endThrottledSync = time.time()
    Print(f'Unthrottled sync time: {endThrottlingSync - clusterStart} seconds')
    Print(f'Throttled sync time: {endThrottledSync - clusterStart} seconds')
    # 15 seconds chosen as the minimum reasonable sync time differential given the throttle and the average block size.
    assert endThrottledSync - clusterStart > endThrottlingSync - clusterStart + 15, 'Throttled sync time must be at least 15 seconds greater than unthrottled'

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
