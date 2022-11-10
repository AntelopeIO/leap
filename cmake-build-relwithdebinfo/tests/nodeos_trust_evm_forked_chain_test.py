#!/usr/bin/env python3

from datetime import datetime
from datetime import timedelta
import time
import json
import signal
import rlp
import re
from ethereum import transactions
from binascii import unhexlify

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
from threading import Thread

###############################################################
# nodeos_trust_evm_forked_chain_test
# 
# This test sets up 2 producing nodes and one "bridge" node using test_control_api_plugin.
#   One producing node has 11 of the elected producers and the other has 10 of the elected producers.
#   All the producers are named in alphabetical order, so that the 11 producers, in the one production node, are
#       scheduled first, followed by the 10 producers in the other producer node. Each producing node is only connected
#       to the other producing node via the "bridge" node.
#   The bridge node has the test_control_api_plugin, which exposes a restful interface that the test script uses to kill
#       the "bridge" node at a specific producer in the production cycle. This is used to fork the producer network
#       precisely when the 11 producer node has finished producing and the other producing node is about to produce.
#   The fork in the producer network results in one fork of the block chain that advances with 10 producers with a LIB
#      that has advanced, since all of the previous blocks were confirmed and the producer that was scheduled for that
#      slot produced it, and one with 11 producers with a LIB that has not advanced.  This situation is validated by
#      the test script.
#   After both chains are allowed to produce, the "bridge" node is turned back on.
#   Time is allowed to progress so that the "bridge" node can catchup and both producer nodes to come to consensus
#   The block log is then checked for both producer nodes to verify that the 10 producer fork is selected and that
#       both nodes are in agreement on the block log.
#
###############################################################

Print=Utils.Print

from core_symbol import CORE_SYMBOL

def analyzeBPs(bps0, bps1, expectDivergence):
    start=0
    index=None
    length=len(bps0)
    firstDivergence=None
    errorInDivergence=False
    analysysPass=0
    bpsStr=None
    bpsStr0=None
    bpsStr1=None
    while start < length:
        analysysPass+=1
        bpsStr=None
        for i in range(start,length):
            bp0=bps0[i]
            bp1=bps1[i]
            if bpsStr is None:
                bpsStr=""
            else:
                bpsStr+=", "
            blockNum0=bp0["blockNum"]
            prod0=bp0["prod"]
            blockNum1=bp1["blockNum"]
            prod1=bp1["prod"]
            numDiff=True if blockNum0!=blockNum1 else False
            prodDiff=True if prod0!=prod1 else False
            if numDiff or prodDiff:
                index=i
                if firstDivergence is None:
                    firstDivergence=min(blockNum0, blockNum1)
                if not expectDivergence:
                    errorInDivergence=True
                break
            bpsStr+=str(blockNum0)+"->"+prod0

        if index is None:
            if expectDivergence:
                errorInDivergence=True
                break
            return None

        bpsStr0=None
        bpsStr2=None
        start=length
        for i in range(index,length):
            if bpsStr0 is None:
                bpsStr0=""
                bpsStr1=""
            else:
                bpsStr0+=", "
                bpsStr1+=", "
            bp0=bps0[i]
            bp1=bps1[i]
            blockNum0=bp0["blockNum"]
            prod0=bp0["prod"]
            blockNum1=bp1["blockNum"]
            prod1=bp1["prod"]
            numDiff="*" if blockNum0!=blockNum1 else ""
            prodDiff="*" if prod0!=prod1 else ""
            if not numDiff and not prodDiff:
                start=i
                index=None
                if expectDivergence:
                    errorInDivergence=True
                break
            bpsStr0+=str(blockNum0)+numDiff+"->"+prod0+prodDiff
            bpsStr1+=str(blockNum1)+numDiff+"->"+prod1+prodDiff
        if errorInDivergence:
            break

    if errorInDivergence:
        msg="Failed analyzing block producers - "
        if expectDivergence:
            msg+="nodes do not indicate different block producers for the same blocks, but they are expected to diverge at some point."
        else:
            msg+="did not expect nodes to indicate different block producers for the same blocks."
        msg+="\n  Matching Blocks= %s \n  Diverging branch node0= %s \n  Diverging branch node1= %s" % (bpsStr,bpsStr0,bpsStr1)
        Utils.errorExit(msg)

    return firstDivergence

