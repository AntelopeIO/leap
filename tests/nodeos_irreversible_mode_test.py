#!/usr/bin/env python3

import re
import os
import time
import signal
import subprocess
import shutil

from TestHarness import Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr

###############################################################
# nodeos_irreversible_mode_test
#
# Many smaller tests centered around irreversible mode
#
###############################################################

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30
numOfProducers = 4
totalNodes = 20

# Parse command line arguments
args = TestHelper.parse_args({"-v","--dump-error-details","--leave-running","--keep-logs","--unshared"})
Utils.Debug = args.v
dumpErrorDetails=args.dump_error_details
speculativeReadMode="head"
blockLogRetainBlocks="10000"

# Setup cluster and it's wallet manager
walletMgr=WalletMgr(True)
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

def backupBlksDir(nodeId):
   dataDir = Utils.getNodeDataDir(nodeId)
   sourceDir = os.path.join(dataDir, "blocks")
   destinationDir = os.path.join(os.path.dirname(dataDir), os.path.basename(dataDir) + "-backup", "blocks")
   shutil.copytree(sourceDir, destinationDir)

def recoverBackedupBlksDir(nodeId):
   dataDir = Utils.getNodeDataDir(nodeId)
   # Delete existing one and copy backed up one
   existingBlocksDir = os.path.join(dataDir, "blocks")
   backedupBlocksDir = os.path.join(os.path.dirname(dataDir), os.path.basename(dataDir) + "-backup", "blocks")
   shutil.rmtree(existingBlocksDir, ignore_errors=True)
   shutil.copytree(backedupBlocksDir, existingBlocksDir)

def getLatestSnapshot(nodeId):
   snapshotDir = os.path.join(Utils.getNodeDataDir(nodeId), "snapshots")
   snapshotDirContents = os.listdir(snapshotDir)
   assert len(snapshotDirContents) > 0
   snapshotDirContents.sort()
   return os.path.join(snapshotDir, snapshotDirContents[-1])


def removeReversibleBlks(nodeId):
   dataDir = Utils.getNodeDataDir(nodeId)
   reversibleBlks = os.path.join(dataDir, "blocks", "reversible")
   shutil.rmtree(reversibleBlks, ignore_errors=True)

def removeState(nodeId):
   dataDir = Utils.getNodeDataDir(nodeId)
   state = os.path.join(dataDir, "state")
   shutil.rmtree(state, ignore_errors=True)

def getHeadLibAndForkDbHead(node: Node):
   info = node.getInfo()
   assert info is not None, "Fail to retrieve info from the node, the node is currently having a problem"
   head = int(info["head_block_num"])
   lib = int(info["last_irreversible_block_num"])
   forkDbHead =  int(info["fork_db_head_block_num"])
   return head, lib, forkDbHead

# Wait for some time until LIB advance
def waitForBlksProducedAndLibAdvanced():
   requiredConfirmation = int(2 / 3 * numOfProducers) + 1
   maxNumOfBlksReqToConfirmLib = (12 * requiredConfirmation - 1) * 2
   # Give 6 seconds buffer time
   bufferTime = 6
   timeToWait = maxNumOfBlksReqToConfirmLib / 2 + bufferTime
   time.sleep(timeToWait)

# Ensure that the relaunched node received blks from producers, in other words head and lib is advancing
def ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest):
   head, lib, forkDbHead = getHeadLibAndForkDbHead(nodeToTest)
   waitForBlksProducedAndLibAdvanced()
   headAfterWaiting, libAfterWaiting, forkDbHeadAfterWaiting = getHeadLibAndForkDbHead(nodeToTest)
   assert headAfterWaiting > head and libAfterWaiting > lib and forkDbHeadAfterWaiting > forkDbHead, \
      "Either Head ({} -> {})/ Lib ({} -> {})/ Fork Db Head ({} -> {}) is not advancing".format(head, headAfterWaiting, lib, libAfterWaiting, forkDbHead, forkDbHeadAfterWaiting)

