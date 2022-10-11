#!/usr/bin/env python3

import signal
import time
import json

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr
from TestHarness.Cluster import NamedAccounts
from TestHarness.TestHelper import AppArgs
from core_symbol import CORE_SYMBOL

###############################################################
# nodeos_retry_transaction_test
# 
# This test sets up 3 producing nodes and 4 non-producing
#   nodes; 2 API nodes and 2 relay nodes. The API nodes will be
#   sent many transfers.  When it is complete it verifies that
#   all of the transactions made it into blocks.
#
# During processing the two relay nodes are killed so that
# one of the api nodes is isolated and its transactions will
# be lost. The api node retry logic should allow it to resend
# those transactions once connectivity is restored.
#
###############################################################

Print=Utils.Print

appArgs = AppArgs()
minTotalAccounts = 5
extraArgs = appArgs.add(flag="--transaction-time-delta", type=int, help="How many seconds seconds behind an earlier sent transaction should be received after a later one", default=5)
extraArgs = appArgs.add(flag="--num-transactions", type=int, help="How many total transactions should be sent", default=1000)
extraArgs = appArgs.add(flag="--max-transactions-per-second", type=int, help="How many transactions per second should be sent", default=50)
extraArgs = appArgs.add(flag="--total-accounts", type=int, help="How many accounts should be involved in sending transfers.  Must be greater than %d" % (minTotalAccounts), default=10)
args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--clean-run"}, applicationSpecificArgs=appArgs)

Utils.Debug=args.v
totalProducerNodes=3
totalNodes=7
totalNonProducerNodes=totalNodes-totalProducerNodes
maxActiveProducers=totalProducerNodes
totalProducers=totalProducerNodes
cluster=Cluster(walletd=True)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
killAll=args.clean_run
walletPort=TestHelper.DEFAULT_WALLET_PORT
blocksPerSec=2
transBlocksBehind=args.transaction_time_delta * blocksPerSec
numTransactions = args.num_transactions
maxTransactionsPerSecond = args.max_transactions_per_second
assert args.total_accounts >= minTotalAccounts, Print("ERROR: Only %d was selected for --total-accounts, must have at least %d" % (args.total_accounts, minTotalAccounts))
if numTransactions % args.total_accounts > 0:
    oldNumTransactions = numTransactions
    numTransactions = int((oldNumTransactions + args.total_accounts - 1)/args.total_accounts) * args.total_accounts
    Print("NOTE: --num-transactions passed as %d, but rounding to %d so each of the %d accounts gets the same number of transactions" %
          (oldNumTransactions, numTransactions, args.total_accounts))
numRounds = int(numTransactions / args.total_accounts)
assert numRounds > 3, Print("ERROR: Need more than three rounds: %d" % numRounds)


walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
killEosInstances=not dontKill
killWallet=not dontKill

WalletdName=Utils.EosWalletName
ClientName="cleos"

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    Print("Stand up cluster")

    specificExtraNodeosArgs={
        3:"--transaction-retry-max-storage-size-gb 5 --disable-api-persisted-trx", # api node
        4:"--disable-api-persisted-trx",                                           # relay only, will be killed
        5:"--transaction-retry-max-storage-size-gb 5",                             # api node, will be isolated
        6:"--disable-api-persisted-trx"                                            # relay only, will be killed
    }

    # topo=ring all nodes are connected in a ring but also to the bios node
    if cluster.launch(pnodes=totalProducerNodes, totalNodes=totalNodes, totalProducers=totalProducers,
                      topo="ring",
                      specificExtraNodeosArgs=specificExtraNodeosArgs,
                      useBiosBootFile=False) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")

    cluster.waitOnClusterSync(blockAdvancing=5)
    Utils.Print("Cluster in Sync")
    cluster.biosNode.kill(signal.SIGTERM)
    Utils.Print("Bios node killed")
    # need bios to pass along blocks so api node can continue without its other peer, but drop trx which is the point of this test
    Utils.Print("Restart bios in drop transactions mode")
    cluster.biosNode.relaunch("bios", cachePopen=True, addSwapFlags={"--p2p-accept-transactions": "false"})

