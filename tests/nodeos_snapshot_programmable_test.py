#!/usr/bin/env python3

import os
import time
import signal
import decimal
import math
import re
import shutil

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr
from TestHarness.Node import BlockType
from TestHarness.TestHelper import AppArgs
from TestHarness.testUtils import BlockLogAction

###############################################################
# nodeos_snapshot_programmable_test
#
#  Test configures a producing node and 2 non-producing nodes.
#  Configures trx_generator(s) and starts generating transactions and sending them
#  to the producing node.
#  - Schedule a snapshot from producing node
#  - Create a snapshot from producing node
#  - Convert snapshot to JSON
#  - Trim blocklog to head block of snapshot
#  - Start nodeos in irreversible mode on blocklog
#  - Generate snapshot and convert to JSON
#  - Compare JSON snapshot to original snapshot JSON
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--clean-run","--wallet-port"},
                             applicationSpecificArgs=appArgs)

relaunchTimeout = 30
Utils.Debug=args.v
pnodes=1
testAccounts = 2
trxGeneratorCnt=2
startedNonProdNodes = 2
cluster=Cluster(walletd=True)
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
dontKill=args.leave_running
prodCount=2
killAll=args.clean_run
walletPort=args.wallet_port
totalNodes=startedNonProdNodes+pnodes

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
killEosInstances=not dontKill
killWallet=not dontKill

WalletdName=Utils.EosWalletName
ClientName="cleos"

trxGenLauncher=None

snapshotScheduleDB = "snapshot-schedule.json"

def getLatestSnapshot(nodeId):
    snapshotDir = os.path.join(Utils.getNodeDataDir(nodeId), "snapshots")
    snapshotDirContents = os.listdir(snapshotDir)
    assert len(snapshotDirContents) > 0
    # disregard snapshot schedule config in same folder
    if snapshotScheduleDB in snapshotDirContents: snapshotDirContents.remove(snapshotScheduleDB)
    snapshotDirContents.sort()
    return os.path.join(snapshotDir, snapshotDirContents[-1])

def removeState(nodeId):
    dataDir = Utils.getNodeDataDir(nodeId)
    state = os.path.join(dataDir, "state")
    shutil.rmtree(state, ignore_errors=True)