# Confirm the head lib and fork db of irreversible mode
# Under any condition of irreversible mode:
# - forkDbHead >= head == lib
# headLibAndForkDbHeadBeforeSwitchMode should be only passed IF production is disabled, otherwise it provides erroneous check
# When comparing with the the state before node is switched:
# - head == libBeforeSwitchMode == lib and forkDbHead == headBeforeSwitchMode == forkDbHeadBeforeSwitchMode
def confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode=None):
   head, lib, forkDbHead = getHeadLibAndForkDbHead(nodeToTest)
   assert head == lib, "Head ({}) should be equal to lib ({})".format(head, lib)
   assert forkDbHead >= head, "Fork db head ({}) should be larger or equal to the head ({})".format(forkDbHead, head)

   if headLibAndForkDbHeadBeforeSwitchMode:
      headBeforeSwitchMode, libBeforeSwitchMode, forkDbHeadBeforeSwitchMode = headLibAndForkDbHeadBeforeSwitchMode
      assert head == libBeforeSwitchMode, "Head ({}) should be equal to lib before switch mode ({})".format(head, libBeforeSwitchMode)
      assert lib == libBeforeSwitchMode, "Lib ({}) should be equal to lib before switch mode ({})".format(lib, libBeforeSwitchMode)
      assert forkDbHead == headBeforeSwitchMode and forkDbHead == forkDbHeadBeforeSwitchMode, \
         "Fork db head ({}) should be equal to head before switch mode ({}) and fork db head before switch mode ({})".format(forkDbHead, headBeforeSwitchMode, forkDbHeadBeforeSwitchMode)

# Confirm lib of irreversible mode
# Under any condition of irreversible mode:
# - forkDbHead >= head == lib
# headLibAndForkDbHeadBeforeSwitchMode should be only passed IF production is disabled, otherwise it provides erroneous check
# When comparing with the the state before node is switched:
# - head == libBeforeSwitchMode == lib and forkDbHead == libBeforeSwitchMode == forkDbLibBeforeSwitchMode
def confirmLibOfIrrMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode=None):
   head, lib, forkDbHead = getHeadLibAndForkDbHead(nodeToTest)
   assert head == lib, "Head ({}) should be equal to lib ({})".format(head, lib)
   assert forkDbHead >= head, "Fork db head ({}) should be larger or equal to the head ({})".format(forkDbHead, head)

   if headLibAndForkDbHeadBeforeSwitchMode:
      headBeforeSwitchMode, libBeforeSwitchMode, forkDbHeadBeforeSwitchMode = headLibAndForkDbHeadBeforeSwitchMode
      assert head == libBeforeSwitchMode, "Head ({}) should be equal to lib before switch mode ({})".format(head, libBeforeSwitchMode)
      assert lib == libBeforeSwitchMode, "Lib ({}) should be equal to lib before switch mode ({})".format(lib, libBeforeSwitchMode)
      assert forkDbHead == libBeforeSwitchMode, \
         "Fork db head ({}) should be equal to lib before switch mode ({})".format(forkDbHead, libBeforeSwitchMode)

# Confirm the head lib and fork db of speculative mode
# Under any condition of speculative mode:
# - forkDbHead == head >= lib
# headLibAndForkDbHeadBeforeSwitchMode should be only passed IF production is disabled, otherwise it provides erroneous check
# When comparing with the the state before node is switched:
# - head == forkDbHeadBeforeSwitchMode == forkDbHead and lib == headBeforeSwitchMode == libBeforeSwitchMode
def confirmHeadLibAndForkDbHeadOfSpecMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode=None):
   head, lib, forkDbHead = getHeadLibAndForkDbHead(nodeToTest)
   assert head >= lib, "Head should be larger or equal to lib (head: {}, lib: {})".format(head, lib)
   assert head == forkDbHead, "Head ({}) should be equal to fork db head ({})".format(head, forkDbHead)

   if headLibAndForkDbHeadBeforeSwitchMode:
      headBeforeSwitchMode, libBeforeSwitchMode, forkDbHeadBeforeSwitchMode = headLibAndForkDbHeadBeforeSwitchMode
      assert head == forkDbHeadBeforeSwitchMode, "Head ({}) should be equal to fork db head before switch mode ({})".format(head, forkDbHeadBeforeSwitchMode)
      assert lib == headBeforeSwitchMode and lib == libBeforeSwitchMode, \
         "Lib ({}) should be equal to head before switch mode ({}) and lib before switch mode ({})".format(lib, headBeforeSwitchMode, libBeforeSwitchMode)
      assert forkDbHead == forkDbHeadBeforeSwitchMode, \
         "Fork db head ({}) should be equal to fork db head before switch mode ({}) ".format(forkDbHead, forkDbHeadBeforeSwitchMode)

