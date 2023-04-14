#!/usr/bin/env python3

import decimal
import re
import time

from TestHarness import Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.Cluster import PFSetupPolicy

###############################################################
# prod_preactivation_test
# --dump-error-details <Upon error print etc/eosio/node_*/config.ini and prod_preactivation_test<pid>/node_*/stderr.log to stdout>
# --keep-logs <Don't delete TestLogs/prod_preactivation_test<pid>/node_* folders upon test completion>
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit
cmdError=Utils.cmdError

args = TestHelper.parse_args({"--host","--port","--defproducera_prvt_key","--defproducerb_prvt_key"
                              ,"--dump-error-details","--dont-launch","--keep-logs","-v","--leave-running","--only-bios"
                              ,"--sanity-test","--wallet-port","--unshared"})
server=args.host
port=args.port
debug=args.v
defproduceraPrvtKey=args.defproducera_prvt_key
defproducerbPrvtKey=args.defproducerb_prvt_key
dumpErrorDetails=args.dump_error_details
dontLaunch=args.dont_launch
prodCount=2
onlyBios=args.only_bios
sanityTest=args.sanity_test
walletPort=args.wallet_port

Utils.Debug=debug
localTest=True
cluster=Cluster(host=server, port=port, defproduceraPrvtKey=defproduceraPrvtKey, defproducerbPrvtKey=defproducerbPrvtKey, unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False
dontBootstrap=sanityTest

WalletdName=Utils.EosWalletName
ClientName="cleos"

try:
    TestHelper.printSystemInfo("BEGIN prod_preactivation_test.py")
    cluster.setWalletMgr(walletMgr)
    Print("SERVER: %s" % (server))
    Print("PORT: %d" % (port))

    if localTest and not dontLaunch:
        Print("Stand up cluster")
        if cluster.launch(pnodes=prodCount, totalNodes=prodCount, prodCount=1, onlyBios=onlyBios,
                         dontBootstrap=dontBootstrap,
                         pfSetupPolicy=PFSetupPolicy.NONE, extraNodeosArgs=" --plugin eosio::producer_api_plugin  --http-max-response-time-ms 990000 ") is False:
            cmdError("launcher")
            errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    node = cluster.getNode(0)
    resource = "producer"
    command = "get_supported_protocol_features"
    Print("try to get supported feature list from Node 0 with cmd: %s" % (command))
    feature0 = node.processUrllibRequest(resource, command)

    node = cluster.getNode(1)
    Print("try to get supported feature list from Node 1 with cmd: %s" % (command))
    feature1 = node.processUrllibRequest(resource, command)

    if feature0 != feature1:
        errorExit("feature list mismatch between node 0 and node 1")
    else:
        Print("feature list from node 0 matches with that from node 1")

    if len(feature0) == 0:
        errorExit("No supported feature list")

    digest = ""
    for i in range(0, len(feature0["payload"])):
       feature = feature0["payload"][i]
       if feature["specification"][0]["value"] != "PREACTIVATE_FEATURE":
           continue
       else:
           digest = feature["feature_digest"]

    if len(digest) == 0:
        errorExit("code name PREACTIVATE_FEATURE not found")

    Print("found digest ", digest, " of PREACTIVATE_FEATURE")

    node0 = cluster.getNode(0)
    contract="eosio.bios"
    contractDir="libraries/testing/contracts/old_versions/v1.7.0-develop-preactivate_feature/%s" % (contract)
    wasmFile="%s.wasm" % (contract)
    abiFile="%s.abi" % (contract)

    Print("publish a new bios contract %s should fails because env.is_feature_activated unresolveable" % (contractDir))
    retMap = node0.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, True, shouldFail=True)

    outPut = retMap["output"].decode("utf-8")
    if outPut.find("unresolveable") < 0:
        errorExit(f"bios contract not result in expected unresolveable error: {outPut}")

    secwait = 30
    Print("Wait for node 1 to produce...")
    node = cluster.getNode(1)
    while secwait > 0:
       info = node.getInfo()
       if info["head_block_producer"] >= "defproducerl" and info["head_block_producer"] <= "defproduceru":
          break
       time.sleep(1)
       secwait = secwait - 1

    secwait = 30
    Print("Waiting until node 0 start to produce...")
    node = cluster.getNode(1)
    while secwait > 0:
       info = node.getInfo()
       if info["head_block_producer"] >= "defproducera" and info["head_block_producer"] <= "defproducerk":
          break
       time.sleep(1)
       secwait = secwait - 1

    if secwait <= 0:
       errorExit("No producer of node 0")
    resource = "producer"
    command = "schedule_protocol_feature_activations"
    payload = {"protocol_features_to_activate":[digest]}

    Print("try to preactivate feature on node 1, cmd: /v1/%s/%s %s" % (resource, command, payload))
    result = node.processUrllibRequest(resource, command, payload)

    if result["payload"]["result"] != "ok":
        errorExit("failed to preactivate feature from producer plugin on node 1")
    else:
        Print("feature PREACTIVATE_FEATURE (%s) preactivation success" % (digest))

    time.sleep(0.6)
    Print("publish a new bios contract %s should fails because node1 is not producing block yet" % (contractDir))
    retMap = node0.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, True, shouldFail=True)
    if retMap["output"].decode("utf-8").find("unresolveable") < 0:
        errorExit("bios contract not result in expected unresolveable error")

    Print("now wait for node 1 produce a block...")
    secwait = 30 # wait for node 1 produce a block
    while secwait > 0:
       info = node.getInfo()
       if info["head_block_producer"] >= "defproducerl" and info["head_block_producer"] <= "defproduceru":
          break
       time.sleep(1)
       secwait = secwait - 1

    if secwait <= 0:
       errorExit("No blocks produced by node 1")

    time.sleep(0.6)
    retMap = node0.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, True)
    Print("sucessfully set new contract with new intrinsic!!!")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