try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.setWalletMgr(walletMgr)

    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    specificExtraNodeosArgs={}
    Print("Stand up cluster")
    if cluster.launch(prodCount=prodCount, onlyBios=False, pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes*prodCount,
                      useBiosBootFile=False, specificExtraNodeosArgs=specificExtraNodeosArgs, loadSystemContract=True, maximumP2pPerHost=totalNodes+trxGeneratorCnt) is False:
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Create test wallet")
    wallet = walletMgr.create('txntestwallet')
    cluster.populateWallet(2, wallet)

    Print("Create test accounts for transactions.")
    cluster.createAccounts(cluster.eosioAccount, stakedDeposit=0, validationNodeIndex=0)

    account1Name = cluster.accounts[0].name
    account2Name = cluster.accounts[1].name

    account1PrivKey = cluster.accounts[0].activePrivateKey
    account2PrivKey = cluster.accounts[1].activePrivateKey

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts([cluster.accounts[0], cluster.accounts[1]])

    def waitForBlock(node, blockNum, blockType=BlockType.head, timeout=None, reportInterval=20):
        if not node.waitForBlock(blockNum, timeout=timeout, blockType=blockType, reportInterval=reportInterval):
            info=node.getInfo()
            headBlockNum=info["head_block_num"]
            libBlockNum=info["last_irreversible_block_num"]
            Utils.errorExit("Failed to get to %s block number %d. Last had head block number %d and lib %d" % (blockType, blockNum, headBlockNum, libBlockNum))


    snapshotNodeId = 0
    node0=cluster.getNode(snapshotNodeId)
    irrNodeId = snapshotNodeId+1

    nodeSnap=cluster.getNode(snapshotNodeId)
    nodeIrr=cluster.getNode(irrNodeId)

    Print("Wait for account creation to be irreversible")
    blockNum=node0.getBlockNum(BlockType.head)
    waitForBlock(node0, blockNum, blockType=BlockType.lib)

    Print("Configure and launch txn generators")
    targetTpsPerGenerator = 10
    testTrxGenDurationSec=60*30
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[account1Name, account2Name],
                                acctPrivKeysList=[account1PrivKey,account2PrivKey], nodeId=snapshotNodeId, tpsPerGenerator=targetTpsPerGenerator,
                                numGenerators=trxGeneratorCnt, durationSec=testTrxGenDurationSec, waitToComplete=False)

    cluster.waitForTrxGeneratorsSpinup(nodeId=snapshotNodeId, numGenerators=trxGeneratorCnt)

    blockNum=node0.getBlockNum(BlockType.head)
    timePerBlock=500
    transactionsPerBlock=targetTpsPerGenerator*trxGeneratorCnt*timePerBlock/1000
    steadyStateWait=30
    startBlockNum=blockNum+steadyStateWait
    numBlocks=30
    endBlockNum=startBlockNum+numBlocks
    waitForBlock(node0, endBlockNum)
    steadyStateWindowTrxs=0
    steadyStateAvg=0
    steadyStateWindowBlks=0
    for bNum in range(startBlockNum, endBlockNum):
        steadyStateWindowBlks=steadyStateWindowBlks+1
        block=node0.getBlock(bNum)
        steadyStateWindowTrxs+=len(block["transactions"])

    steadyStateAvg=steadyStateWindowTrxs / steadyStateWindowBlks

    Print("Validate transactions are generating")
    minReqPctLeeway=0.60
    minRequiredTransactions=minReqPctLeeway*transactionsPerBlock
    assert steadyStateAvg>=minRequiredTransactions, "Expected to at least receive %s transactions per block, but only getting %s" % (minRequiredTransactions, steadyStateAvg)

    Print("Create snapshot")
    ret = nodeSnap.scheduleSnapshot()
    assert ret is not None, "Snapshot creation failed"
  
    # scheduleSnapshot is async, not returning block info
    Print("Wait for snapshot node lib to advance")
    ret_head_block_num = nodeSnap.getBlockNum(BlockType.head)
    waitForBlock(nodeSnap, ret_head_block_num+1, blockType=BlockType.lib)

    Print("Kill snapshot node")
    nodeSnap.kill(signal.SIGTERM)

    Print("Convert snapshot to JSON")
    snapshotFile = getLatestSnapshot(snapshotNodeId)
    Utils.processLeapUtilCmd("snapshot to-json --input-file {}".format(snapshotFile), "snapshot to-json", silentErrors=False)
    snapshotFile = snapshotFile + ".json"

    Print("Trim irreversible blocklog to snapshot head block num")
    nodeIrr.kill(signal.SIGTERM)
    output=cluster.getBlockLog(irrNodeId, blockLogAction=BlockLogAction.trim, last=ret_head_block_num, throwException=True)

    Print("Relaunch irreversible node in irreversible mode")
    removeState(irrNodeId)
    Utils.rmFromFile(Utils.getNodeConfigDir(irrNodeId, "config.ini"), "p2p-peer-address")
    swapFlags = {"--read-mode":"irreversible", "--p2p-max-nodes-per-host":"0", "--max-clients":"0", "--allowed-connection":"none"}
    isRelaunchSuccess = nodeIrr.relaunch(chainArg="--replay", addSwapFlags=swapFlags, timeout=relaunchTimeout, cachePopen=True)
    assert isRelaunchSuccess, "Failed to relaunch snapshot node"

    Print("Create snapshot from irreversible")
    ret = nodeIrr.createSnapshot()
    assert ret is not None, "Snapshot creation failed"
    ret_irr_head_block_num = ret["payload"]["head_block_num"]
    Print(f"Snapshot head block number {ret_irr_head_block_num}")
    assert ret_irr_head_block_num == ret_head_block_num, f"Snapshot head block numbers do not match: {ret_irr_head_block_num} != {ret_head_block_num}"

    Print("Kill snapshot node")
    nodeIrr.kill(signal.SIGTERM)

    Print("Convert snapshot to JSON")
    irrSnapshotFile = getLatestSnapshot(irrNodeId)
    Utils.processLeapUtilCmd("snapshot to-json --input-file {}".format(irrSnapshotFile), "snapshot to-json", silentErrors=False)
    irrSnapshotFile = irrSnapshotFile + ".json"

    assert Utils.compareFiles(snapshotFile, irrSnapshotFile), f"Snapshot files differ {snapshotFile} != {irrSnapshotFile}"

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killWallet, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)