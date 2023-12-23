#!/usr/bin/env python3

import signal
import json
import time
import os
import filecmp

from TestHarness import Account, Cluster, Node, TestHelper, Utils, WalletMgr

###############################################################
# nodeos_chainbase_allocation_test
#
# Test snapshot creation and restarting from snapshot
#
###############################################################

# Parse command line arguments
args = TestHelper.parse_args({"-v","--dump-error-details","--leave-running","--keep-logs","--unshared"})
Utils.Debug = args.v
dumpErrorDetails=args.dump_error_details

walletMgr=WalletMgr(True)
cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
try:
    TestHelper.printSystemInfo("BEGIN")

    # The following is the list of chainbase objects that need to be verified:
    # - account_object (bootstrap)
    # - code_object (bootstrap)
    # - global_property_object
    # - key_value_object (bootstrap)
    # - protocol_state_object (bootstrap)
    # - permission_object (bootstrap)
    # The bootstrap process has created account_object and code_object (by uploading the bios contract),
    # key_value_object (token creation), protocol_state_object (preactivation feature), and permission_object
    # (automatically taken care by the automatically generated eosio account)
    assert cluster.launch(
        pnodes=1,
        prodCount=1,
        totalProducers=1,
        totalNodes=3,
        loadSystemContract=False,
        specificExtraNodeosArgs={
            1:"--read-mode irreversible --plugin eosio::producer_api_plugin"})

    producerNodeId = 0
    irrNodeId = 1
    nonProdNodeId = 2
    producerNode = cluster.getNode(producerNodeId)
    irrNode = cluster.getNode(irrNodeId)
    nonProdNode = cluster.getNode(nonProdNodeId)

    # Schedule a new producer to trigger new producer schedule for "global_property_object"
    newProducerAcc = Account("newprod")
    newProducerAcc.ownerPublicKey = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
    newProducerAcc.activePublicKey = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
    nonProdNode.createAccount(newProducerAcc, cluster.eosioAccount, waitForTransBlock=True)

    setProdsStr = '{"schedule": ['
    setProdsStr += '{"producer_name":' + newProducerAcc.name + ',"authority": ["block_signing_authority_v0", {"threshold":1, "keys":[{"key":' + newProducerAcc.activePublicKey + ', "weight":1}]}]}'
    setProdsStr += ']}'
    cmd="push action -j eosio setprods '{}' -p eosio".format(setProdsStr)
    trans = producerNode.processCleosCmd(cmd, cmd, silentErrors=False)
    assert trans
    setProdsBlockNum = int(trans["processed"]["block_num"])

    # Wait until the block where set prods is executed become irreversible so the producer schedule
    def isSetProdsBlockNumIrr():
            return producerNode.getIrreversibleBlockNum() >= setProdsBlockNum
    Utils.waitForBool(isSetProdsBlockNumIrr, timeout=30, sleepTime=0.1)
    # Once it is irreversible, immediately pause the producer so the promoted producer schedule is not cleared
    producerNode.processUrllibRequest("producer", "pause")

    producerNode.kill(signal.SIGTERM)

    # Create the snapshot and rename it to avoid name conflict later on
    res = irrNode.createSnapshot()
    beforeShutdownSnapshotPath = res["payload"]["snapshot_name"]
    snapshotPathWithoutExt, snapshotExt = os.path.splitext(beforeShutdownSnapshotPath)
    os.rename(beforeShutdownSnapshotPath, snapshotPathWithoutExt + "_before_shutdown" + snapshotExt)

    # Restart irr node and ensure the snapshot is still identical
    irrNode.kill(signal.SIGTERM)
    isRelaunchSuccess = irrNode.relaunch(timeout=5)
    assert isRelaunchSuccess, "Fail to relaunch"
    res = irrNode.createSnapshot()
    afterShutdownSnapshotPath = res["payload"]["snapshot_name"]
    assert filecmp.cmp(beforeShutdownSnapshotPath, afterShutdownSnapshotPath), "snapshot is not identical"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
