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
# nodeos_snapshot_diff_test
#
#  Test configures a producing node and 2 non-producing nodes.
#  Configures trx_generator(s) and starts generating transactions and sending them
#  to the producing node.
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
args = TestHelper.parse_args({"--dump-error-details","--keep-logs","-v","--leave-running","--wallet-port","--unshared"},
                             applicationSpecificArgs=appArgs)

relaunchTimeout = 30
Utils.Debug=args.v
pnodes=1
testAccounts = 2
trxGeneratorCnt=2
startedNonProdNodes = 3
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
dumpErrorDetails=args.dump_error_details
prodCount=2
walletPort=args.wallet_port
totalNodes=startedNonProdNodes+pnodes

walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

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

    Print("Stand up cluster")
    if cluster.launch(prodCount=prodCount, onlyBios=False, pnodes=pnodes, totalNodes=totalNodes, totalProducers=pnodes*prodCount,
                      loadSystemContract=True, maximumP2pPerHost=totalNodes+trxGeneratorCnt) is False:
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Create test wallet")
    wallet = walletMgr.create('txntestwallet')
    cluster.populateWallet(2, wallet)

    Print("Create test accounts for transactions.")
    cluster.createAccounts(cluster.eosioAccount, stakedDeposit=0)

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
    progNodeId = irrNodeId+1

    nodeSnap=cluster.getNode(snapshotNodeId)
    nodeIrr=cluster.getNode(irrNodeId)
    nodeProg=cluster.getNode(progNodeId)

    Print("Wait for account creation to be irreversible")
    blockNum=node0.getBlockNum(BlockType.head)
    waitForBlock(node0, blockNum, blockType=BlockType.lib)

    Print("Configure and launch txn generators")
    
    targetTpsPerGenerator = 10
    testTrxGenDurationSec=60*30
    cluster.launchTrxGenerators(contractOwnerAcctName=cluster.eosioAccount.name, acctNamesList=[account1Name, account2Name],
                                acctPrivKeysList=[account1PrivKey,account2PrivKey], nodeId=snapshotNodeId, tpsPerGenerator=targetTpsPerGenerator,
                                numGenerators=trxGeneratorCnt, durationSec=testTrxGenDurationSec, waitToComplete=False)

    status = cluster.waitForTrxGeneratorsSpinup(nodeId=snapshotNodeId, numGenerators=trxGeneratorCnt)
    assert status is not None, "ERROR: Failed to spinup Transaction Generators"

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
    
    Print("Create snapshot (node 0)")
    ret = nodeSnap.createSnapshot()
    assert ret is not None, "Snapshot creation failed"
    ret_head_block_num = ret["payload"]["head_block_num"]
    Print(f"Snapshot head block number {ret_head_block_num}")

    Print("Wait for snapshot node lib to advance")
    waitForBlock(nodeSnap, ret_head_block_num+1, blockType=BlockType.lib)

    Print("Kill snapshot node")
    nodeSnap.kill(signal.SIGTERM)

    Print("Convert snapshot to JSON")
    snapshotFile = getLatestSnapshot(snapshotNodeId)
    Utils.processLeapUtilCmd("snapshot to-json --input-file {}".format(snapshotFile), "snapshot to-json", silentErrors=False)
    snapshotFile = snapshotFile + ".json"

    Print("Trim programmable blocklog to snapshot head block num and relaunch programmable node")
    nodeProg.kill(signal.SIGTERM)
    output=cluster.getBlockLog(progNodeId, blockLogAction=BlockLogAction.trim, first=0, last=ret_head_block_num, throwException=True)
    removeState(progNodeId)
    nodeProg.rmFromCmd('--p2p-peer-address')
    isRelaunchSuccess = nodeProg.relaunch(chainArg="--replay", addSwapFlags={}, timeout=relaunchTimeout)
    assert isRelaunchSuccess, "Failed to relaunch programmable node"

    Print("Schedule snapshot (node 2)")
    ret = nodeProg.scheduleSnapshotAt(ret_head_block_num)
    assert ret is not None, "Snapshot scheduling failed"

    Print("Wait for programmable node lib to advance")
    waitForBlock(nodeProg, ret_head_block_num+1, blockType=BlockType.lib)

    Print("Kill programmable node")
    nodeProg.kill(signal.SIGTERM)

    Print("Convert snapshot to JSON")
    progSnapshotFile = getLatestSnapshot(progNodeId)
    Utils.processLeapUtilCmd("snapshot to-json --input-file {}".format(progSnapshotFile), "snapshot to-json", silentErrors=False)
    progSnapshotFile = progSnapshotFile + ".json"

    Print("Trim irreversible blocklog to snapshot head block num")
    nodeIrr.kill(signal.SIGTERM)
    output=cluster.getBlockLog(irrNodeId, blockLogAction=BlockLogAction.trim, first=0, last=ret_head_block_num, throwException=True)

    Print("Relaunch irreversible node in irreversible mode")
    removeState(irrNodeId)
    nodeIrr.rmFromCmd('--p2p-peer-address')
    swapFlags = {"--read-mode":"irreversible", "--p2p-max-nodes-per-host":"0", "--max-clients":"0", "--allowed-connection":"none"}
    isRelaunchSuccess = nodeIrr.relaunch(chainArg="--replay", addSwapFlags=swapFlags, timeout=relaunchTimeout)
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
    assert Utils.compareFiles(progSnapshotFile, irrSnapshotFile), f"Snapshot files differ {progSnapshotFile} != {irrSnapshotFile}"

    testSuccessful=True

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
