#!/usr/bin/env python3
import random
import os
import json
import time
import signal
import calendar
from datetime import datetime

from flask import Flask, request, jsonify
from flask_cors import CORS
from eth_hash.auto import keccak
import requests
import json

from binascii import unhexlify
from TestHarness import Cluster, TestHelper, Utils, WalletMgr
from TestHarness.TestHelper import AppArgs
from core_symbol import CORE_SYMBOL


###############################################################
# nodeos_trust_evm_fork_server
#
# Set up a TrustEVM env
#
# This test sets up 2 producing nodes and one "bridge" node using test_control_api_plugin.
#   One producing node has 3 of the elected producers and the other has 1 of the elected producers.
#   All the producers are named in alphabetical order, so that the 3 producers, in the one production node, are
#       scheduled first, followed by the 1 producer in the other producer node. Each producing node is only connected
#       to the other producing node via the "bridge" node.
#   The bridge node has the test_control_api_plugin, which exposes a restful interface that the test script uses to kill
#       the "bridge" node when /fork endpoint called.
#
# --trust-evm-contract-root should point to root of TrustEVM contract build dir
# --genesis-json file to save generated EVM genesis json
# --read-endpoint trustnode-rpc endpoint (read endpoint)
#
# Example:
#  ./tests/nodeos_trust_fork_server.py --trust-evm-contract-root ~/ext/TrustEVM/contract/build --leave-running
#
#  Launches wallet at port: 9899
#    Example: bin/cleos --wallet-url http://127.0.0.1:9899 ...
#
#  Sets up endpoint on port 5000
#    /            - for req['method'] == "eth_sendRawTransaction"
#    /fork        - create forked chain, does not return until a fork has started
#    /restore     - resolve fork and stabilize chain
#
# Dependencies:
#    pip install eth-hash requests flask flask-cors
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

appArgs=AppArgs()
appArgs.add(flag="--trust-evm-contract-root", type=str, help="TrustEVM contract build dir", default=None)
appArgs.add(flag="--genesis-json", type=str, help="File to save generated genesis json", default="trust-evm-genesis.json")
appArgs.add(flag="--read-endpoint", type=str, help="EVM read enpoint (trustevm-rpc)", default="http://localhost:8881")

args=TestHelper.parse_args({"--keep-logs","--dump-error-details","-v","--leave-running","--clean-run" }, applicationSpecificArgs=appArgs)
debug=args.v
killEosInstances= not args.leave_running
dumpErrorDetails=args.dump_error_details
keepLogs=args.keep_logs
killAll=args.clean_run
trustEvmContractRoot=args.trust_evm_contract_root
gensisJson=args.genesis_json
readEndpoint=args.read_endpoint

assert trustEvmContractRoot is not None, "--trust-evm-contract-root is required"

totalProducerNodes=2
totalNonProducerNodes=1
totalNodes=totalProducerNodes+totalNonProducerNodes
maxActiveProducers=21
totalProducers=maxActiveProducers

seed=1
Utils.Debug=debug
testSuccessful=False

random.seed(seed) # Use a fixed seed for repeatability.
cluster=Cluster(walletd=True)
walletMgr=WalletMgr(True)


