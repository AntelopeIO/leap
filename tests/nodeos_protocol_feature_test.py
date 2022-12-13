#!/usr/bin/env python3

import signal
import json
from os.path import join
from datetime import datetime

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr
from TestHarness.Cluster import PFSetupPolicy

###############################################################
# nodeos_protocol_feature_test
#
# Many smaller tests centered around irreversible mode
#
###############################################################

# Parse command line arguments
args = TestHelper.parse_args({"-v","--clean-run","--dump-error-details","--leave-running","--keep-logs"})
Utils.Debug = args.v
killAll=args.clean_run
dumpErrorDetails=args.dump_error_details
dontKill=args.leave_running
killEosInstances=not dontKill
killWallet=not dontKill
keepLogs=args.keep_logs

# The following test case will test the Protocol Feature JSON reader of the blockchain

def restartNode(node: Node, chainArg=None, addSwapFlags=None):
    if not node.killed:
        node.kill(signal.SIGTERM)
    isRelaunchSuccess = node.relaunch(chainArg, addSwapFlags=addSwapFlags, timeout=5, cachePopen=True)
    assert isRelaunchSuccess, "Fail to relaunch"

walletMgr=WalletMgr(True)
cluster=Cluster(walletd=True)
cluster.setWalletMgr(walletMgr)

testSuccessful = False
try:
    TestHelper.printSystemInfo("BEGIN")
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    cluster.launch(extraNodeosArgs=" --plugin eosio::producer_api_plugin  --http-max-response-time-ms 990000 ",
                   dontBootstrap=True,
                   pfSetupPolicy=PFSetupPolicy.NONE)
    biosNode = cluster.biosNode

    # Modify the JSON file and then restart the node so it updates the internal state
    newSubjectiveRestrictions = {
        "earliest_allowed_activation_time": "2030-01-01T00:00:00.000",
        "preactivation_required": True,
        "enabled": False
    }
    biosNode.modifyBuiltinPFSubjRestrictions("PREACTIVATE_FEATURE", newSubjectiveRestrictions)
    restartNode(biosNode)

    supportedProtocolFeatureDict = biosNode.getSupportedProtocolFeatureDict()
    preactivateFeatureSubjectiveRestrictions = supportedProtocolFeatureDict["PREACTIVATE_FEATURE"]["subjective_restrictions"]

    # Ensure that the PREACTIVATE_FEATURE subjective restrictions match the value written in the JSON
    assert preactivateFeatureSubjectiveRestrictions == newSubjectiveRestrictions,\
        "PREACTIVATE_FEATURE subjective restrictions are not updated according to the JSON"

    testSuccessful = True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, killEosInstances, killWallet, keepLogs, killAll, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