def relaunchNode(node: Node, chainArg="", addSwapFlags=None, relaunchAssertMessage="Fail to relaunch"):
   isRelaunchSuccess = node.relaunch(chainArg=chainArg, addSwapFlags=addSwapFlags, timeout=relaunchTimeout)
   time.sleep(1) # Give a second to replay or resync if needed
   assert isRelaunchSuccess, relaunchAssertMessage
   return isRelaunchSuccess

# List to contain the test result message
testResultMsgs = []
testSuccessful = False
try:
   # Kill any existing instances and launch cluster
   TestHelper.printSystemInfo("BEGIN")
   cluster.launch(
      prodCount=numOfProducers,
      totalProducers=numOfProducers,
      totalNodes=totalNodes,
      pnodes=1,
      topo="mesh",
      specificExtraNodeosArgs={
         0:"--enable-stale-production",
         4:"--read-mode irreversible",
         6:"--read-mode irreversible",
         9:"--plugin eosio::producer_api_plugin",
        10:"--read-mode speculative",
        11:"--read-mode irreversible",
        12:"--read-mode speculative",
        13:"--read-mode irreversible",
        14:"--read-mode speculative --plugin eosio::producer_api_plugin",
        15:"--read-mode speculative",
        16:"--read-mode irreversible",
        17:"--read-mode speculative",
        18:"--read-mode irreversible",
        19:"--read-mode speculative --plugin eosio::producer_api_plugin"
        })

   producingNodeId = 0
   producingNode = cluster.getNode(producingNodeId)

   def stopProdNode():
      if not producingNode.killed:
         producingNode.kill(signal.SIGTERM)

   def startProdNode():
      if producingNode.killed:
         relaunchNode(producingNode)

   # Give some time for it to produce, so lib is advancing
   waitForBlksProducedAndLibAdvanced()

   # Kill all nodes, so we can test all node in isolated environment
   for clusterNode in cluster.nodes:
      clusterNode.kill(signal.SIGTERM)
   cluster.biosNode.kill(signal.SIGTERM)

   # Wrapper function to execute test
   # This wrapper function will resurrect the node to be tested, and shut it down by the end of the test
   def executeTest(nodeIdOfNodeToTest, runTestScenario):
      testResult = False
      resultDesc = None
      try:
         # Relaunch killed node so it can be used for the test
         nodeToTest = cluster.getNode(nodeIdOfNodeToTest)
         Utils.Print("Re-launch node #{} to excute test scenario: {}".format(nodeIdOfNodeToTest, runTestScenario.__name__))
         relaunchNode(nodeToTest, relaunchAssertMessage="Fail to relaunch before running test scenario")

         # Run test scenario
         runTestScenario(nodeIdOfNodeToTest, nodeToTest)
         resultDesc = "!!!TEST CASE #{} ({}) IS SUCCESSFUL".format(nodeIdOfNodeToTest, runTestScenario.__name__)
         testResult = True
      except Exception as e:
         resultDesc = "!!!BUG ON TEST CASE #{} ({}): {}".format(nodeIdOfNodeToTest, runTestScenario.__name__, e)
      finally:
         Utils.Print(resultDesc)
         testResultMsgs.append(resultDesc)
         # Kill node after use
         if not nodeToTest.killed: nodeToTest.kill(signal.SIGTERM)
      return testResult

   # 1st test case: Replay in irreversible mode with reversible blks
   # Expectation: Node replays and launches successfully and forkdb head, head, and lib matches the irreversible mode expectation
   def replayInIrrModeWithRevBlks(nodeIdOfNodeToTest, nodeToTest):
      # Track head blk num and lib before shutdown
      headLibAndForkDbHeadBeforeSwitchMode = getHeadLibAndForkDbHead(nodeToTest)

      # Kill node and replay in irreversible mode
      nodeToTest.kill(signal.SIGTERM)
      relaunchNode(nodeToTest, chainArg=" --read-mode irreversible --replay")

      # Confirm state
      confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode)

   # 2nd test case: Replay in irreversible mode without reversible blks
   # Expectation: Node replays and launches successfully and forkdb head, head, and lib matches the irreversible mode expectation
   def replayInIrrModeWithoutRevBlks(nodeIdOfNodeToTest, nodeToTest):
      # Track head blk num and lib before shutdown
      headLibAndForkDbHeadBeforeSwitchMode = getHeadLibAndForkDbHead(nodeToTest)

      # Shut down node, remove reversible blks and relaunch
      nodeToTest.kill(signal.SIGTERM)
      removeReversibleBlks(nodeIdOfNodeToTest)
      relaunchNode(nodeToTest, chainArg=" --read-mode irreversible --replay")

      # Ensure the node condition is as expected after relaunch
      confirmLibOfIrrMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode)

   # 3rd test case: Switch mode speculative -> irreversible without replay
   # Expectation: Node switches mode successfully and forkdb head, head, and lib matches the irreversible mode expectation
   def switchSpecToIrrMode(nodeIdOfNodeToTest, nodeToTest):
      # Track head blk num and lib before shutdown
      headLibAndForkDbHeadBeforeSwitchMode = getHeadLibAndForkDbHead(nodeToTest)

      # Kill and relaunch in irreversible mode
      nodeToTest.kill(signal.SIGTERM)
      relaunchNode(nodeToTest, addSwapFlags={"--read-mode": "irreversible", "--block-log-retain-blocks":blockLogRetainBlocks})

      # Ensure the node condition is as expected after relaunch
      confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode)

   # 4th test case: Switch mode irreversible -> speculative without replay
   # Expectation: Node switches mode successfully and forkdb head, head, and lib matches the speculative mode expectation
   def switchIrrToSpecMode(nodeIdOfNodeToTest, nodeToTest):
      # Track head blk num and lib before shutdown
      headLibAndForkDbHeadBeforeSwitchMode = getHeadLibAndForkDbHead(nodeToTest)

      # Kill and relaunch in speculative mode
      nodeToTest.kill(signal.SIGTERM)
      relaunchNode(nodeToTest, addSwapFlags={"--read-mode": speculativeReadMode, "--block-log-retain-blocks":blockLogRetainBlocks})

      # Ensure the node condition is as expected after relaunch
      confirmHeadLibAndForkDbHeadOfSpecMode(nodeToTest, headLibAndForkDbHeadBeforeSwitchMode)

   # 5th test case: Switch mode speculative -> irreversible without replay and connected to producing node
   # Expectation: Node switches mode successfully
   #              and the head and lib should be advancing after some blocks produced
   #              and forkdb head, head, and lib matches the irreversible mode expectation
   def switchSpecToIrrModeWithConnectedToProdNode(nodeIdOfNodeToTest, nodeToTest):
      try:
         startProdNode()

         # Kill and relaunch in irreversible mode
         nodeToTest.kill(signal.SIGTERM)
         waitForBlksProducedAndLibAdvanced() # Wait for some blks to be produced and lib advance
         relaunchNode(nodeToTest, addSwapFlags={"--read-mode": "irreversible", "--block-log-retain-blocks":blockLogRetainBlocks})

         # Ensure the node condition is as expected after relaunch
         ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest)
         confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest)
      finally:
         stopProdNode()

   # 6th test case: Switch mode irreversible -> speculative without replay and connected to producing node
   # Expectation: Node switches mode successfully
   #              and the head and lib should be advancing after some blocks produced
   #              and forkdb head, head, and lib matches the speculative mode expectation
   def switchIrrToSpecModeWithConnectedToProdNode(nodeIdOfNodeToTest, nodeToTest):
      try:
         startProdNode()

         # Kill and relaunch in irreversible mode
         nodeToTest.kill(signal.SIGTERM)
         waitForBlksProducedAndLibAdvanced() # Wait for some blks to be produced and lib advance)
         relaunchNode(nodeToTest, addSwapFlags={"--read-mode": speculativeReadMode, "--block-log-retain-blocks":blockLogRetainBlocks})

         # Ensure the node condition is as expected after relaunch
         ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest)
         confirmHeadLibAndForkDbHeadOfSpecMode(nodeToTest)
      finally:
         stopProdNode()

   # 7th test case: Replay in irreversible mode with reversible blks while connected to producing node
   # Expectation: Node replays and launches successfully
   #              and the head and lib should be advancing after some blocks produced
   #              and forkdb head, head, and lib matches the irreversible mode expectation
   def replayInIrrModeWithRevBlksAndConnectedToProdNode(nodeIdOfNodeToTest, nodeToTest):
      try:
         startProdNode()
         # Kill node and replay in irreversible mode
         nodeToTest.kill(signal.SIGTERM)
         waitForBlksProducedAndLibAdvanced() # Wait
         relaunchNode(nodeToTest, chainArg=" --read-mode irreversible --replay")

         # Ensure the node condition is as expected after relaunch
         ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest)
         confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest)
      finally:
         stopProdNode()

   # 8th test case: Replay in irreversible mode without reversible blks while connected to producing node
   # Expectation: Node replays and launches successfully
   #              and the head and lib should be advancing after some blocks produced
   #              and forkdb head, head, and lib matches the irreversible mode expectation
   def replayInIrrModeWithoutRevBlksAndConnectedToProdNode(nodeIdOfNodeToTest, nodeToTest):
      try:
         startProdNode()

         # Kill node, remove rev blks and then replay in irreversible mode
         nodeToTest.kill(signal.SIGTERM)
         removeReversibleBlks(nodeIdOfNodeToTest)
         waitForBlksProducedAndLibAdvanced() # Wait
         relaunchNode(nodeToTest, chainArg=" --read-mode irreversible --replay")

         # Ensure the node condition is as expected after relaunch
         ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest)
         confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest)
      finally:
         stopProdNode()

   # 9th test case: Switch to speculative mode while using irreversible mode snapshots and using backed up speculative blocks
   # Expectation: Node replays and launches successfully
   #              and the head and lib should be advancing after some blocks produced
   #              and forkdb head, head, and lib should stay the same after relaunch
   def switchToSpecModeWithIrrModeSnapshot(nodeIdOfNodeToTest, nodeToTest):
      try:
         # Kill node and backup blocks directory of speculative mode
         headLibAndForkDbHeadBeforeShutdown = getHeadLibAndForkDbHead(nodeToTest)
         nodeToTest.kill(signal.SIGTERM)
         backupBlksDir(nodeIdOfNodeToTest)

         # Relaunch in irreversible mode and create the snapshot
         relaunchNode(nodeToTest, addSwapFlags={"--read-mode": "irreversible"})
         confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest)
         nodeToTest.createSnapshot()
         nodeToTest.kill(signal.SIGTERM)

         # Start from clean data dir, recover back up blocks, and then relaunch with irreversible snapshot
         removeState(nodeIdOfNodeToTest)
         recoverBackedupBlksDir(nodeIdOfNodeToTest) # this function will delete the existing blocks dir first
         relaunchNode(nodeToTest, chainArg=" --snapshot {}".format(getLatestSnapshot(nodeIdOfNodeToTest)), addSwapFlags={"--read-mode": speculativeReadMode})
         confirmHeadLibAndForkDbHeadOfSpecMode(nodeToTest)
         # Ensure it automatically replays "reversible blocks", i.e. head lib and fork db should be the same
         headLibAndForkDbHeadAfterRelaunch = getHeadLibAndForkDbHead(nodeToTest)
         assert headLibAndForkDbHeadBeforeShutdown == headLibAndForkDbHeadAfterRelaunch, \
            "1: Head, Lib, and Fork Db after relaunch is different {} vs {}".format(headLibAndForkDbHeadBeforeShutdown, headLibAndForkDbHeadAfterRelaunch)

         # Start production and wait until lib advance, ensure everything is alright
         startProdNode()
         ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest)

         # Note the head, lib and fork db head
         stopProdNode()
         headLibAndForkDbHeadBeforeShutdown = getHeadLibAndForkDbHead(nodeToTest)
         nodeToTest.kill(signal.SIGTERM)

         # Relaunch the node again (using the same snapshot)
         # This time ensure it automatically replays both "irreversible blocks" and "reversible blocks", i.e. the end result should be the same as before shutdown
         removeState(nodeIdOfNodeToTest)
         relaunchNode(nodeToTest)
         headLibAndForkDbHeadAfterRelaunch = getHeadLibAndForkDbHead(nodeToTest)
         assert headLibAndForkDbHeadBeforeShutdown == headLibAndForkDbHeadAfterRelaunch, \
            "2: Head, Lib, and Fork Db after relaunch is different {} vs {}".format(headLibAndForkDbHeadBeforeShutdown, headLibAndForkDbHeadAfterRelaunch)
      finally:
         stopProdNode()

   # 10th test case: Load an irreversible snapshot into a node running without a block log
   # Expectation: Node launches successfully
   #              and the head and lib should be advancing after some blocks produced
   def switchToNoBlockLogWithIrrModeSnapshot(nodeIdOfNodeToTest, nodeToTest):
      try:
         # Kill node and backup blocks directory of speculative mode
         headLibAndForkDbHeadBeforeShutdown = getHeadLibAndForkDbHead(nodeToTest)
         nodeToTest.kill(signal.SIGTERM)

         # Relaunch in irreversible mode and create the snapshot
         relaunchNode(nodeToTest, addSwapFlags={"--read-mode": "irreversible", "--block-log-retain-blocks":"0"})
         confirmHeadLibAndForkDbHeadOfIrrMode(nodeToTest)
         nodeToTest.createSnapshot()
         nodeToTest.kill(signal.SIGTERM)

         # Start from clean data dir and then relaunch with irreversible snapshot, no block log means that fork_db will be reset
         removeState(nodeIdOfNodeToTest)
         relaunchNode(nodeToTest, chainArg=" --snapshot {}".format(getLatestSnapshot(nodeIdOfNodeToTest)), addSwapFlags={"--read-mode": speculativeReadMode, "--block-log-retain-blocks":"0"})
         confirmHeadLibAndForkDbHeadOfSpecMode(nodeToTest)
         # Ensure it does not replay "reversible blocks", i.e. head and lib should be different
         headLibAndForkDbHeadAfterRelaunch = getHeadLibAndForkDbHead(nodeToTest)
         assert headLibAndForkDbHeadBeforeShutdown != headLibAndForkDbHeadAfterRelaunch, \
            "1: Head, Lib, and Fork Db same after relaunch {} vs {}".format(headLibAndForkDbHeadBeforeShutdown, headLibAndForkDbHeadAfterRelaunch)

         # Start production and wait until lib advance, ensure everything is alright
         startProdNode()
         ensureHeadLibAndForkDbHeadIsAdvancing(nodeToTest)

         # Note the head, lib and fork db head
         stopProdNode()
         headLibAndForkDbHeadBeforeShutdown = getHeadLibAndForkDbHead(nodeToTest)
         nodeToTest.kill(signal.SIGTERM)

         # Relaunch the node again (using the same snapshot)
         # The end result should be the same as before shutdown
         removeState(nodeIdOfNodeToTest)
         relaunchNode(nodeToTest)
         headLibAndForkDbHeadAfterRelaunch2 = getHeadLibAndForkDbHead(nodeToTest)
         assert headLibAndForkDbHeadAfterRelaunch == headLibAndForkDbHeadAfterRelaunch2, \
            "2: Head, Lib, and Fork Db after relaunch is different {} vs {}".format(headLibAndForkDbHeadAfterRelaunch, headLibAndForkDbHeadAfterRelaunch2)
      finally:
         stopProdNode()


   # Start executing test cases here
   testSuccessful = executeTest(1, replayInIrrModeWithRevBlks)
   testSuccessful = testSuccessful and executeTest(2, replayInIrrModeWithoutRevBlks)
   testSuccessful = testSuccessful and executeTest(3, switchSpecToIrrMode)
   testSuccessful = testSuccessful and executeTest(4, switchIrrToSpecMode)
   testSuccessful = testSuccessful and executeTest(5, switchSpecToIrrModeWithConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(6, switchIrrToSpecModeWithConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(7, replayInIrrModeWithRevBlksAndConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(8, replayInIrrModeWithoutRevBlksAndConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(9, switchToSpecModeWithIrrModeSnapshot)

   # retest with read-mode speculative instead of head
   speculativeReadMode="speculative"
   testSuccessful = testSuccessful and executeTest(10, switchSpecToIrrMode)
   testSuccessful = testSuccessful and executeTest(11, switchIrrToSpecMode)
   testSuccessful = testSuccessful and executeTest(12, switchSpecToIrrModeWithConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(13, switchIrrToSpecModeWithConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(14, switchToSpecModeWithIrrModeSnapshot)

   # retest with read-mode head and no block log
   speculativeReadMode="head"
   blockLogRetainBlocks="0"
   testSuccessful = testSuccessful and executeTest(15, switchSpecToIrrMode)
   testSuccessful = testSuccessful and executeTest(16, switchIrrToSpecMode)
   testSuccessful = testSuccessful and executeTest(17, switchSpecToIrrModeWithConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(18, switchIrrToSpecModeWithConnectedToProdNode)
   testSuccessful = testSuccessful and executeTest(19, switchToNoBlockLogWithIrrModeSnapshot)

finally:
   TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)
   # Print test result
   for msg in testResultMsgs:
      Print(msg)
   if not testSuccessful and len(testResultMsgs) < 9:
      Print("Subsequent tests were not run after failing test scenario.")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