# ***   create accounts to vote in desired producers   ***

    Print("creating %d accounts" % (args.total_accounts))
    namedAccounts=NamedAccounts(cluster,args.total_accounts)
    accounts=namedAccounts.accounts

    accountsToCreate = [cluster.eosioAccount]
    for account in accounts:
        accountsToCreate.append(account)

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, accountsToCreate)

    for _, account in cluster.defProducerAccounts.items():
        walletMgr.importKey(account, testWallet, ignoreDupKeyWarning=True)

    Print("Wallet \"%s\" password=%s." % (testWalletName, testWallet.password.encode("utf-8")))

    for account in accounts:
        walletMgr.importKey(account, testWallet)

    # ***   identify each node (producers and non-producing nodes)   ***

    prodNodes=[]
    prodNodes.append( cluster.getNode(0) )
    prodNodes.append( cluster.getNode(1) )
    prodNodes.append( cluster.getNode(2) )
    apiNodes=[]
    apiNodes.append( cluster.getNode(3) )
    apiNodes.append( cluster.getNode(5) )
    relayNodes=[]
    relayNodes.append( cluster.getNode(4) )
    relayNodes.append( cluster.getNode(6) )

    apiNodeCount = len(apiNodes)

    node=apiNodes[0]
    checkTransIds = []
    startTime = time.perf_counter()
    Print("Create new accounts via %s" % (cluster.eosioAccount.name))
    for account in accounts:
        trans = node.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=(account == accounts[-1]), stakeNet=1000, stakeCPU=1000, buyRAM=1000, exitOnError=True)
        checkTransIds.append(Node.getTransId(trans))

    nextTime = time.perf_counter()
    Print("Create new accounts took %s sec" % (nextTime - startTime))
    startTime = nextTime

    Print("Transfer funds to new accounts via %s" % (cluster.eosioAccount.name))
    for account in accounts:
        transferAmount="1000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        trans = node.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=(account == accounts[-1]), reportStatus=False)
        checkTransIds.append(Node.getTransId(trans))

    nextTime = time.perf_counter()
    Print("Transfer funds took %s sec" % (nextTime - startTime))
    startTime = nextTime

    Print("Delegate Bandwidth to new accounts")
    for account in accounts:
        trans=node.delegatebw(account, 200.0000, 200.0000, waitForTransBlock=(account == accounts[-1]), exitOnError=True, reportStatus=False)
        checkTransIds.append(Node.getTransId(trans))

    nextTime = time.perf_counter()
    Print("Delegate Bandwidth took %s sec" % (nextTime - startTime))
    startTime = nextTime
    lastIrreversibleBlockNum = None

    overdrawAccount = accounts[0]
    Print(f"Attempt to transfer more funds than available from {overdrawAccount.name} to test transaction fails with overdrawn balance, not expiration in retry.")
    overdrawTransferAmount = "1001.0000 {0}".format(CORE_SYMBOL)
    Print("Transfer funds %s from account %s to %s" % (overdrawTransferAmount, overdrawAccount.name, cluster.eosioAccount.name))
    overdrawtrans = node.transferFunds(overdrawAccount, cluster.eosioAccount, overdrawTransferAmount, "test overdraw transfer", exitOnError=False, reportStatus=False, retry=1)
    assert overdrawtrans is None, f"ERROR: Overdraw transaction attempt should have failed with overdrawn balance: {overdrawtrans}"

    def cacheTransIdInBlock(transId, transToBlock, node):
        global lastIrreversibleBlockNum
        lastPassLIB = None
        blockWaitTimeout = 60
        transTimeDelayed = False
        while True:
            trans = node.getTransaction(transId, delayedRetry=False)
            if trans is None:
                if transTimeDelayed:
                    return (None, None)
                else:
                    if Utils.Debug:
                        Print("Transaction not found for trans id: %s. Will wait %d seconds to see if it arrives in a block." %
                              (transId, args.transaction_time_delta))
                    transTimeDelayed = True
                    node.waitForTransactionInBlock(transId, timeout = args.transaction_time_delta)
                    continue

            lastIrreversibleBlockNum = node.getIrreversibleBlockNum()
            blockNum = Node.getTransBlockNum(trans)
            assert blockNum is not None, Print("ERROR: could not retrieve block num from transId: %s, trans: %s" % (transId, json.dumps(trans, indent=2)))
            block = node.getBlock(blockNum)
            if block is not None:
                transactions = block["transactions"]
                for trans_receipt in transactions:
                    btrans = trans_receipt["trx"]
                    assert btrans is not None, Print("ERROR: could not retrieve \"trx\" from transaction_receipt: %s, from transId: %s that led to block: %s" % (json.dumps(trans_receipt, indent=2), transId, json.dumps(block, indent=2)))
                    btransId = btrans["id"]
                    assert btransId is not None, Print("ERROR: could not retrieve \"id\" from \"trx\": %s, from transId: %s that led to block: %s" % (json.dumps(btrans, indent=2), transId, json.dumps(block, indent=2)))
                    transToBlock[btransId] = block

                break

            if lastPassLIB is not None and lastPassLIB >= lastIrreversibleBlockNum:
                Print("ERROR: could not find block number: %d from transId: %s, waited %d seconds for LIB to advance but it did not. trans: %s" % (blockNum, transId, blockWaitTimeout, json.dumps(trans, indent=2)))
                return (trans, None)
            if Utils.Debug:
                extra = "" if lastPassLIB is None else " and since it progressed from %d in our last pass" % (lastPassLIB)
                Print("Transaction returned for trans id: %s indicated it was in block num: %d, but that block could not be found.  LIB is %d%s, we will wait to see if the block arrives." %
                      (transId, blockNum, lastIrreversibleBlockNum, extra))
            lastPassLIB = lastIrreversibleBlockNum
            node.waitForBlock(blockNum, timeout = blockWaitTimeout)

        return (block, trans)

    def findTransInBlock(transId, transToBlock, node):
        if transId in transToBlock:
            return
        (block, trans) = cacheTransIdInBlock(transId, transToBlock, node)
        assert trans is not None, Print("ERROR: could not find transaction for transId: %s" % (transId))
        assert block is not None, Print("ERROR: could not retrieve block with block num: %d, from transId: %s, trans: %s" % (blockNum, transId, json.dumps(trans, indent=2)))

    transToBlock = {}
    for transId in checkTransIds:
        findTransInBlock(transId, transToBlock, node)

    nextTime = time.perf_counter()
    Print("Verifying transactions took %s sec" % (nextTime - startTime))
    startTransferTime = nextTime

    #verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)

    Print("Sending %d transfers" % (numTransactions))
    delayAfterRounds = int(maxTransactionsPerSecond / args.total_accounts)
    history = []
    startTime = time.perf_counter()
    startRound = time.perf_counter()
    for round in range(0, numRounds):
        # ensure we are not sending too fast
        startRound = time.perf_counter()
        timeDiff = startRound - startTime
        expectedTransactions = maxTransactionsPerSecond * timeDiff
        sentTransactions = round * args.total_accounts
        if sentTransactions > expectedTransactions:
            excess = sentTransactions - expectedTransactions
            # round up to a second
            delayTime = int((excess + maxTransactionsPerSecond - 1) / maxTransactionsPerSecond)
            Utils.Print("Delay %d seconds to keep max transactions under %d per second" % (delayTime, maxTransactionsPerSecond))
            time.sleep(delayTime)

        if round % 3 == 0:
            killTime = time.perf_counter()
            cluster.getNode(4).kill(signal.SIGTERM)
            cluster.getNode(6).kill(signal.SIGTERM)
            startRound = startRound - ( time.perf_counter() - killTime )
            startTime = startTime - ( time.perf_counter() - killTime )

        trackedTransPopens = []

        transferAmount = Node.currencyIntToStr(round + 1, CORE_SYMBOL)
        Print("Sending round %d, transfer: %s" % (round, transferAmount))
        for accountIndex in range(0, args.total_accounts):
            fromAccount = accounts[accountIndex]
            toAccountIndex = accountIndex + 1 if accountIndex + 1 < args.total_accounts else 0
            toAccount = accounts[toAccountIndex]
            node = apiNodes[accountIndex % apiNodeCount]
            popen, cmd = node.transferFundsAsync(fromAccount, toAccount, transferAmount, "transfer round %d" % (round), exitOnError=False, retry=2)
            trackedTransPopens.append((popen, cmd))

        if round % 3 == 0:
            relaunchTime = time.perf_counter()
            cluster.getNode(4).relaunch(cachePopen=True)
            cluster.getNode(6).relaunch(cachePopen=True)
            startRound = startRound - ( time.perf_counter() - relaunchTime )
            startTime = startTime - ( time.perf_counter() - relaunchTime )

        # store off the transaction id, which we can use with the node.transCache
        for i in range(0, len(trackedTransPopens)):
            trans = Utils.toJson( Utils.checkDelayedOutput(trackedTransPopens[i][0], trackedTransPopens[i][1]) )
            assert trans is not None, Print("ERROR: failed round: %d, index: %s" % (round, i))
            # store off the transaction id, which we can use with the node.transCache
            history.append(Node.getTransId(trans))


    nextTime = time.perf_counter()
    Print("Sending transfers took %s sec" % (nextTime - startTransferTime))
    startTranferValidationTime = nextTime

    blocks = {}
    transToBlock = {}
    missingTransactions = []
    transBlockOutOfOrder = []
    newestBlockNum = None
    newestBlockNumTransId = None
    newestBlockNumTransOrder = None
    lastBlockNum = None
    lastTransId = None
    transOrder = 0
    lastPassLIB = None
    for transId in history:
        blockNum = None
        block = None
        transDesc = None
        if transId not in transToBlock:
            (block, trans) = cacheTransIdInBlock(transId, transToBlock, node)
            if trans is None or block is None:
                missingTransactions.append({
                    "newer_trans_id" : transId,
                    "newer_trans_index" : transOrder,
                    "newer_bnum" : None,
                    "last_trans_id" : lastTransId,
                    "last_trans_index" : transOrder - 1,
                    "last_bnum" : lastBlockNum,
                })
                if newestBlockNum > lastBlockNum:
                    missingTransactions[-1]["highest_block_seen"] = newestBlockNum
            blockNum = Node.getTransBlockNum(trans)
            assert blockNum is not None, Print("ERROR: could not retrieve block num from transId: %s, trans: %s" % (transId, json.dumps(trans, indent=2)))
        else:
            block = transToBlock[transId]
            blockNum = block["block_num"]
            assert blockNum is not None, Print("ERROR: could not retrieve block num for block retrieved for transId: %s, block: %s" % (transId, json.dumps(block, indent=2)))

        if lastBlockNum is not None:
            if blockNum > lastBlockNum + transBlocksBehind or blockNum + transBlocksBehind < lastBlockNum:
                transBlockOutOfOrder.append({
                    "newer_trans_id" : transId,
                    "newer_trans_index" : transOrder,
                    "newer_bnum" : blockNum,
                    "last_trans_id" : lastTransId,
                    "last_trans_index" : transOrder - 1,
                    "last_bnum" : lastBlockNum
                })
                if newestBlockNum > lastBlockNum:
                    last = transBlockOutOfOrder[-1]
                    last["older_trans_id"] = newestBlockNumTransId
                    last["older_trans_index"] = newestBlockNumTransOrder
                    last["older_bnum"] = newestBlockNum

        if newestBlockNum is None:
            newestBlockNum = blockNum
            newestBlockNumTransId = transId
            newestBlockNumTransOrder = transOrder
        elif blockNum > newestBlockNum:
            newestBlockNum = blockNum
            newestBlockNumTransId = transId
            newestBlockNumTransOrder = transOrder

        lastTransId = transId
        transOrder += 1
        lastBlockNum = blockNum

    nextTime = time.perf_counter()
    Print("Validating transfers took %s sec" % (nextTime - startTranferValidationTime))

    missingReportError = False
    if len(missingTransactions) > 0:
        verboseOutput = "Missing transaction information: [" if Utils.Debug else "Missing transaction ids: ["
        verboseOutput = ", ".join([missingTrans if Utils.Debug else missingTrans["newer_trans_id"] for missingTrans in missingTransactions])
        verboseOutput += "]"
        Utils.Print("ERROR: There are %d missing transactions.  %s" % (len(missingTransactions), verboseOutput))
        missingReportError = True

    delayedReportError = True # expect to find delayed transactions since they were retried
    if len(transBlockOutOfOrder) > 0:
        verboseOutput = "Delayed transaction information: [" if Utils.Debug else "Delayed transaction ids: ["
        verboseOutput = ", ".join([json.dumps(trans, indent=2) if Utils.Debug else trans["newer_trans_id"] for trans in transBlockOutOfOrder])
        verboseOutput += "]"
        Utils.Print("There are %d transactions delayed more than %d seconds.  %s" % (len(transBlockOutOfOrder), args.transaction_time_delta, verboseOutput))
        delayedReportError = False

    testSuccessful = not missingReportError and not delayedReportError
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

errorCode = 0 if testSuccessful else 1
exit(errorCode)
