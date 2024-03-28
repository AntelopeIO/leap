#!/usr/bin/env python3

import decimal
import re
import time
import json

from TestHarness import Cluster, Node, ReturnType, TestHelper, Utils, WalletMgr
from TestHarness.Cluster import PFSetupPolicy
from TestHarness.accounts import Account

###############################################################
# prod_preactivation_test
# --dump-error-details <Upon error print etc/eosio/node_*/config.ini and prod_preactivation_test<pid>/node_*/stderr.log to stdout>
# --keep-logs <Don't delete TestLogs/prod_preactivation_test<pid>/node_* folders upon test completion>
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit
cmdError=Utils.cmdError

args = TestHelper.parse_args({"--host","--port","--defproducera_prvt_key","--defproducerb_prvt_key"
                              ,"--dump-error-details","--dont-launch","--keep-logs","-v","--leave-running"
                              ,"--wallet-port","--unshared"})
server=args.host
port=args.port
debug=args.v
defproduceraPrvtKey=args.defproducera_prvt_key
defproducerbPrvtKey=args.defproducerb_prvt_key
dumpErrorDetails=args.dump_error_details
dontLaunch=args.dont_launch
prodCount=2
walletPort=args.wallet_port

Utils.Debug=debug
localTest=True
cluster=Cluster(host=server, port=port, defproduceraPrvtKey=defproduceraPrvtKey, defproducerbPrvtKey=defproducerbPrvtKey, unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True, port=walletPort)
testSuccessful=False

WalletdName=Utils.EosWalletName
ClientName="cleos"

try:
    TestHelper.printSystemInfo("BEGIN prod_preactivation_test.py")
    cluster.setWalletMgr(walletMgr)
    Print("SERVER: %s" % (server))
    Print("PORT: %d" % (port))

    if localTest and not dontLaunch:
        Print("Stand up cluster")
        if cluster.launch(pnodes=prodCount, totalNodes=prodCount, prodCount=1,
                          pfSetupPolicy=PFSetupPolicy.NONE, extraNodeosArgs=" --plugin eosio::producer_api_plugin  --http-max-response-time-ms 990000 ") is False:
            cmdError("launcher")
            errorExit("Failed to stand up eos cluster.")

    # setup producers using bios contract that does not need preactivate_feature
    contract="eosio.bios"
    contractDir="libraries/testing/contracts/old_versions/v1.6.0-rc3/%s" % (contract)
    wasmFile="%s.wasm" % (contract)
    abiFile="%s.abi" % (contract)
    retMap = cluster.biosNode.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True)
    if retMap is None:
        errorExit("publish of contract failed")

    producerKeys=cluster.parseClusterKeys(prodCount)

    eosioName="eosio"
    eosioKeys=producerKeys[eosioName]
    eosioAccount=Account(eosioName)
    eosioAccount.ownerPrivateKey=eosioKeys["private"]
    eosioAccount.ownerPublicKey=eosioKeys["public"]
    eosioAccount.activePrivateKey=eosioKeys["private"]
    eosioAccount.activePublicKey=eosioKeys["public"]

    Utils.Print("Creating accounts: %s " % ", ".join(producerKeys.keys()))
    producerKeys.pop(eosioName)
    accounts=[]
    for name, keys in producerKeys.items():
        initx = Account(name)
        initx.ownerPrivateKey=keys["private"]
        initx.ownerPublicKey=keys["public"]
        initx.activePrivateKey=keys["private"]
        initx.activePublicKey=keys["public"]
        trans=cluster.biosNode.createAccount(initx, eosioAccount, 0)
        if trans is None:
            errorExit("ERROR: Failed to create account %s" % (name))
        Node.validateTransaction(trans)
        accounts.append(initx)

    counts = dict.fromkeys(range(prodCount), 0)  # initialize node prods count to 0
    setProdsStr = '{"schedule": '
    prodStanzas = []
    prodNames = []
    for name, keys in list(producerKeys.items())[:21]:
        if counts[keys["node"]] >= prodCount:
            Utils.Print(f'Count for this node exceeded: {counts[keys["node"]]}')
            continue
        prodStanzas.append({'producer_name': keys['name'], 'block_signing_key': keys['public']})
        prodNames.append(keys["name"])
        counts[keys["node"]] += 1
    setProdsStr += json.dumps(prodStanzas)
    setProdsStr += ' }'
    if Utils.Debug: Utils.Print("setprods: %s" % (setProdsStr))
    Utils.Print("Setting producers: %s." % (", ".join(prodNames)))
    opts = "--permission eosio@active"
    # pylint: disable=redefined-variable-type
    trans = cluster.biosNode.pushMessage("eosio", "setprods", setProdsStr, opts)
    if trans is None or not trans[0]:
        errorExit("ERROR: Failed to set producer %s." % (keys["name"]))

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
    retMap = node0.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True, shouldFail=True)

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
    retMap = node0.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True, shouldFail=True)
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
    retMap = node0.publishContract(cluster.eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True, shouldFail=False)
    if retMap is None:
        errorExit("publish of new contract failed")
    Print("sucessfully set new contract with new intrinsic!!!")

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful, dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