try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    walletMgr.killall(allInstances=killAll)
    walletMgr.cleanup()

    # ***   setup topogrophy   ***

    # "bridge" shape connects defprocera through defproducerc (3 in node0) to each other and defproduceru (1 in node01)
    # and the only connection between those 2 groups is through the bridge node

    specificExtraNodeosArgs={}
    # Connect SHiP to node01 so it will switch forks as they are resolved
    specificExtraNodeosArgs[1]="--plugin eosio::state_history_plugin --state-history-endpoint 127.0.0.1:8999 --trace-history --chain-state-history --disable-replay-opts  "
    # producer nodes will be mapped to 0 through totalProducerNodes-1, so the number totalProducerNodes will be the non-producing node
    specificExtraNodeosArgs[totalProducerNodes]="--plugin eosio::test_control_api_plugin  "
    extraNodeosArgs="--contracts-console"

    Print("Stand up cluster")
    if cluster.launch(topo="bridge", pnodes=totalProducerNodes,
                      totalNodes=totalNodes, totalProducers=totalProducers,
                      useBiosBootFile=False, extraNodeosArgs=extraNodeosArgs, specificExtraNodeosArgs=specificExtraNodeosArgs) is False:
        Utils.cmdError("launcher")
        Utils.errorExit("Failed to stand up eos cluster.")

    Print("Validating system accounts after bootstrap")
    cluster.validateAccounts(None)

    Print ("Wait for Cluster stabilization")
    # wait for cluster to start producing blocks
    if not cluster.waitOnClusterBlockNumSync(3):
        errorExit("Cluster never stabilized")
    Print ("Cluster stabilized")

    prodNode = cluster.getNode(0)
    prodNode0 = prodNode
    prodNode1 = cluster.getNode(1)
    nonProdNode = cluster.getNode(2)

    accounts=cluster.createAccountKeys(6)
    if accounts is None:
        Utils.errorExit("FAILURE - create keys")

    evmAcc = accounts[0]
    evmAcc.name = "evmevmevmevm"
    evmAcc.activePrivateKey="5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
    evmAcc.activePublicKey="EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"
    accounts[1].name="tester111111" # needed for voting
    accounts[2].name="tester222222" # needed for voting
    accounts[3].name="tester333333" # needed for voting
    accounts[4].name="tester444444" # needed for voting
    accounts[5].name="tester555555" # needed for voting

    testWalletName="test"

    Print("Creating wallet \"%s\"." % (testWalletName))
    testWallet=walletMgr.create(testWalletName, [cluster.eosioAccount,accounts[0],accounts[1],accounts[2],accounts[3],accounts[4],accounts[5]])

    for _, account in cluster.defProducerAccounts.items():
        walletMgr.importKey(account, testWallet, ignoreDupKeyWarning=True)

    for i in range(0, totalNodes):
        node=cluster.getNode(i)
        node.producers=Cluster.parseProducers(i)
        numProducers=len(node.producers)
        for prod in node.producers:
            prodName = cluster.defProducerAccounts[prod].name
            if prodName == "defproducera" or prodName == "defproducerb" or prodName == "defproducerc" or prodName == "defproduceru":
                Print("Register producer %s" % cluster.defProducerAccounts[prod].name)
                trans=node.regproducer(cluster.defProducerAccounts[prod], "http::/mysite.com", 0, waitForTransBlock=False, exitOnError=True)

    # create accounts via eosio as otherwise a bid is needed
    for account in accounts:
        Print("Create new account %s via %s with private key: %s" % (account.name, cluster.eosioAccount.name, account.activePrivateKey))
        trans=nonProdNode.createInitializeAccount(account, cluster.eosioAccount, stakedDeposit=0, waitForTransBlock=True, stakeNet=10000, stakeCPU=10000, buyRAM=10000000, exitOnError=True)
        transferAmount="100000000.0000 {0}".format(CORE_SYMBOL)
        Print("Transfer funds %s from account %s to %s" % (transferAmount, cluster.eosioAccount.name, account.name))
        nonProdNode.transferFunds(cluster.eosioAccount, account, transferAmount, "test transfer", waitForTransBlock=True)
        trans=nonProdNode.delegatebw(account, 20000000.0000, 20000000.0000, waitForTransBlock=False, exitOnError=True)

    # ***   vote using accounts   ***

    cluster.waitOnClusterSync(blockAdvancing=3)

    # vote a,b,c  u
    voteProducers=[]
    voteProducers.append("defproducera")
    voteProducers.append("defproducerb")
    voteProducers.append("defproducerc")
    voteProducers.append("defproduceru")
    for account in accounts:
        Print("Account %s vote for producers=%s" % (account.name, voteProducers))
        trans=prodNode.vote(account, voteProducers, exitOnError=True, waitForTransBlock=False)

    #verify nodes are in sync and advancing
    cluster.waitOnClusterSync(blockAdvancing=3)
    Print("Shutdown unneeded bios node")
    cluster.biosNode.kill(signal.SIGTERM)

    # setup evm

    contractDir=trustEvmContractRoot + "/evm_runtime"
    wasmFile="evm_runtime.wasm"
    abiFile="evm_runtime.abi"
    Utils.Print("Publish evm_runtime contract")
    trans = prodNode.publishContract(evmAcc, contractDir, wasmFile, abiFile, waitForTransBlock=True)
    transId=prodNode.getTransId(trans)
    blockNum = prodNode.getBlockNumByTransId(transId)
    block = prodNode.getBlock(blockNum)
    Utils.Print("Block Id: ", block["id"])
    Utils.Print("Block timestamp: ", block["timestamp"])

    genesis_info = {
        "alloc": {},
        "coinbase": "0x0000000000000000000000000000000000000000",
        "config": {
            "chainId": 15555,
            "homesteadBlock": 0,
            "eip150Block": 0,
            "eip155Block": 0,
            "byzantiumBlock": 0,
            "constantinopleBlock": 0,
            "petersburgBlock": 0,
            "istanbulBlock": 0,
            "noproof": {}
        },
        "difficulty": "0x01",
        "extraData": "TrustEVM",
        "gasLimit": "0x7ffffffffff",
        "mixHash": "0x"+block["id"],
        "nonce": hex(1000),
        "timestamp": hex(int(calendar.timegm(datetime.strptime(block["timestamp"].split(".")[0], '%Y-%m-%dT%H:%M:%S').timetuple())))
    }

    # accounts: {
    #    mnemonic: "test test test test test test test test test test test junk",
    #    path: "m/44'/60'/0'/0",
    #    initialIndex: 0,
    #    count: 20,
    #    passphrase: "",
    # }

    addys = {
        "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266":"0x038318535b54105d4a7aae60c08fc45f9687181b4fdfc625bd1a753fa7397fed75,0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80",
        "0x70997970C51812dc3A010C7d01b50e0d17dc79C8":"0x02ba5734d8f7091719471e7f7ed6b9df170dc70cc661ca05e688601ad984f068b0,0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d",
        "0x3C44CdDdB6a900fa2b585dd299e03d12FA4293BC":"0x039d9031e97dd78ff8c15aa86939de9b1e791066a0224e331bc962a2099a7b1f04,0x5de4111afa1a4b94908f83103eb1f1706367c2e68ca870fc3fb9a804cdab365a",
        "0x90F79bf6EB2c4f870365E785982E1f101E93b906":"0x0220b871f3ced029e14472ec4ebc3c0448164942b123aa6af91a3386c1c403e0eb,0x7c852118294e51e653712a81e05800f419141751be58f605c371e15141b007a6",
        "0x15d34AAf54267DB7D7c367839AAf71A00a2C6A65":"0x03bf6ee64a8d2fdc551ec8bb9ef862ef6b4bcb1805cdc520c3aa5866c0575fd3b5,0x47e179ec197488593b187f80a00eb0da91f1b9d0b13f8733639f19c30a34926a",
        "0x9965507D1a55bcC2695C58ba16FB37d819B0A4dc":"0x0337b84de6947b243626cc8b977bb1f1632610614842468dfa8f35dcbbc55a515e,0x8b3a350cf5c34c9194ca85829a2df0ec3153be0318b5e2d3348e872092edffba",
        "0x976EA74026E726554dB657fA54763abd0C3a0aa9":"0x029a4ab212cb92775d227af4237c20b81f4221e9361d29007dfc16c79186b577cb,0x92db14e403b83dfe3df233f83dfa3a0d7096f21ca9b0d6d6b8d88b2b4ec1564e",
        "0x14dC79964da2C08b23698B3D3cc7Ca32193d9955":"0x0201f2bf1fa920e77a43c7aec2587d0b3814093420cc59a9b3ad66dd5734dda7be,0x4bbbf85ce3377467afe5d46f804f221813b2bb87f24d81f60f1fcdbf7cbf4356",
        "0x23618e81E3f5cdF7f54C3d65f7FBc0aBf5B21E8f":"0x03931e7fda8da226f799f791eefc9afebcd7ae2b1b19a03c5eaa8d72122d9fe74d,0xdbda1821b80551c9d65939329250298aa3472ba22feea921c0cf5d620ea67b97",
        "0xa0Ee7A142d267C1f36714E4a8F75612F20a79720":"0x023255458e24278e31d5940f304b16300fdff3f6efd3e2a030b5818310ac67af45,0x2a871d0798f97d79848a013d4936a73bf4cc922c825d33c1cf7073dff6d409c6",
        "0xBcd4042DE499D14e55001CcbB24a551F3b954096":"0x030bb316cf4dbaeff8df7c6a8f3c55a11c72f7f2f8d79c274f27cdce2220f36371,0xf214f2b2cd398c806f84e317254e0f0b801d0643303237d97a22a48e01628897",
        "0x71bE63f3384f5fb98995898A86B02Fb2426c5788":"0x02e393c954d127d79d56b594a46df6b2e053f49446759eac612dbe12ade3095c67,0x701b615bbdfb9de65240bc28bd21bbc0d996645a3dd57e7b12bc2bdf6f192c82",
        "0xFABB0ac9d68B0B445fB7357272Ff202C5651694a":"0x02463e7db0f9c35ba7ae68a8098f1024019b90191281276eeef294acb3f1354b0a,0xa267530f49f8280200edf313ee7af6b827f2a8bce2897751d06a843f644967b1",
        "0x1CBd3b2770909D4e10f157cABC84C7264073C9Ec":"0x037805be0fc5186c4437306b531c8e981d3922e5fc81d72d527931b995445fb78e,0x47c99abed3324a2707c28affff1267e45918ec8c3f20b8aa892e8b065d2942dd",
        "0xdF3e18d64BC6A983f673Ab319CCaE4f1a57C7097":"0x03407a862c69cbc66dceea40079757697abcf931043f5a5f128a56ae6e51bdbce7,0xc526ee95bf44d8fc405a158bb884d9d1238d99f0612e9f33d006bb0789009aaa",
        "0xcd3B766CCDd6AE721141F452C550Ca635964ce71":"0x02d5ab34891bfe989bfa557f983cb5d031c4acc845ad990baaefb58ca1db0e7716,0x8166f546bab6da521a8369cab06c5d2b9e46670292d85c875ee9ec20e84ffb61",
        "0x2546BcD3c84621e976D8185a91A922aE77ECEc30":"0x0364c1c85d9aa8081a8ef94c35379fa7532942b2d8cbbd1e3ea71c0e3609b96cc0,0xea6c44ac03bff858b476bba40716402b03e41b8e97e276d1baec7c37d42484a0",
        "0xbDA5747bFD65F08deb54cb465eB87D40e51B197E":"0x02c216848622dfc38a2ad2a921f524103cf654a22b8679736ecedc4901453ea3f7,0x689af8efa8c651a91ad287602527f3af2fe9f6501a7ac4b061667b5a93e037fd",
        "0xdD2FD4581271e230360230F9337D5c0430Bf44C0":"0x02302a94bd084ff317493db7c2fe07e0935c0f6d3e6772d6af3c58e92abebfb402,0xde9be858da4a475276426320d5e9262ecfc3ba460bfac56360bfa6c4c28b4ee0",
        "0x8626f6940E2eb28930eFb4CeF49B2d1F2C9C1199":"0x027bf824b28c4bf11ce553fa746a18754949ab4959e2ea73465778d14179211f8c,0xdf57089febbacf7ba0bc227dafbffa9fc08a93fdc68e1e42411a14efcf23656e"
    }

    for k in addys:
        trans = prodNode.pushMessage(evmAcc.name, "setbal", '{"addy":"' + k[2:].lower() + '", "bal":"0000000000000000000000000000000010000000000000000000000000000000"}', '-p evmevmevmevm')
        genesis_info["alloc"][k.lower()] = {"balance":"0x10000000000000000000000000000000"}
    prodNode.waitForTransBlockIfNeeded(trans[1], True)

    if gensisJson[0] != '/': gensisJson = os.path.realpath(gensisJson)
    f=open(gensisJson,"w")
    f.write(json.dumps(genesis_info))
    f.close()
    Utils.Print("#####################################################")
    Utils.Print("Generated EVM json genesis file in: %s" % gensisJson)
    Utils.Print("")
    Utils.Print("You can now run:")
    Utils.Print("  trustevm-node --plugin=blockchain_plugin --ship-endpoint=127.0.0.1:8999 --genesis-json=%s --chain-data=/tmp --verbosity=4" % gensisJson)
    Utils.Print("  trustevm-rpc --trust-evm-node=127.0.0.1:8080 --http-port=0.0.0.0:8881 --chaindata=/tmp --api-spec=eth,debug,net,trace")
    Utils.Print("")
    Utils.Print("Web3 endpoint:")
    Utils.Print("  http://localhost:5000")

    app = Flask(__name__)
    CORS(app)

    @app.route("/fork", methods=["POST"])
    def fork():
        Print("Sending command to kill bridge node to separate the 2 producer groups.")
        forkAtProducer="defproducera"
        prodNode1Prod="defproduceru"
        preKillBlockNum=nonProdNode.getBlockNum()
        preKillBlockProducer=nonProdNode.getBlockProducerByNum(preKillBlockNum)
        nonProdNode.killNodeOnProducer(producer=forkAtProducer, whereInSequence=1)
        Print("Current block producer %s fork will be at producer %s" % (preKillBlockProducer, forkAtProducer))
        prodNode0.waitForProducer(forkAtProducer)
        prodNode1.waitForProducer(prodNode1Prod)
        if nonProdNode.verifyAlive(): # if on defproducera, need to wait again
            prodNode0.waitForProducer(forkAtProducer)
            prodNode1.waitForProducer(prodNode1Prod)

        if nonProdNode.verifyAlive():
            Print("Bridge did not shutdown")
            return "Bridge did not shutdown"

        Print("Fork started")
        return "Fork started"

    @app.route("/restore", methods=["POST"])
    def restore():
        Print("Relaunching the non-producing bridge node to connect the producing nodes again")

        if nonProdNode.verifyAlive():
            return "bridge is already running"

        if not nonProdNode.relaunch():
            Utils.errorExit("Failure - (non-production) node %d should have restarted" % (nonProdNode.nodeNum))

        return "restored fork should resolve"

    @app.route("/", methods=["POST"])
    def default():
        def forward_request(req):
            if req['method'] == "eth_sendRawTransaction":
                actData = {"ram_payer":"evmevmevmevm", "rlptx":req['params'][0][2:]}
                prodNode.pushMessage(evmAcc.name, "pushtx", json.dumps(actData), '-p evmevmevmevm')
                return {
                    "id": req['id'],
                    "jsonrpc": "2.0",
                    "result": '0x'+keccak(unhexlify(req['params'][0][2:])).hex()
                }
            return requests.post(readEndpoint, json.dumps(req), headers={"Content-Type":"application/json"}).json()

        request_data = request.get_json()
        if type(request_data) == dict:
            return jsonify(forward_request(request_data))

        res = []
        for r in request_data:
            res.append(forward_request(r))

        return jsonify(res)

    app.run(host='0.0.0.0', port=5000)

finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, killEosInstances=killEosInstances, killWallet=killEosInstances, keepLogs=keepLogs, cleanRun=killAll, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