def getMinHeadAndLib(prodNodes):
    info0=prodNodes[0].getInfo(exitOnError=True)
    info1=prodNodes[1].getInfo(exitOnError=True)
    headBlockNum=min(int(info0["head_block_num"]),int(info1["head_block_num"]))
    libNum=min(int(info0["last_irreversible_block_num"]), int(info1["last_irreversible_block_num"]))
    return (headBlockNum, libNum)

def generate_evm_transactions(nonce, prodNode):
    while True:
        Utils.Print("Execute ETH contract")
        nonce += 1
        toAdd = "2787b98fc4e731d0456b3941f0b3fe2e01430000"
        amount = 0
        unsignedTrx = transactions.Transaction(
            nonce,
            150000000000, #150 GWei
            100000,       #100k Gas
            toAdd,
            amount,
            unhexlify("6057361d000000000000000000000000000000000000000000000000000000000000007b")
        )
        rlptx = rlp.encode(unsignedTrx.sign(evmSendKey, evmChainId), transactions.Transaction)
        actData = {"ram_payer":"evmevmevmevm", "rlptx":rlptx.hex()}
        evmRetValue = prodNode.pushMessage(evmAcc.name, "pushtx", json.dumps(actData), '-p evmevmevmevm', expiration=300)
        if not evmRetValue[0]:
            try:
                Utils.Print("*** Invalid nonce ***")
                Utils.Print(f"evmRetValue: {evmRetValue}")
                found = re.search('"console": "nonce:([0-9]+)', str(evmRetValue[1])).group(1)
                Utils.Print(f"*** Invalid nonce of {nonce} should be {found} ***")
                nonce = int(found)
                nonce -= 1 # for the +1 at top of loop
                continue
            except AttributeError as e:
                Utils.Print(f"Exception {e}")
                pass
        assert evmRetValue[0], "pushtx to ETH contract failed."
        Utils.Print("\tReturn value:", evmRetValue[1]["processed"]["action_traces"][0]["return_value_data"])
        Utils.Print("\tBlock#", evmRetValue[1]["processed"]["block_num"])
        row0=prodNode.getTableRow(evmAcc.name, 3, "storage", 0)
        Utils.Print("\tTable row:", row0)
        time.sleep(1)


appArgs=AppArgs()
extraArgs = appArgs.add(flag="--trust-evm-contract-root", type=str, help="TrustEVM contract build dir", default=None)

args = TestHelper.parse_args({"--prod-count","--dump-error-details","--keep-logs","-v","--leave-running","--clean-run",
                              "--wallet-port"}, applicationSpecificArgs=appArgs)
Utils.Debug=args.v
totalProducerNodes=2
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes
maxActiveProducers=21
totalProducers=maxActiveProducers
cluster=Cluster(walletd=True)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
prodCount=args.prod_count
killAll=args.clean_run
walletPort=args.wallet_port
trustEvmContractRoot=args.trust_evm_contract_root

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
killEosInstances=not dontKill
killWallet=not dontKill

WalletdName=Utils.EosWalletName
ClientName="cleos"

