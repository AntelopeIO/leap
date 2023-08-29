#!/usr/bin/env python3

import time

from TestHarness import Cluster, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys
from TestHarness.accounts import NamedAccounts

###############################################################
# nodeos_under_min_avail_ram
#
# Sets up 4 producing nodes using --chain-state-db-guard-size-mb and --chain-state-db-size-mb to verify that nodeos will
# shutdown safely when --chain-state-db-guard-size-mb is reached and restarts the shutdown nodes, with a higher
# --chain-state-db-size-mb size, to verify that the node can restart and continue till the guard is reached again. The
# test both verifies all nodes going down and 1 node at a time.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--wallet-port","--unshared"})
Utils.Debug=args.v
pNodes=4
totalNodes=5
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails=args.dump_error_details
walletPort=args.wallet_port

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
ClientName="cleos"

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    Print("Stand up cluster")
    minRAMFlag="--chain-state-db-guard-size-mb"
    minRAMValue=1002
    maxRAMFlag="--chain-state-db-size-mb"
    maxRAMValue=1010
    extraNodeosArgs=" %s %d %s %d  --http-max-response-time-ms 990000 " % (minRAMFlag, minRAMValue, maxRAMFlag, maxRAMValue)
    if cluster.launch(onlyBios=False, pnodes=pNodes, totalNodes=totalNodes, totalProducers=totalNodes, extraNodeosArgs=extraNodeosArgs) is False:
        Utils.cmdError("launcher")
        errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    Print("creating accounts")
    namedAccounts=NamedAccounts(cluster,10)
    accounts=namedAccounts.accounts

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, [cluster.eosioAccount])

    for _, account in cluster.defProducerAccounts.items():
        walletMgr.importKey(account, testWallet, ignoreDupKeyWarning=True)

    Print("Wallet \"%s\" password=%s." % (testWalletName, testWallet.password.encode("utf-8")))

    nodes=[]
    nodes.append(cluster.getNode(0))
    nodes.append(cluster.getNode(1))
    nodes.append(cluster.getNode(2))
    nodes.append(cluster.getNode(3))
    numNodes=len(nodes)
    nonProdNode = cluster.getNode(4)

    for account in accounts:
        walletMgr.importKey(account, testWallet)

    # create accounts via eosio as otherwise a bid is needed
    for account in accounts:
        Print("Create new account %s via %s" % (account.name, cluster.eosioAccount.name))
        trans=nonProdNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=500000, waitForTransBlock=True, stakeNet=50000, stakeCPU=50000, buyRAM=50000, exitOnError=True)
        transferAmount="70000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        nonProdNode.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=True)
        trans=nonProdNode.delegatebw(account, 1000000.0000, 68000000.0000, waitForTransBlock=True, exitOnError=True)

    contractAccount=createAccountKeys(1)[0]
    contractAccount.name="contracttest"
    walletMgr.importKey(contractAccount, testWallet)
    Print("Create new account %s via %s" % (contractAccount.name, cluster.eosioAccount.name))
    trans=nonProdNode.createInitializeAccount(contractAccount, cluster.eosioAccount, stakedDeposit=500000, waitForTransBlock=True, stakeNet=50000, stakeCPU=50000, buyRAM=50000, exitOnError=True)
    transferAmount="90000000.0000 {0}".format(CORE_SYMBOL)
    Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, contractAccount.name))
    nonProdNode.transferFunds(cluster.eosioAccount, contractAccount, transferAmount, "test transfer", waitForTransBlock=True)
    trans=nonProdNode.delegatebw(contractAccount, 1000000.0000, 88000000.0000, waitForTransBlock=True, exitOnError=True)

    contractDir="unittests/test-contracts/integration_test"
    wasmFile="integration_test.wasm"
    abiFile="integration_test.abi"
    Print("Publish contract")
    trans=nodes[0].publishContract(contractAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True)
    if trans is None:
        Utils.cmdError("%s set contract %s" % (ClientName, contractAccount.name))
        errorExit("Failed to publish contract.")

    contract=contractAccount.name
    Print("push create action to %s contract" % (contract))
    action="store"
    numAmount=5000
    keepProcessing=True
    count=0
    while keepProcessing:
        numAmount+=1
        timeOutCount=0
        for fromIndex in range(namedAccounts.numAccounts):
            count+=1
            toIndex=fromIndex+1
            if toIndex==namedAccounts.numAccounts:
                toIndex=0
            fromAccount=accounts[fromIndex]
            toAccount=accounts[toIndex]
            data="{\"from\":\"%s\",\"to\":\"%s\",\"num\":%d}" % (fromAccount.name, toAccount.name, numAmount)
            opts="--permission %s@active --permission %s@active --expiration 90" % (contract, fromAccount.name)
            try:
                trans=nodes[count % numNodes].pushMessage(contract, action, data, opts)
                if trans is None or not trans[0]:
                    timeOutCount+=1
                    if timeOutCount>=3:
                        Print("Failed to push create action to eosio contract for %d consecutive times, looks like nodeos already exited." % (timeOutCount))
                        keepProcessing=False
                        break

                    Print("Failed to push create action to eosio contract. sleep for 5 seconds")
                    count-=1 # failed attempt shouldn't be counted
                    time.sleep(5)
                else:
                    timeOutCount=0
                time.sleep(1)
            except TypeError as ex:
                keepProcessing=False
                break

    #spread the actions to all accounts, to use each accounts tps bandwidth
    fromIndexStart=fromIndex+1 if fromIndex+1<namedAccounts.numAccounts else 0

    # min and max are subjective, just assigned to make sure that many small changes in nodeos don't 
    # result in the test not correctly validating behavior
    if count < 12 or count > 24:
        strMsg="little" if count < 25 else "much"
        Utils.cmdError("Was able to send %d store actions which was too %s" % (count, strMsg))
        errorExit("Incorrect number of store actions sent")

    # Make sure all the nodes are shutdown (may take a little while for this to happen, so making multiple passes)
    count=0
    while True:
        allDone=True
        for node in nodes:
            if node.verifyAlive():
                allDone=False
        if allDone:
            break
        count+=1
        if count>12:
            Utils.cmdError("All Nodes should have died")
            errorExit("Failure - All Nodes should have died")
        time.sleep(5)

    for i in range(numNodes):
        f = open(Utils.getNodeDataDir(i) + "/stderr.txt")
        contents = f.read()
        if contents.find("database chain::guard_exception") == -1:
            errorExit("Node%d is expected to exit because of database guard_exception, but was not." % (i))

    Print("all nodes exited with expected reason database_guard_exception")

    Print("relaunch nodes with new capacity")
    addSwapFlags={}
    maxRAMValue+=2
    currentMinimumMaxRAM=maxRAMValue
    enabledStaleProduction=False
    for i in range(numNodes):
        addSwapFlags[maxRAMFlag]=str(maxRAMValue)
        #addSwapFlags["--max-irreversible-block-age"]=str(-1)
        nodeIndex=numNodes-i-1
        if not enabledStaleProduction:
            addSwapFlags["--enable-stale-production"]=""   # just enable stale production for the first node
            enabledStaleProduction=True
        if not nodes[nodeIndex].relaunch("", newChain=False, addSwapFlags=addSwapFlags):
            Utils.cmdError(f'Failed to restart {nodes[nodeIndex].name} with new capacity {maxRAMValue}')
            errorExit("Failure - Node should have restarted")
        addSwapFlags={}
        maxRAMValue=currentMinimumMaxRAM+30

    time.sleep(20)
    for i in range(numNodes):
        if not nodes[i].verifyAlive():
            Utils.cmdError("Node %d should be alive" % (i))
            errorExit("Failure - All Nodes should be alive")

    # get all the nodes to get info, so reported status (on error) reflects their current state
    Print("push more actions to %s contract" % (contract))
    cluster.getInfos()
    action="store"
    keepProcessing=True
    count=0
    while keepProcessing and count < 40:
        Print("Send %s" % (action))
        numAmount+=1
        for fromIndexOffset in range(namedAccounts.numAccounts):
            count+=1
            fromIndex=fromIndexStart+fromIndexOffset
            if fromIndex>=namedAccounts.numAccounts:
                fromIndex-=namedAccounts.numAccounts 
            toIndex=fromIndex+1
            if toIndex==namedAccounts.numAccounts:
                toIndex=0
            fromAccount=accounts[fromIndex]
            toAccount=accounts[toIndex]
            data="{\"from\":\"%s\",\"to\":\"%s\",\"num\":%d}" % (fromAccount.name, toAccount.name, numAmount)
            opts="--permission %s@active --permission %s@active --expiration 90" % (contract, fromAccount.name)
            try:
                trans=nodes[count % numNodes].pushMessage(contract, action, data, opts)
                if trans is None or not trans[0]:
                    Print("Failed to push create action to eosio contract. sleep for 60 seconds")
                    time.sleep(60)
                time.sleep(1)
            except TypeError as ex:
                Print("Failed to send %s" % (action))

            if not nodes[len(nodes)-1].verifyAlive():
                keepProcessing=False
                break

    if keepProcessing:
        Utils.cmdError("node[%d] never shutdown" % (numNodes-1))
        errorExit("Failure - Node should be shutdown")

    for i in range(numNodes):
        # only the last node should be dead
        if not nodes[i].verifyAlive() and i<numNodes-1:
            Utils.cmdError("Node %d should be alive" % (i))
            errorExit("Failure - Node should be alive")

    Print("relaunch node with even more capacity")
    addSwapFlags={}

    time.sleep(10)
    maxRAMValue=currentMinimumMaxRAM+5
    currentMinimumMaxRAM=maxRAMValue
    addSwapFlags[maxRAMFlag]=str(maxRAMValue)
    if not nodes[len(nodes)-1].relaunch("", newChain=False, addSwapFlags=addSwapFlags):
        Utils.cmdError("Failed to restart node %d with new capacity %s" % (numNodes-1, maxRAMValue))
        errorExit("Failure - Node should have restarted")
    addSwapFlags={}

    time.sleep(10)
    for node in nodes:
        if not node.verifyAlive():
            Utils.cmdError("All Nodes should be alive")
            errorExit("Failure - All Nodes should be alive")

    time.sleep(20)
    Print("Send 1 more action to every node")
    numAmount+=1
    for fromIndexOffset in range(namedAccounts.numAccounts):
        # just sending one node to each
        if fromIndexOffset>=len(nodes):
           break
        fromIndex=fromIndexStart+fromIndexOffset
        if fromIndex>=namedAccounts.numAccounts:
            fromIndex-=namedAccounts.numAccounts 
        toIndex=fromIndex+1
        if toIndex==namedAccounts.numAccounts:
            toIndex=0
        fromAccount=accounts[fromIndex]
        toAccount=accounts[toIndex]
        node=nodes[fromIndexOffset]
        data="{\"from\":\"%s\",\"to\":\"%s\",\"num\":%d}" % (fromAccount.name, toAccount.name, numAmount)
        opts="--permission %s@active --permission %s@active --expiration 90" % (contract, fromAccount.name)
        try:
            trans=node.pushMessage(contract, action, data, opts)
            if trans is None or not trans[0]:
                Print("Failed to push create action to eosio contract. sleep for 60 seconds")
                time.sleep(60)
                continue
            time.sleep(1)
        except TypeError as ex:
            Utils.cmdError("Failed to send %s action to node %d" % (fromAccount, fromIndexOffset, action))
            errorExit("Failure - send %s action should have succeeded" % (action))

    time.sleep(10)
    Print("Check nodes are alive")
    for node in nodes:
        if not node.verifyAlive():
            Utils.cmdError("All Nodes should be alive")
            errorExit("Failure - All Nodes should be alive")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