assert trustEvmContractRoot is not None, "--trust-evm-contract-root is required"

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    Print("Stand up cluster")
    specificExtraNodeosArgs={}
    # producer nodes will be mapped to 0 through totalProducerNodes-1, so the number totalProducerNodes will be the non-producing node
    specificExtraNodeosArgs[totalProducerNodes]="--plugin eosio::test_control_api_plugin --plugin eosio::state_history_plugin --state-history-endpoint 127.0.0.1:8999 --trace-history --chain-state-history --disable-replay-opts  "
    extraNodeosArgs="--contracts-console"


    # ***   setup topogrophy   ***

    # "bridge" shape connects defprocera through defproducerk (in node0) to each other and defproducerl through defproduceru (in node01)
    # and the only connection between those 2 groups is through the bridge node

    if cluster.launch(prodCount=prodCount, topo="bridge", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducers,
                      useBiosBootFile=False, extraNodeosArgs=extraNodeosArgs, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")
    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)


    # ***   create accounts to vote in desired producers   ***

    accounts=cluster.createAccountKeys(6)
    if accounts is None:
        Utils.errorExit("FAILURE - create keys")
    accounts[0].name="tester111111"
    accounts[1].name="tester222222"
    accounts[2].name="tester333333"
    accounts[3].name="tester444444"
    accounts[4].name="tester555555"
    accounts[5].name="evmevmevmevm"

    evmAcc = accounts[5]

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, [cluster.eosioAccount,accounts[0],accounts[1],accounts[2],accounts[3],accounts[4],accounts[5]])

    for _, account in cluster.defProducerAccounts.items():
        walletMgr.importKey(account, testWallet, ignoreDupKeyWarning=True)

    Print("Wallet \"%s\" password=%s." % (testWalletName, testWallet.password.encode("utf-8")))


    # ***   identify each node (producers and non-producing node)   ***

    nonProdNode=None
    prodNodes=[]
    producers=[]
    for i in range(0, totalNodes):
        node=cluster.getNode(i)
        node.producers=Cluster.parseProducers(i)
        numProducers=len(node.producers)
        Print("node has producers=%s" % (node.producers))
        if numProducers==0:
            if nonProdNode is None:
                nonProdNode=node
                nonProdNode.nodeNum=i
            else:
                Utils.errorExit("More than one non-producing nodes")
        else:
            for prod in node.producers:
                trans=node.regproducer(cluster.defProducerAccounts[prod], "http::/mysite.com", 0, waitForTransBlock=False, exitOnError=True)

            prodNodes.append(node)
            producers.extend(node.producers)


    # ***   delegate bandwidth to accounts   ***

    node=prodNodes[0]
    prodNode = node
    # create accounts via eosio as otherwise a bid is needed
    for account in accounts:
        Print("Create new account %s via %s" % (account.name, cluster.eosioAccount.name))
        trans=node.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=True, stakeNet=10000, stakeCPU=10000, buyRAM=10000000, exitOnError=True)
        transferAmount="100000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        node.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=True)
        trans=node.delegatebw(account, 20000000.0000, 20000000.0000, waitForTransBlock=True, exitOnError=True)


    # ***   vote using accounts   ***

    #verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)
    index=0
    for account in accounts:
        Print("Vote for producers=%s" % (producers))
        trans=prodNodes[index % len(prodNodes)].vote(account, producers, waitForTransBlock=True)
        index+=1

    # ***   Setup EVM contract ***

    contractDir=trustEvmContractRoot + "/evm_runtime"
    wasmFile="evm_runtime.wasm"
    abiFile="evm_runtime.abi"
    Utils.Print("Publish evm_runtime contract")
    evmTrans = prodNode.publishContract(evmAcc, contractDir, wasmFile, abiFile, waitForTransBlock=True)
    transId=prodNode.getTransId(evmTrans)
    blockNum = prodNode.getBlockNumByTransId(transId)
    block = prodNode.getBlock(blockNum)
    Utils.Print("Block Id: ", block["id"])
    Utils.Print("Block timestamp: ", block["timestamp"])

    Utils.Print("Set balance")
    evmTrans = prodNode.pushMessage(evmAcc.name, "setbal", '{"addy":"2787b98fc4e731d0456b3941f0b3fe2e01439961", "bal":"0000000000000000000000000000000100000000000000000000000000000000"}', '-p evmevmevmevm')
    prodNode.waitForTransBlockIfNeeded(evmTrans[1], True)

    Utils.Print("Send balance")
    evmChainId = 15555
    fromAdd = "2787b98fc4e731d0456b3941f0b3fe2e01439961"
    toAdd = '0x9edf022004846bc987799d552d1b8485b317b7ed'
    amount = 100
    nonce = 0
    evmSendKey = "a3f1b69da92a0233ce29485d3049a4ace39e8d384bbc2557e3fc60940ce4e954"
    unsignedTrx = transactions.Transaction(
        nonce,
        150000000000, #150 GWei
        100000,       #100k Gas
        toAdd,
        amount,
        b''
    )
    rlptx = rlp.encode(unsignedTrx.sign(evmSendKey, evmChainId), transactions.Transaction)
    actData = {"ram_payer":"evmevmevmevm", "rlptx":rlptx.hex()}
    evmTrans = prodNode.pushMessage(evmAcc.name, "pushtx", json.dumps(actData), '-p evmevmevmevm')
    prodNode.waitForTransBlockIfNeeded(evmTrans[1], True)

    Utils.Print("Send balance again, should fail with wrong nonce")
    retValue = prodNode.pushMessage(evmAcc.name, "pushtx", json.dumps(actData), '-p evmevmevmevm', silentErrors=True, force=True)
    assert not retValue[0], f"push trx should have failed: {retValue}"

    Utils.Print("Simple Solidity contract")
    # // SPDX-License-Identifier: GPL-3.0
    # pragma solidity >=0.7.0 <0.9.0;
    # contract Storage {
    #     uint256 number;
    #     function store(uint256 num) public {
    #         number = num;
    #     }
    #     function retrieve() public view returns (uint256){
    #         return number;
    #     }
    # }
    retValue = prodNode.pushMessage(evmAcc.name, "updatecode", '{"address":"2787b98fc4e731d0456b3941f0b3fe2e01430000","incarnation":0,"code_hash":"286e3d498e2178afc91275f11d446e62a0d85b060ce66d6ca75f6ec9d874d560","code":"608060405234801561001057600080fd5b50600436106100365760003560e01c80632e64cec11461003b5780636057361d14610059575b600080fd5b610043610075565b60405161005091906100d9565b60405180910390f35b610073600480360381019061006e919061009d565b61007e565b005b60008054905090565b8060008190555050565b60008135905061009781610103565b92915050565b6000602082840312156100b3576100b26100fe565b5b60006100c184828501610088565b91505092915050565b6100d3816100f4565b82525050565b60006020820190506100ee60008301846100ca565b92915050565b6000819050919050565b600080fd5b61010c816100f4565b811461011757600080fd5b5056fea26469706673582212209a159a4f3847890f10bfb87871a61eba91c5dbf5ee3cf6398207e292eee22a1664736f6c63430008070033"}', '-p evmevmevmevm')

    Utils.Print("Pausing for setup of trustevm node")
    input("Press Enter to continue...")

    # start generating evm transactions
    thread = Thread(target=generate_evm_transactions, args=(nonce, prodNode))
    thread.start()

    Print("Thread spawned")

    # ***   Identify a block where production is stable   ***

    #verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=5)
    blockNum=node.getNextCleanProductionCycle(trans)
    blockProducer=node.getBlockProducerByNum(blockNum)
    Print("Validating blockNum=%s, producer=%s" % (blockNum, blockProducer))
    cluster.biosNode.kill(signal.SIGTERM)

    class HeadWaiter:
        def __init__(self, node):
            self.node=node
            self.cachedHeadBlockNum=node.getBlockNum()

        def waitIfNeeded(self, blockNum):
            delta=self.cachedHeadBlockNum-blockNum
            if delta >= 0:
                return
            previousHeadBlockNum=self.cachedHeadBlockNum
            delta=-1*delta
            timeout=(delta+1)/2 + 3 # round up to nearest second and 3 second extra leeway
            self.node.waitForBlock(blockNum, timeout=timeout)
            self.cachedHeadBlockNum=node.getBlockNum()
            if blockNum > self.cachedHeadBlockNum:
                Utils.errorExit("Failed to advance from block number %d to %d in %d seconds.  Only got to block number %d" % (previousHeadBlockNum, blockNum, timeout, self.cachedHeadBlockNum))

        def getBlock(self, blockNum):
            self.waitIfNeeded(blockNum)
            return self.node.getBlock(blockNum)

    #advance to the next block of 12
    lastBlockProducer=blockProducer

    waiter=HeadWaiter(node)

    while blockProducer==lastBlockProducer:
        blockNum+=1
        block=waiter.getBlock(blockNum)
        Utils.Print("Block num: %d, block: %s" % (blockNum, json.dumps(block, indent=4, sort_keys=True)))
        blockProducer=Node.getBlockAttribute(block, "producer", blockNum)

    timestampStr=Node.getBlockAttribute(block, "timestamp", blockNum)
    timestamp=datetime.strptime(timestampStr, Utils.TimeFmt)


    # ***   Identify what the production cycle is   ***

    productionCycle=[]
    producerToSlot={}
    slot=-1
    inRowCountPerProducer=12
    minNumBlocksPerProducer=10
    lastTimestamp=timestamp
    headBlockNum=node.getBlockNum()
    firstBlockForWindowMissedSlot=None
    while True:
        if blockProducer not in producers:
            Utils.errorExit("Producer %s was not one of the voted on producers" % blockProducer)

        productionCycle.append(blockProducer)
        slot+=1
        if blockProducer in producerToSlot:
            Utils.errorExit("Producer %s was first seen in slot %d, but is repeated in slot %d" % (blockProducer, producerToSlot[blockProducer], slot))

        producerToSlot[blockProducer]={"slot":slot, "count":0}
        lastBlockProducer=blockProducer
        blockSkip=[]
        roundSkips=0
        missedSlotAfter=[]
        if firstBlockForWindowMissedSlot is not None:
            missedSlotAfter.append(firstBlockForWindowMissedSlot)
            firstBlockForWindowMissedSlot=None

        while blockProducer==lastBlockProducer:
            producerToSlot[blockProducer]["count"]+=1
            blockNum+=1
            block=waiter.getBlock(blockNum)
            blockProducer=Node.getBlockAttribute(block, "producer", blockNum)
            timestampStr=Node.getBlockAttribute(block, "timestamp", blockNum)
            timestamp=datetime.strptime(timestampStr, Utils.TimeFmt)
            timediff=timestamp-lastTimestamp
            slotTime=0.5
            slotsDiff=int(timediff.total_seconds() / slotTime)
            if slotsDiff != 1:
                slotTimeDelta=timedelta(slotTime)
                first=lastTimestamp + slotTimeDelta
                missed=first.strftime(Utils.TimeFmt)
                if slotsDiff > 2:
                    last=timestamp - slotTimeDelta
                    missed+= " thru " + last.strftime(Utils.TimeFmt)
                missedSlotAfter.append("%d (%s)" % (blockNum-1, missed))
            lastTimestamp=timestamp

        if producerToSlot[lastBlockProducer]["count"] < minNumBlocksPerProducer or producerToSlot[lastBlockProducer]["count"] > inRowCountPerProducer:
            Utils.errorExit("Producer %s, in slot %d, expected to produce %d blocks but produced %d blocks.  At block number %d. " %
                            (lastBlockProducer, slot, inRowCountPerProducer, producerToSlot[lastBlockProducer]["count"], blockNum-1) +
                            "Slots were missed after the following blocks: %s" % (", ".join(missedSlotAfter)))

        if len(missedSlotAfter) > 0:
            # it may be the most recent producer missed a slot
            possibleMissed=missedSlotAfter[-1]
            if possibleMissed == blockNum - 1:
                firstBlockForWindowMissedSlot=possibleMissed

        if blockProducer==productionCycle[0]:
            break

    output=None
    for blockProducer in productionCycle:
        if output is None:
            output=""
        else:
            output+=", "
        output+=blockProducer+":"+str(producerToSlot[blockProducer]["count"])
    Print("ProductionCycle ->> {\n%s\n}" % output)

    #retrieve the info for all the nodes to report the status for each
    for node in cluster.getNodes():
        node.getInfo()
    cluster.reportStatus()


    # ***   Killing the "bridge" node   ***

    Print("Sending command to kill \"bridge\" node to separate the 2 producer groups.")
    # block number to start expecting node killed after
    preKillBlockNum=nonProdNode.getBlockNum()
    preKillBlockProducer=nonProdNode.getBlockProducerByNum(preKillBlockNum)
    Print("preKillBlockProducer = {}".format(preKillBlockProducer))
    # kill at last block before defproducerl, since the block it is killed on will get propagated
    killAtProducer="defproducerk"
    nonProdNode.killNodeOnProducer(producer=killAtProducer, whereInSequence=(inRowCountPerProducer-1))


    # ***   Identify a highest block number to check while we are trying to identify where the divergence will occur   ***

    # will search full cycle after the current block, since we don't know how many blocks were produced since retrieving
    # block number and issuing kill command
    postKillBlockNum=prodNodes[1].getBlockNum()
    blockProducers0=[]
    blockProducers1=[]
    libs0=[]
    libs1=[]
    lastBlockNum=max([preKillBlockNum,postKillBlockNum])+2*maxActiveProducers*inRowCountPerProducer
    actualLastBlockNum=None
    prodChanged=False
    nextProdChange=False
    #identify the earliest LIB to start identify the earliest block to check if divergent branches eventually reach concensus
    (headBlockNum, libNumAroundDivergence)=getMinHeadAndLib(prodNodes)
    Print("Tracking block producers from %d till divergence or %d. Head block is %d and lowest LIB is %d" % (preKillBlockNum, lastBlockNum, headBlockNum, libNumAroundDivergence))
    transitionCount=0
    missedTransitionBlock=None
    for blockNum in range(preKillBlockNum,lastBlockNum + 1):
        #avoiding getting LIB until my current block passes the head from the last time I checked
        if blockNum>headBlockNum:
            (headBlockNum, libNumAroundDivergence)=getMinHeadAndLib(prodNodes)

        # track the block number and producer from each producing node
        # we use timeout 70 here because of case when chain break, call to getBlockProducerByNum
        # and call of producer_plugin::schedule_delayed_production_loop happens nearly immediately
        # for 10 producers wait cycle is 10 * (12*0.5) = 60 seconds.
        # for 11 producers wait cycle is 11 * (12*0.5) = 66 seconds.
        blockProducer0=prodNodes[0].getBlockProducerByNum(blockNum, timeout=70)
        blockProducer1=prodNodes[1].getBlockProducerByNum(blockNum, timeout=70)
        Print("blockNum = {} blockProducer0 = {} blockProducer1 = {}".format(blockNum, blockProducer0, blockProducer1))
        blockProducers0.append({"blockNum":blockNum, "prod":blockProducer0})
        blockProducers1.append({"blockNum":blockNum, "prod":blockProducer1})

        #in the case that the preKillBlockNum was also produced by killAtProducer, ensure that we have
        #at least one producer transition before checking for killAtProducer
        if not prodChanged:
            if preKillBlockProducer!=blockProducer0:
                prodChanged=True
                Print("prodChanged = True")

        #since it is killing for the last block of killAtProducer, we look for the next producer change
        if not nextProdChange and prodChanged and blockProducer1==killAtProducer:
            nextProdChange=True
            Print("nextProdChange = True")
        elif nextProdChange and blockProducer1!=killAtProducer:
            Print("nextProdChange = False")
            if blockProducer0!=blockProducer1:
                Print("Divergence identified at block %s, node_00 producer: %s, node_01 producer: %s" % (blockNum, blockProducer0, blockProducer1))
                actualLastBlockNum=blockNum
                break
            else:
                missedTransitionBlock=blockNum
                transitionCount+=1
                Print("missedTransitionBlock = {} transitionCount = {}".format(missedTransitionBlock, transitionCount))
                # allow this to transition twice, in case the script was identifying an earlier transition than the bridge node received the kill command
                if transitionCount>1:
                    Print("At block %d and have passed producer: %s %d times and we have not diverged, stopping looking and letting errors report" % (blockNum, killAtProducer, transitionCount))
                    actualLastBlockNum=blockNum
                    break

        #if we diverge before identifying the actualLastBlockNum, then there is an ERROR
        if blockProducer0!=blockProducer1:
            extra="" if transitionCount==0 else " Diverged after expected killAtProducer transition at block %d." % (missedTransitionBlock)
            Utils.errorExit("Groups reported different block producers for block number %d.%s %s != %s." % (blockNum,extra,blockProducer0,blockProducer1))

    #verify that the non producing node is not alive (and populate the producer nodes with current getInfo data to report if
    #an error occurs)
    if nonProdNode.verifyAlive():
        Utils.errorExit("Expected the non-producing node to have shutdown.")

    Print("Analyzing the producers leading up to the block after killing the non-producing node, expecting divergence at %d" % (blockNum))

    firstDivergence=analyzeBPs(blockProducers0, blockProducers1, expectDivergence=True)
    # Nodes should not have diverged till the last block
    if firstDivergence!=blockNum:
        Utils.errorExit("Expected to diverge at %s, but diverged at %s." % (firstDivergence, blockNum))
    blockProducers0=[]
    blockProducers1=[]

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print("node info: %s" % (info))

    killBlockNum=blockNum
    lastBlockNum=killBlockNum+(maxActiveProducers - 1)*inRowCountPerProducer+1  # allow 1st testnet group to produce just 1 more block than the 2nd

    Print("Tracking the blocks from the divergence till there are 10*12 blocks on one chain and 10*12+1 on the other, from block %d to %d" % (killBlockNum, lastBlockNum))

    for blockNum in range(killBlockNum,lastBlockNum):
        blockProducer0=prodNodes[0].getBlockProducerByNum(blockNum)
        blockProducer1=prodNodes[1].getBlockProducerByNum(blockNum)
        blockProducers0.append({"blockNum":blockNum, "prod":blockProducer0})
        blockProducers1.append({"blockNum":blockNum, "prod":blockProducer1})


    Print("Analyzing the producers from the divergence to the lastBlockNum and verify they stay diverged, expecting divergence at block %d" % (killBlockNum))

    firstDivergence=analyzeBPs(blockProducers0, blockProducers1, expectDivergence=True)
    if firstDivergence!=killBlockNum:
        Utils.errorExit("Expected to diverge at %s, but diverged at %s." % (firstDivergence, killBlockNum))
    blockProducers0=[]
    blockProducers1=[]

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print("node info: %s" % (info))

    Print("Relaunching the non-producing bridge node to connect the producing nodes again")

    if not nonProdNode.relaunch():
        Utils.errorExit("Failure - (non-production) node %d should have restarted" % (nonProdNode.nodeNum))


    Print("Waiting to allow forks to resolve")

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print("node info: %s" % (info))

    #ensure that the nodes have enough time to get in concensus, so wait for 3 producers to produce their complete round
    time.sleep(inRowCountPerProducer * 3 / 2)
    remainingChecks=60
    match=False
    checkHead=False
    checkMatchBlock=killBlockNum
    forkResolved=False
    while remainingChecks>0:
        if checkMatchBlock == killBlockNum and checkHead:
            checkMatchBlock = prodNodes[0].getBlockNum()
        blockProducer0=prodNodes[0].getBlockProducerByNum(checkMatchBlock)
        blockProducer1=prodNodes[1].getBlockProducerByNum(checkMatchBlock)
        match=blockProducer0==blockProducer1
        if match:
            if checkHead:
                forkResolved=True
                break
            else:
                checkHead=True
                continue
        Print("Fork has not resolved yet, wait a little more. Block %s has producer %s for node_00 and %s for node_01.  Original divergence was at block %s. Wait time remaining: %d" % (checkMatchBlock, blockProducer0, blockProducer1, killBlockNum, remainingChecks))
        time.sleep(1)
        remainingChecks-=1
    
    assert forkResolved, "fork was not resolved in a reasonable time. node_00 lib {} head {} node_01 lib {} head {}".format(
                                                                                  prodNodes[0].getIrreversibleBlockNum(), 
                                                                                          prodNodes[0].getHeadBlockNum(), 
                                                                                                          prodNodes[1].getIrreversibleBlockNum(), 
                                                                                                                 prodNodes[1].getHeadBlockNum()) 

    for prodNode in prodNodes:
        info=prodNode.getInfo()
        Print("node info: %s" % (info))

    # ensure all blocks from the lib before divergence till the current head are now in consensus
    endBlockNum=max(prodNodes[0].getBlockNum(), prodNodes[1].getBlockNum())

    Print("Identifying the producers from the saved LIB to the current highest head, from block %d to %d" % (libNumAroundDivergence, endBlockNum))

    for blockNum in range(libNumAroundDivergence,endBlockNum):
        blockProducer0=prodNodes[0].getBlockProducerByNum(blockNum)
        blockProducer1=prodNodes[1].getBlockProducerByNum(blockNum)
        blockProducers0.append({"blockNum":blockNum, "prod":blockProducer0})
        blockProducers1.append({"blockNum":blockNum, "prod":blockProducer1})


    Print("Analyzing the producers from the saved LIB to the current highest head and verify they match now")

    analyzeBPs(blockProducers0, blockProducers1, expectDivergence=False)

    resolvedKillBlockProducer=None
    for prod in blockProducers0:
        if prod["blockNum"]==killBlockNum:
            resolvedKillBlockProducer = prod["prod"]
    if resolvedKillBlockProducer is None:
        Utils.errorExit("Did not find find block %s (the original divergent block) in blockProducers0, test setup is wrong.  blockProducers0: %s" % (killBlockNum, ", ".join(blockProducers0)))
    Print("Fork resolved and determined producer %s for block %s" % (resolvedKillBlockProducer, killBlockNum))

    blockProducers0=[]
    blockProducers1=[]

    Utils.Print("Press enter to end test")
    input("Press Enter...")


    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)