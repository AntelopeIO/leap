import copy
import subprocess
import time
import glob
import shutil
import os
import re
import string
import signal
import datetime
import sys
import random
import json
import socket
from pathlib import Path

from .core_symbol import CORE_SYMBOL
from .accounts import Account, createAccountKeys
from .testUtils import BlockLogAction
from .testUtils import Utils
from .Node import BlockType
from .Node import Node
from .WalletMgr import WalletMgr
from .TransactionGeneratorsLauncher import TransactionGeneratorsLauncher, TpsTrxGensConfig
from .launcher import cluster_generator
try:
    from .libc import unshare, CLONE_NEWNET
    from .interfaces import getInterfaceFlags, setInterfaceUp, IFF_LOOPBACK
except:
    pass

# Protocol Feature Setup Policy
class PFSetupPolicy:
    NONE = 0
    PREACTIVATE_FEATURE_ONLY = 1
    FULL = 2 # This will only happen if the cluster is bootstrapped (i.e. dontBootstrap == False)
    @staticmethod
    def hasPreactivateFeature(policy):
        return policy == PFSetupPolicy.PREACTIVATE_FEATURE_ONLY or \
                policy == PFSetupPolicy.FULL
    @staticmethod
    def isValid(policy):
        return policy == PFSetupPolicy.NONE or \
               policy == PFSetupPolicy.PREACTIVATE_FEATURE_ONLY or \
               policy == PFSetupPolicy.FULL

# pylint: disable=too-many-instance-attributes
# pylint: disable=too-many-public-methods
class Cluster(object):
    __chainSyncStrategies=Utils.getChainStrategies()
    __chainSyncStrategy=None
    __WalletName="MyWallet"
    __localHost="localhost"
    __BiosHost="localhost"
    __BiosPort=8788
    __LauncherCmdArr=[]
    __bootlog="leap-ignition-wd/bootlog.txt"

    # pylint: disable=too-many-arguments
    def __init__(self, localCluster=True, host="localhost", port=8888, walletHost="localhost", walletPort=9899
                 , defproduceraPrvtKey=None, defproducerbPrvtKey=None, staging=False, loggingLevel="debug", loggingLevelDict={}, nodeosVers="", unshared=False, keepRunning=False, keepLogs=False):
        """Cluster container.
        localCluster [True|False] Is cluster local to host.
        host: eos server host
        port: eos server port
        walletHost: eos wallet host
        walletPort: wos wallet port
        defproduceraPrvtKey: Defproducera account private key
        defproducerbPrvtKey: Defproducerb account private key
        staging: [True|False] If true, don't generate new node configurations
        loggingLevel: Logging level to apply to all nodeos loggers in all nodes
        loggingLevelDict: Dictionary of node indices and logging level to apply to all nodeos loggers in that node
        nodeosVers: Nodeos version string for compatibility
        unshared: [True|False] If true, launch all processes in Linux namespace
        keepRunning: [True|False] If true, leave nodes running when Cluster is destroyed. Implies keepLogs.
        keepLogs: [True|False] If true, retain log files after cluster shuts down.
        """
        self.accounts=[]
        self.nodes=[]
        self.unstartedNodes=[]
        self.localCluster=localCluster
        self.wallet=None
        self.walletMgr=None
        self.host=host
        self.port=port
        self.p2pBasePort=9876
        self.walletHost=walletHost
        self.walletPort=walletPort
        self.staging=staging
        self.loggingLevel=loggingLevel
        self.loggingLevelDict=loggingLevelDict
        self.keepRunning=keepRunning
        self.keepLogs=keepLogs or keepRunning
        # init accounts
        self.defProducerAccounts={}
        self.defproduceraAccount=self.defProducerAccounts["defproducera"]= Account("defproducera")
        self.defproducerbAccount=self.defProducerAccounts["defproducerb"]= Account("defproducerb")
        self.eosioAccount=self.defProducerAccounts["eosio"]= Account("eosio")

        self.defproduceraAccount.ownerPrivateKey=defproduceraPrvtKey
        self.defproduceraAccount.activePrivateKey=defproduceraPrvtKey
        self.defproducerbAccount.ownerPrivateKey=defproducerbPrvtKey
        self.defproducerbAccount.activePrivateKey=defproducerbPrvtKey

        self.trxGenLauncher=None
        self.preExistingFirstTrxFiles=[]

        self.filesToCleanup=[]
        self.testFailed=False
        self.alternateVersionLabels=Cluster.__defaultAlternateVersionLabels()
        self.biosNode = None
        self.nodeosVers=nodeosVers
        self.nodeosLogPath=Path(Utils.TestLogRoot) / Path(f'{Path(sys.argv[0]).stem}{os.getpid()}')

        self.libTestingContractsPath = Path(__file__).resolve().parents[2] / "libraries" / "testing" / "contracts"
        self.unittestsContractsPath = Path(__file__).resolve().parents[2] / "unittests" / "contracts"

        if unshared:
            unshare(CLONE_NEWNET)
            for index, name in socket.if_nameindex():
                if getInterfaceFlags(name) & IFF_LOOPBACK:
                    setInterfaceUp(name)

    def setChainStrategy(self, chainSyncStrategy=Utils.SyncReplayTag):
        self.__chainSyncStrategy=self.__chainSyncStrategies.get(chainSyncStrategy)
        if self.__chainSyncStrategy is None:
            self.__chainSyncStrategy=self.__chainSyncStrategies.get("none")

    def setWalletMgr(self, walletMgr):
        self.walletMgr=walletMgr

    @staticmethod
    def __defaultAlternateVersionLabels():
        """Return a labels dictionary with just the "current" label to path set."""
        labels={}
        labels["current"]="./"
        return labels

    def setAlternateVersionLabels(self, file):
        """From the provided file return a dictionary of labels to paths."""
        Utils.Print("alternate file=%s" % (file))
        self.alternateVersionLabels=Cluster.__defaultAlternateVersionLabels()
        if file is None:
            # only have "current"
            return
        if not os.path.exists(file):
            Utils.errorExit("Alternate Version Labels file \"%s\" does not exist" % (file))
        with open(file, 'r') as f:
            content=f.read()
            p=re.compile(r'^\s*(\w+)\s*=\s*([^\s](?:.*[^\s])?)\s*$', re.MULTILINE)
            all=p.findall(content)
            for match in all:
                label=match[0]
                path=match[1]
                if label=="current":
                    Utils.Print("ERROR: cannot overwrite default label %s with path=%s" % (label, path))
                    continue
                self.alternateVersionLabels[label]=path
                if Utils.Debug: Utils.Print("Version label \"%s\" maps to \"%s\"" % (label, path))

    # launch local nodes and set self.nodes
    # pylint: disable=too-many-locals
    # pylint: disable=too-many-return-statements
    # pylint: disable=too-many-branches
    # pylint: disable=too-many-statements
    def launch(self, pnodes=1, unstartedNodes=0, totalNodes=1, prodCount=21, topo="mesh", delay=2, onlyBios=False, dontBootstrap=False,
               totalProducers=None, sharedProducers=0, extraNodeosArgs="", specificExtraNodeosArgs=None, specificNodeosInstances=None, onlySetProds=False,
               pfSetupPolicy=PFSetupPolicy.FULL, alternateVersionLabelsFile=None, associatedNodeLabels=None, loadSystemContract=True, nodeosLogPath=Path(Utils.TestLogRoot) / Path(f'{Path(sys.argv[0]).stem}{os.getpid()}'), genesisPath=None,
               maximumP2pPerHost=0, maximumClients=25, prodsEnableTraceApi=True):
        """Launch cluster.
        pnodes: producer nodes count
        unstartedNodes: non-producer nodes that are configured into the launch, but not started.  Should be included in totalNodes.
        totalNodes: producer + non-producer nodes + unstarted non-producer nodes count
        prodCount: producers per producer node count
        topo: cluster topology (as defined by launcher, and "bridge" shape that is specific to this launch method)
        delay: delay between individual nodes launch (as defined by launcher)
          delay 0 exposes a bootstrap bug where producer handover may have a large gap confusing nodes and bringing system to a halt.
        onlyBios: When true, only loads the bios contract (and not more full bootstrapping).
        dontBootstrap: When true, don't do any bootstrapping at all. (even bios is not uploaded)
        extraNodeosArgs: string of arguments to pass through to each nodeos instance (via --nodeos flag on launcher)
        specificExtraNodeosArgs: dictionary of arguments to pass to a specific node (via --specific-num and
                                 --specific-nodeos flags on launcher), example: { "5" : "--plugin eosio::test_control_api_plugin" }
        specificNodeosInstances: dictionary of paths to launch specific nodeos binaries (via --spcfc-inst-num and
                                 --spcfc_inst_nodeos flags to launcher), example: { "4" : "bin/nodeos"}
        onlySetProds: Stop the bootstrap process after setting the producers
        pfSetupPolicy: determine the protocol feature setup policy (none, preactivate_feature_only, or full)
        alternateVersionLabelsFile: Supply an alternate version labels file to use with associatedNodeLabels.
        associatedNodeLabels: Supply a dictionary of node numbers to use an alternate label for a specific node.
        loadSystemContract: indicate whether the eosio.system contract should be loaded
        genesisPath: set the path to a specific genesis.json to use
        maximumP2pPerHost:  Maximum number of client nodes from any single IP address. Defaults to totalNodes if not set.
        maximumClients: Maximum number of clients from which connections are accepted, use 0 for no limit. Defaults to 25.
        prodsEnableTraceApi: Determines whether producer nodes should have eosio::trace_api_plugin enabled. Defaults to True.
        """
        assert(isinstance(topo, str))
        assert PFSetupPolicy.isValid(pfSetupPolicy)
        if alternateVersionLabelsFile is not None:
            assert(isinstance(alternateVersionLabelsFile, str))
        elif associatedNodeLabels is not None:
            associatedNodeLabels=None    # need to supply alternateVersionLabelsFile to use labels

        if associatedNodeLabels is not None:
            assert(isinstance(associatedNodeLabels, dict))
            Utils.Print("associatedNodeLabels size=%s" % (len(associatedNodeLabels)))
        Utils.Print("alternateVersionLabelsFile=%s" % (alternateVersionLabelsFile))

        if not self.localCluster:
            Utils.Print("WARNING: Cluster not local, not launching %s." % (Utils.EosServerName))
            return True

        if len(self.nodes) > 0:
            raise RuntimeError("Cluster already running.")

        if pnodes > totalNodes:
            raise RuntimeError("totalNodes (%d) must be equal to or greater than pnodes(%d)." % (totalNodes, pnodes))
        if pnodes + unstartedNodes > totalNodes:
            raise RuntimeError("totalNodes (%d) must be equal to or greater than pnodes(%d) + unstartedNodes(%d)." % (totalNodes, pnodes, unstartedNodes))

        if self.walletMgr is None:
            self.walletMgr=WalletMgr(True)

        producerFlag=""
        if totalProducers:
            assert(isinstance(totalProducers, (str,int)))
            producerFlag="--producers %s" % (totalProducers)

        if sharedProducers > 0:
            producerFlag += (" --shared-producers %d" % (sharedProducers))

        if maximumP2pPerHost <= 0:
            maximumP2pPerHost = totalNodes

        self.setAlternateVersionLabels(alternateVersionLabelsFile)

        tries = 30
        while not Utils.arePortsAvailable(set(range(self.port, self.port+totalNodes+1))):
            Utils.Print("ERROR: Another process is listening on nodeos default port. wait...")
            if tries == 0:
                return False
            tries = tries - 1
            time.sleep(2)
        loggingLevelDictString = json.dumps(self.loggingLevelDict, separators=(',', ':'))
        args=(f'-p {pnodes} -n {totalNodes} -d {delay} '
              f'-i {datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3]} -f {producerFlag} '
              f'--unstarted-nodes {unstartedNodes} --logging-level {self.loggingLevel} '
              f'--logging-level-map {loggingLevelDictString}')
        argsArr=args.split()
        argsArr.append("--config-dir")
        argsArr.append(str(nodeosLogPath))
        argsArr.append("--data-dir")
        argsArr.append(str(nodeosLogPath))
        if self.staging:
            argsArr.append("--nogen")
        nodeosArgs=""
        if "--max-transaction-time" not in extraNodeosArgs:
            nodeosArgs += " --max-transaction-time -1"
        if "--abi-serializer-max-time-ms" not in extraNodeosArgs:
            nodeosArgs += " --abi-serializer-max-time-ms 990000"
        if "--p2p-max-nodes-per-host" not in extraNodeosArgs:
            nodeosArgs += f" --p2p-max-nodes-per-host {maximumP2pPerHost}"
        if "--max-clients" not in extraNodeosArgs:
            nodeosArgs += f" --max-clients {maximumClients}"
        if Utils.Debug and "--contracts-console" not in extraNodeosArgs:
            nodeosArgs += " --contracts-console"
        if PFSetupPolicy.hasPreactivateFeature(pfSetupPolicy):
            nodeosArgs += " --plugin eosio::producer_api_plugin"
        if prodsEnableTraceApi:
            nodeosArgs += " --plugin eosio::trace_api_plugin "
        if extraNodeosArgs.find("--trace-rpc-abi") == -1:
            nodeosArgs += " --trace-no-abis "
        httpMaxResponseTimeSet = False
        if specificExtraNodeosArgs is not None:
            assert(isinstance(specificExtraNodeosArgs, dict))
            for nodeNum,arg in specificExtraNodeosArgs.items():
                assert(isinstance(nodeNum, (str,int)))
                assert(isinstance(arg, str))
                if not len(arg):
                    continue
                argsArr.append("--specific-num")
                argsArr.append(str(nodeNum))
                argsArr.append("--specific-nodeos")
                if arg.find("--http-max-response-time-ms") != -1:
                    httpMaxResponseTimeSet = True
                if arg[0] != "'" and arg[-1] != "'":
                    argsArr.append("'" + arg + "'")
                else:
                    argsArr.append(arg)
        if specificNodeosInstances is not None:
            assert(isinstance(specificNodeosInstances, dict))
            for nodeNum,arg in specificNodeosInstances.items():
                assert(isinstance(nodeNum, (str,int)))
                assert(isinstance(arg, str))
                argsArr.append("--spcfc-inst-num")
                argsArr.append(str(nodeNum))
                argsArr.append("--spcfc-inst-nodeos")
                argsArr.append(arg)

        if not httpMaxResponseTimeSet and extraNodeosArgs.find("--http-max-response-time-ms") == -1:
            extraNodeosArgs+=" --http-max-response-time-ms 990000 "

        if extraNodeosArgs is not None:
            assert(isinstance(extraNodeosArgs, str))
            nodeosArgs += extraNodeosArgs

        if nodeosArgs:
            argsArr.append("--nodeos")
            argsArr.append(nodeosArgs)

        if genesisPath is None:
            argsArr.append("--max-block-cpu-usage")
            argsArr.append(str(500000))
            argsArr.append("--max-transaction-cpu-usage")
            argsArr.append(str(475000))
        else:
            argsArr.append("--genesis")
            argsArr.append(str(genesisPath))

        if associatedNodeLabels is not None:
            for nodeNum,label in associatedNodeLabels.items():
                assert(isinstance(nodeNum, (str,int)))
                assert(isinstance(label, str))
                path=self.alternateVersionLabels.get(label)
                if path is None:
                    Utils.errorExit("associatedNodeLabels passed in indicates label %s for node num %s, but it was not identified in %s" % (label, nodeNum, alternateVersionLabelsFile))
                argsArr.append("--spcfc-inst-num")
                argsArr.append(str(nodeNum))
                argsArr.append("--spcfc-inst-nodeos")
                argsArr.append(path)

        # must be last cmdArr.append before subprocess.call, so that everything is on the command line
        # before constructing the shape.json file for "bridge"
        if topo=="bridge":
            shapeFilePrefix="shape_bridge"
            shapeFile=shapeFilePrefix+".json"
            cmdArrForOutput=copy.deepcopy(argsArr)
            cmdArrForOutput.append("--output")
            cmdArrForOutput.append(str(nodeosLogPath / shapeFile))
            cmdArrForOutput.append("--shape")
            cmdArrForOutput.append("line")
            s=" ".join(cmdArrForOutput)
            bridgeLauncher = cluster_generator(cmdArrForOutput)
            bridgeLauncher.define_network()
            bridgeLauncher.generate()

            Utils.Print(f"opening {topo} shape file: {nodeosLogPath / shapeFile}")
            f = open(nodeosLogPath / shapeFile, "r")
            shapeFileJsonStr = f.read()
            f.close()
            shapeFileObject = json.loads(shapeFileJsonStr)
            Utils.Print("shapeFileObject=%s" % (shapeFileObject))
            # retrieve the nodes, which is a map of node name to node definition, which the fc library prints out as
            # an array of array, the first level of arrays is the pair entries of the map, the second is an array
            # of two entries - [ <first>, <second> ] with first being the name and second being the node definition
            # also support Python launcher format, which is nested objects
            shapeFileNodes = shapeFileObject["nodes"]

            numProducers=totalProducers if totalProducers is not None else (totalNodes - unstartedNodes)
            maxProducers=ord('z')-ord('a')+1
            assert numProducers<maxProducers, \
                   "ERROR: topo of %s assumes names of \"defproducera\" to \"defproducerz\", so must have at most %d producers" % \
                    (topo,maxProducers)

            # will make a map to node object to make identification easier
            biosNodeObject=None
            bridgeNodes={}
            producerNodes={}
            producers=[]
            for append in range(ord('a'),ord('a')+numProducers):
                name="defproducer" + chr(append)
                producers.append(name)

            # first group starts at 0
            secondGroupStart=int((numProducers+1)/2)
            producerGroup1=[]
            producerGroup2=[]

            Utils.Print("producers=%s" % (producers))
            shapeFileNodeMap = {}
            def getNodeNum(nodeName):
                p=re.compile(r'^testnet_(\d+)$')
                m=p.match(nodeName)
                return int(m.group(1))

            for shapeFileNodePair in shapeFileNodes:
                if len(shapeFileNodePair) == 2:
                    nodeName=shapeFileNodePair[0]
                    shapeFileNode=shapeFileNodePair[1]
                elif type(shapeFileNodePair) == str:
                    nodeName=shapeFileNodePair
                    shapeFileNode=shapeFileNodes[shapeFileNodePair]
                shapeFileNodeMap[nodeName]=shapeFileNode
                Utils.Print("name=%s, shapeFileNode=%s" % (nodeName, shapeFileNodeMap[nodeName]))
                if nodeName=="bios":
                    biosNodeObject=shapeFileNode
                    continue
                nodeNum=getNodeNum(nodeName)
                Utils.Print("nodeNum=%d, shapeFileNode=%s" % (nodeNum, shapeFileNode))
                assert("producers" in shapeFileNode)
                shapeFileNodeProds=shapeFileNode["producers"]
                numNodeProducers=len(shapeFileNodeProds)
                if (numNodeProducers==0):
                    bridgeNodes[nodeName]=shapeFileNode
                else:
                    producerNodes[nodeName]=shapeFileNode
                    group=None
                    # go through all the producers for this node and determine which group on the bridged network they are in
                    for shapeFileNodeProd in shapeFileNodeProds:
                        producerIndex=0
                        for prod in producers:
                            if prod==shapeFileNodeProd:
                                break
                            producerIndex+=1

                        prodGroup=None
                        if producerIndex<secondGroupStart:
                            prodGroup=1
                            if group is None:
                                group=prodGroup
                                producerGroup1.append(nodeName)
                                Utils.Print("Group1 grouping producerIndex=%s, secondGroupStart=%s" % (producerIndex,secondGroupStart))
                        else:
                            prodGroup=2
                            if group is None:
                                group=prodGroup
                                producerGroup2.append(nodeName)
                                Utils.Print("Group2 grouping producerIndex=%s, secondGroupStart=%s" % (producerIndex,secondGroupStart))
                        if group!=prodGroup:
                            Utils.errorExit("Node configuration not consistent with \"bridge\" topology. Node %s has producers that fall into both halves of the bridged network" % (nodeName))

            for _,bridgeNode in bridgeNodes.items():
                bridgeNode["peers"]=[]
                for prodName in producerNodes:
                    bridgeNode["peers"].append(prodName)

            def connectGroup(group, producerNodes, bridgeNodes) :
                groupStr=""
                for nodeName in group:
                    groupStr+=nodeName+", "
                    prodNode=producerNodes[nodeName]
                    prodNode["peers"]=[i for i in group if i!=nodeName]
                    for bridgeName in bridgeNodes:
                        prodNode["peers"].append(bridgeName)

            connectGroup(producerGroup1, producerNodes, bridgeNodes)
            connectGroup(producerGroup2, producerNodes, bridgeNodes)

            f=open(shapeFile,"w")
            f.write(json.dumps(shapeFileObject, indent=4, sort_keys=True))
            f.close()

            argsArr.append("--shape")
            argsArr.append(shapeFile)
        else:
            argsArr.append("--shape")
            argsArr.append(topo)

        if type(specificExtraNodeosArgs) is dict:
            for args in specificExtraNodeosArgs.values():
                if "--plugin eosio::history_api_plugin" in args:
                    argsArr.append("--is-nodeos-v2")
                    break

        # Handle common case of specifying no block offset for older versions
        if "v2" in self.nodeosVers or "v3" in self.nodeosVers or "v4" in self.nodeosVers:
            argsArr = list(map(lambda st: str.replace(st, "--produce-block-offset-ms 0", "--last-block-time-offset-us 0 --last-block-cpu-effort-percent 100"), argsArr))

        Cluster.__LauncherCmdArr = argsArr.copy()

        launcher = cluster_generator(argsArr)
        launcher.define_network()
        launcher.generate()
        self.nodes = []
        for instance in launcher.network.nodes.values():
            eosdcmd = launcher.construct_command_line(instance)

            nodeNum = instance.index
            node = Node(self.host, self.port + nodeNum, nodeNum, Path(instance.data_dir_name),
                        Path(instance.config_dir_name), eosdcmd, unstarted=instance.dont_start,
                        launch_time=launcher.launch_time, walletMgr=self.walletMgr, nodeosVers=self.nodeosVers)
            if nodeNum == Node.biosNodeId:
                self.biosNode = node
            else:
                if node.popenProc:
                    self.nodes.append(node)
                else:
                    self.unstartedNodes.append(node)
            time.sleep(delay)

        self.startedNodesCount = totalNodes - unstartedNodes
        self.productionNodesCount = pnodes
        self.totalNodesCount = totalNodes

        if self.nodes is None or self.startedNodesCount != len(self.nodes):
            Utils.Print("ERROR: Unable to validate %s instances, expected: %d, actual: %d" %
                          (Utils.EosServerName, self.startedNodesCount, len(self.nodes)))
            return False

        if not self.biosNode or not Utils.waitForBool(self.biosNode.checkPulse, Utils.systemWaitTimeout):
            Utils.Print("ERROR: Bios node doesn't appear to be running...")
            return False

        if onlyBios:
            self.nodes=[self.biosNode]

        # ensure cluster node are inter-connected by ensuring everyone has block 1
        Utils.Print("Cluster viability smoke test. Validate every cluster node has block 1. ")
        if not self.waitOnClusterBlockNumSync(1):
            Utils.Print("ERROR: Cluster doesn't seem to be in sync. Some nodes missing block 1")
            return False

        if PFSetupPolicy.hasPreactivateFeature(pfSetupPolicy):
            Utils.Print("Activate Preactivate Feature.")
            self.biosNode.activatePreactivateFeature()

        if dontBootstrap:
            Utils.Print("Skipping bootstrap.")
            return True

        Utils.Print("Bootstrap cluster.")
        if not self.bootstrap(self.biosNode, self.startedNodesCount, prodCount + sharedProducers, totalProducers, pfSetupPolicy, onlyBios, onlySetProds, loadSystemContract):
            Utils.Print("ERROR: Bootstrap failed.")
            return False

        # validate iniX accounts can be retrieved

        producerKeys=Cluster.parseClusterKeys(totalNodes)
        if producerKeys is None:
            Utils.Print("ERROR: Unable to parse cluster info")
            return False

        def initAccountKeys(account, keys):
            account.ownerPrivateKey=keys["private"]
            account.ownerPublicKey=keys["public"]
            account.activePrivateKey=keys["private"]
            account.activePublicKey=keys["public"]

        for name,_ in producerKeys.items():
            account=Account(name)
            initAccountKeys(account, producerKeys[name])
            self.defProducerAccounts[name] = account

        self.eosioAccount=self.defProducerAccounts["eosio"]
        self.defproduceraAccount=self.defProducerAccounts["defproducera"]
        self.defproducerbAccount=self.defProducerAccounts["defproducerb"]

        return True

    # Initialize the default nodes (at present just the root node)
    def initializeNodes(self, defproduceraPrvtKey=None, defproducerbPrvtKey=None, onlyBios=False):
        port=Cluster.__BiosPort if onlyBios else self.port
        host=Cluster.__BiosHost if onlyBios else self.host
        nodeNum="bios" if onlyBios else 0
        node=Node(host, port, nodeNum, walletMgr=self.walletMgr, nodeosVers=self.nodeosVers)
        if Utils.Debug: Utils.Print("Node: %s", str(node))

        node.checkPulse(exitOnError=True)
        self.nodes=[node]

        if defproduceraPrvtKey is not None:
            self.defproduceraAccount.ownerPrivateKey=defproduceraPrvtKey
            self.defproduceraAccount.activePrivateKey=defproduceraPrvtKey

        if defproducerbPrvtKey is not None:
            self.defproducerbAccount.ownerPrivateKey=defproducerbPrvtKey
            self.defproducerbAccount.activePrivateKey=defproducerbPrvtKey

        return True

    # Initialize nodes from the Json nodes string
    def initializeNodesFromJson(self, nodesJsonStr):
        nodesObj= json.loads(nodesJsonStr)
        if nodesObj is None:
            Utils.Print("ERROR: Invalid Json string.")
            return False

        if "keys" in nodesObj:
            keysMap=nodesObj["keys"]

            if "defproduceraPrivateKey" in keysMap:
                defproduceraPrivateKey=keysMap["defproduceraPrivateKey"]
                self.defproduceraAccount.ownerPrivateKey=defproduceraPrivateKey

            if "defproducerbPrivateKey" in keysMap:
                defproducerbPrivateKey=keysMap["defproducerbPrivateKey"]
                self.defproducerbAccount.ownerPrivateKey=defproducerbPrivateKey

        nArr=nodesObj["nodes"]
        nodes=[]
        for n in nArr:
            port=n["port"]
            host=n["host"]
            node=Node(host, port, nodeId=len(nodes), walletMgr=self.walletMgr, nodeosVers=self.nodeosVers)
            if Utils.Debug: Utils.Print("Node:", node)

            node.checkPulse(exitOnError=True)
            nodes.append(node)


        self.nodes=nodes
        return True

    def setNodes(self, nodes):
        """manually set nodes, alternative to explicit launch"""
        self.nodes=nodes

    def waitOnClusterSync(self, timeout=None, blockType=BlockType.head, blockAdvancing=0):
        """Get head or irrevercible block on node 0, then ensure that block (or that block plus the
           blockAdvancing) is present on every cluster node."""
        assert(self.nodes)
        assert(len(self.nodes) > 0)
        node=self.nodes[0]
        targetBlockNum=node.getBlockNum(blockType) #retrieve node 0's head or irrevercible block number
        targetBlockNum+=blockAdvancing
        if Utils.Debug:
            Utils.Print("%s block number on root node: %d" % (blockType.type, targetBlockNum))
        if targetBlockNum == -1:
            return False

        return self.waitOnClusterBlockNumSync(targetBlockNum, timeout, blockType=blockType)

    def waitOnClusterBlockNumSync(self, targetBlockNum, timeout=None, blockType=BlockType.head):
        """Wait for all nodes to have targetBlockNum finalized."""
        assert(self.nodes)

        def doNodesHaveBlockNum(nodes, targetBlockNum, blockType, printCount):
            ret=True
            for node in nodes:
                try:
                    if (not node.killed) and (not node.isBlockPresent(targetBlockNum, blockType=blockType)):
                        ret=False
                        break
                except (TypeError) as _:
                    # This can happen if client connects before server is listening
                    ret=False
                    break

            printCount+=1
            if Utils.Debug and not ret and printCount%5==0:
                blockNums=[]
                for i in range(0, len(nodes)):
                    blockNums.append(nodes[i].getBlockNum())
                Utils.Print("Cluster still not in sync, head blocks for nodes: [ %s ]" % (", ".join(blockNums)))
            return ret

        printCount=0
        lam = lambda: doNodesHaveBlockNum(self.nodes, targetBlockNum, blockType, printCount)
        ret=Utils.waitForBool(lam, timeout)
        return ret

    @staticmethod
    def getClientVersion(fullVersion=False):
        """Returns client version (string)"""
        p = re.compile(r'^v?(.+)\n$')
        try:
            cmd="%s version client" % (Utils.EosClientPath)
            if fullVersion: cmd="%s version full" % (Utils.EosClientPath)
            if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
            response=Utils.checkOutput(cmd.split())
            assert(response)
            assert(isinstance(response, str))
            if Utils.Debug: Utils.Print("response: <%s>" % (response))
            m=p.match(response)
            if m is None:
                Utils.Print("ERROR: client version regex mismatch")
                return None

            verStr=m.group(1)
            return verStr
        except subprocess.CalledProcessError as ex:
            msg=ex.stderr.decode("utf-8")
            Utils.Print("ERROR: Exception during client version query. %s" % (msg))
            raise

    # create account keys and import into wallet. Wallet initialization will be user responsibility
    # also imports defproducera and defproducerb accounts
    def populateWallet(self, accountsCount, wallet, accountNames: list=None, createProducerAccounts: bool=True):
        if accountsCount == 0 and (accountNames is None or len(accountNames) == 0):
            return True
        if self.walletMgr is None:
            Utils.Print("ERROR: WalletMgr hasn't been initialized.")
            return False

        if accountNames is not None:
            assert(len(accountNames) <= accountsCount)

        accounts=None
        if accountsCount > 0:
            Utils.Print ("Create account keys.")
            accounts = createAccountKeys(accountsCount)
            if accounts is None:
                Utils.Print("Account keys creation failed.")
                return False

        if createProducerAccounts:
            Utils.Print("Importing keys for account %s into wallet %s." % (self.defproduceraAccount.name, wallet.name))
            if not self.walletMgr.importKey(self.defproduceraAccount, wallet):
                Utils.Print("ERROR: Failed to import key for account %s" % (self.defproduceraAccount.name))
                return False

            Utils.Print("Importing keys for account %s into wallet %s." % (self.defproducerbAccount.name, wallet.name))
            if not self.walletMgr.importKey(self.defproducerbAccount, wallet):
                Utils.Print("ERROR: Failed to import key for account %s" % (self.defproducerbAccount.name))
                return False

        if accountNames is not None:
            for idx, name in enumerate(accountNames):
                accounts[idx].name =  name

        for account in accounts:
            Utils.Print("Importing keys for account %s into wallet %s." % (account.name, wallet.name))
            if not self.walletMgr.importKey(account, wallet):
                Utils.Print("ERROR: Failed to import key for account %s" % (account.name))
                return False
            self.accounts.append(account)

        return True

    def getNodeP2pPort(self, nodeId: int):
        return self.p2pBasePort + nodeId

    def getNode(self, nodeId=0, exitOnError=True):
        if exitOnError and nodeId >= len(self.nodes):
            Utils.cmdError("cluster never created node %d" % (nodeId))
            Utils.errorExit("Failed to retrieve node %d" % (nodeId))
        if exitOnError and self.nodes[nodeId] is None:
            Utils.cmdError("cluster has None value for node %d" % (nodeId))
            Utils.errorExit("Failed to retrieve node %d" % (nodeId))
        return self.nodes[nodeId]

    def getNodes(self):
        return self.nodes[:]

    def getAllNodes(self):
        nodes = []
        if self.biosNode is not None:
            nodes.append(self.biosNode)
        nodes += self.getNodes()
        return nodes

    def launchUnstarted(self, numToLaunch=1):
        assert(isinstance(numToLaunch, int))
        assert(numToLaunch>0)
        launchList=self.unstartedNodes[:numToLaunch]
        del self.unstartedNodes[:numToLaunch]
        for node in launchList:
            # the node number is indexed off of the started nodes list
            node.launchUnstarted()
            self.nodes.append(node)

    # Spread funds across accounts with transactions spread through cluster nodes.
    #  Validate transactions are synchronized on root node
    def spreadFunds(self, source, accounts, amount=1):
        assert(source)
        assert(isinstance(source, Account))
        assert(accounts)
        assert(isinstance(accounts, list))
        assert(len(accounts) > 0)
        Utils.Print("len(accounts): %d" % (len(accounts)))

        count=len(accounts)
        transferAmount=(count*amount)+amount
        transferAmountStr=Node.currencyIntToStr(transferAmount, CORE_SYMBOL)
        node=self.nodes[0]
        fromm=source
        to=accounts[0]
        Utils.Print("Transfer %s units from account %s to %s on eos server port %d" % (
            transferAmountStr, fromm.name, to.name, node.port))
        trans=node.transferFunds(fromm, to, transferAmountStr)
        transId=Node.getTransId(trans)
        if transId is None:
            return False

        if Utils.Debug: Utils.Print("Funds transfered on transaction id %s." % (transId))

        nextEosIdx=-1
        for i in range(0, count):
            account=accounts[i]
            nextInstanceFound=False
            for _ in range(0, count):
                #Utils.Print("nextEosIdx: %d, n: %d" % (nextEosIdx, n))
                nextEosIdx=(nextEosIdx + 1)%count
                if not self.nodes[nextEosIdx].killed:
                    #Utils.Print("nextEosIdx: %d" % (nextEosIdx))
                    nextInstanceFound=True
                    break

            if nextInstanceFound is False:
                Utils.Print("ERROR: No active nodes found.")
                return False

            #Utils.Print("nextEosIdx: %d, count: %d" % (nextEosIdx, count))
            node=self.nodes[nextEosIdx]
            if Utils.Debug: Utils.Print("Wait for transaction id %s on node port %d" % (transId, node.port))
            if node.waitForTransactionInBlock(transId) is False:
                Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, node.port))
                return False

            transferAmount -= amount
            transferAmountStr=Node.currencyIntToStr(transferAmount, CORE_SYMBOL)
            fromm=account
            to=accounts[i+1] if i < (count-1) else source
            Utils.Print("Transfer %s units from account %s to %s on eos server port %d." %
                    (transferAmountStr, fromm.name, to.name, node.port))

            trans=node.transferFunds(fromm, to, transferAmountStr)
            transId=Node.getTransId(trans)
            if transId is None:
                return False

            if Utils.Debug: Utils.Print("Funds transfered on block num %s." % (transId))

        # As an extra step wait for last transaction on the root node
        node=self.nodes[0]
        if Utils.Debug: Utils.Print("Wait for transaction id %s on node port %d" % (transId, node.port))
        if node.waitForTransactionInBlock(transId) is False:
            Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, node.port))
            return False

        return True

    def validateSpreadFunds(self, initialBalances, transferAmount, source, accounts):
        """Given initial Balances, will validate each account has the expected balance based upon transferAmount.
        This validation is repeated against every node in the cluster."""
        assert(source)
        assert(isinstance(source, Account))
        assert(accounts)
        assert(isinstance(accounts, list))
        assert(len(accounts) > 0)
        assert(initialBalances)
        assert(isinstance(initialBalances, dict))
        assert(isinstance(transferAmount, int))

        for node in self.nodes:
            if node.killed:
                continue

            if Utils.Debug: Utils.Print("Validate funds on %s server port %d." %
                                        (Utils.EosServerName, node.port))

            if node.validateFunds(initialBalances, transferAmount, source, accounts) is False:
                Utils.Print("ERROR: Failed to validate funds on eos node port: %d" % (node.port))
                return False

        return True

    def spreadFundsAndValidate(self, transferAmount=1):
        """Sprays 'transferAmount' funds across configured accounts and validates action. The spray is done in a trickle down fashion with account 1
        receiving transferAmount*n SYS and forwarding x-transferAmount funds. Transfer actions are spread round-robin across the cluster to vaidate system cohesiveness."""

        if Utils.Debug: Utils.Print("Get initial system balances.")
        initialBalances=self.nodes[0].getEosBalances([self.defproduceraAccount] + self.accounts)
        assert(initialBalances)
        assert(isinstance(initialBalances, dict))

        if False == self.spreadFunds(self.defproduceraAccount, self.accounts, transferAmount):
            Utils.Print("ERROR: Failed to spread funds across nodes.")
            return False

        Utils.Print("Funds spread across all accounts. Now validate funds")

        if False == self.validateSpreadFunds(initialBalances, transferAmount, self.defproduceraAccount, self.accounts):
            Utils.Print("ERROR: Failed to validate funds transfer across nodes.")
            return False

        return True

    def validateAccounts(self, accounts, testSysAccounts=True):
        assert(len(self.nodes) > 0)
        node=self.nodes[0]

        myAccounts = []
        if testSysAccounts:
            myAccounts += [self.eosioAccount, self.defproduceraAccount, self.defproducerbAccount]
        if accounts:
            assert(isinstance(accounts, list))
            myAccounts += accounts

        node.validateAccounts(myAccounts)

    def createAccountAndVerify(self, account, creator, stakedDeposit=1000, stakeNet=100, stakeCPU=100, buyRAM=10000, validationNodeIndex=-1):
        """create account, verify account and return transaction id"""
        node=self.nodes[validationNodeIndex]
        waitViaRetry =  self.totalNodesCount > self.productionNodesCount
        trans=node.createInitializeAccount(account, creator, stakedDeposit, waitForTransBlock=waitViaRetry, stakeNet=stakeNet, stakeCPU=stakeCPU, buyRAM=buyRAM, exitOnError=True)
        if not waitViaRetry:
            node.waitForTransBlockIfNeeded(trans, True, exitOnError=True)
        assert(node.verifyAccount(account))
        return trans

    # # create account, verify account and return transaction id
    # def createAccountAndVerify(self, account, creator, stakedDeposit=1000):
    #     if len(self.nodes) == 0:
    #         Utils.Print("ERROR: No nodes initialized.")
    #         return None
    #     node=self.nodes[0]

    #     transId=node.createAccount(account, creator, stakedDeposit)

    #     if transId is not None and node.verifyAccount(account) is not None:
    #         return transId
    #     return None

    def createInitializeAccount(self, account, creatorAccount, stakedDeposit=1000, waitForTransBlock=False, stakeNet=100, stakeCPU=100, buyRAM=10000, exitOnError=False):
        assert(len(self.nodes) > 0)
        node=self.nodes[0]
        trans=node.createInitializeAccount(account, creatorAccount, stakedDeposit, waitForTransBlock, stakeNet=stakeNet, stakeCPU=stakeCPU, buyRAM=buyRAM)
        return trans

    @staticmethod
    def nodeNameToId(name):
        r"""Convert node name to decimal id. Node name regex is "node_([\d]+)". "node_bios" is a special name which returns -1. Examples: node_00 => 0, node_21 => 21, node_bios => -1. """
        if name == "node_bios":
            return -1

        m=re.search(r"node_([\d]+)", name)
        return int(m.group(1))

    @staticmethod
    def parseProducerKeys(startFile, nodeName):
        """Parse node start file for producer keys. Returns dictionary. (Keys: account name; Values: dictionary objects (Keys: ["name", "node", "private","public"]; Values: account name, node id returned by nodeNameToId(nodeName), private key(string)and public key(string)))."""

        startStr=None
        with open(startFile, 'r') as f:
            startStr=f.read()

        pattern=r"\s*--signature-provider\s*(\w+)=KEY:(\w+)"
        m=re.search(pattern, startStr)
        regMsg="None" if m is None else "NOT None"
        if m is None:
            if Utils.Debug: Utils.Print(f'No producer keys found in node {nodeName}')
            return None

        pubKey=m.group(1)
        privateKey=m.group(2)

        pattern=r"\s*--producer-name\s*\W*(\w+)"
        matches=re.findall(pattern, startStr)
        if matches is None:
            if Utils.Debug: Utils.Print("Failed to find producers.")
            return None

        producerKeys={}
        for m in matches:
            if Utils.Debug: Utils.Print ("Found producer : %s" % (m))
            nodeId=Cluster.nodeNameToId(nodeName)
            keys={"name": m, "node": nodeId, "private": privateKey, "public": pubKey}
            producerKeys[m]=keys

        return producerKeys

    @staticmethod
    def parseProducers(nodeNum):
        """Parse node start file for producers."""

        startCmd=Utils.getNodeDataDir(nodeNum, "start.cmd")
        if Utils.Debug: Utils.Print(f'Parsing file {startCmd}')
        configStr=None
        with open(startCmd, 'r') as f:
            configStr=f.read()

        pattern=r"\s*--producer-name\s*\W*(\w+)"
        producerMatches=re.findall(pattern, configStr)
        if producerMatches is None:
            if Utils.Debug: Utils.Print(f'No producers found in node_{nodeNum}.')
            return None
        if Utils.Debug: Utils.Print(f'Found producers {producerMatches}')
        return producerMatches

    @staticmethod
    def parseClusterKeys(totalNodes):
        """Parse cluster start files. Updates producer keys data members."""

        startFile=Utils.getNodeDataDir("bios", "start.cmd")
        if Utils.Debug: Utils.Print("Parsing file %s" % startFile)
        nodeName=Utils.nodeExtensionToName("bios")
        producerKeys=Cluster.parseProducerKeys(startFile, nodeName)
        if producerKeys is None:
            Utils.Print("ERROR: Failed to parse eosio private keys from cluster start files.")
            return None

        for i in range(0, totalNodes):
            startFile=Utils.getNodeDataDir(i, "start.cmd")
            if Utils.Debug: Utils.Print("Parsing file %s" % startFile)

            nodeName=Utils.nodeExtensionToName(i)
            keys=Cluster.parseProducerKeys(startFile, nodeName)
            if keys is not None:
                producerKeys.update(keys)
        Utils.Print(f'Found {len(producerKeys)} producer keys')
        return producerKeys

    def bootstrap(self, biosNode, totalNodes, prodCount, totalProducers, pfSetupPolicy, onlyBios=False, onlySetProds=False, loadSystemContract=True):
        """Create 'prodCount' init accounts and deposits 10000000000 SYS in each. If prodCount is -1 will initialize all possible producers.
        Ensure nodes are inter-connected prior to this call. One way to validate this will be to check if every node has block 1."""

        Utils.Print(f'Starting cluster bootstrap of {prodCount} producers.')
        assert PFSetupPolicy.isValid(pfSetupPolicy)
        if totalProducers is None:
            totalProducers=totalNodes

        producerKeys=Cluster.parseClusterKeys(totalNodes)
        # should have totalNodes node plus bios node
        if producerKeys is None:
            Utils.Print("ERROR: Failed to parse any producer keys from start files.")
            return None
        elif len(producerKeys) < (totalProducers+1):
            Utils.Print("ERROR: Failed to parse %d producer keys from cluster start files, only found %d." % (totalProducers+1,len(producerKeys)))
            return None

        if not self.walletMgr.launch():
            Utils.Print("ERROR: Failed to launch bootstrap wallet.")
            return None

        ignWallet=self.walletMgr.create("ignition")

        eosioName="eosio"
        eosioKeys=producerKeys[eosioName]
        eosioAccount=Account(eosioName)
        eosioAccount.ownerPrivateKey=eosioKeys["private"]
        eosioAccount.ownerPublicKey=eosioKeys["public"]
        eosioAccount.activePrivateKey=eosioKeys["private"]
        eosioAccount.activePublicKey=eosioKeys["public"]

        if not self.walletMgr.importKey(eosioAccount, ignWallet):
            Utils.Print("ERROR: Failed to import %s account keys into ignition wallet." % (eosioName))
            return None

        contract="eosio.bios"
        contractDir= str(self.libTestingContractsPath / contract)
        if PFSetupPolicy.hasPreactivateFeature(pfSetupPolicy):
            contractDir=str(self.libTestingContractsPath / "old_versions" / "v1.7.0-develop-preactivate_feature" / contract)
        else:
            contractDir=str(self.libTestingContractsPath / "old_versions" / "v1.6.0-rc3" / contract)
        wasmFile="%s.wasm" % (contract)
        abiFile="%s.abi" % (contract)
        Utils.Print("Publish %s contract" % (contract))
        trans=biosNode.publishContract(eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True)
        if trans is None:
            Utils.Print("ERROR: Failed to publish contract %s." % (contract))
            return None

        if pfSetupPolicy == PFSetupPolicy.FULL:
            biosNode.preactivateAllBuiltinProtocolFeature()

        Node.validateTransaction(trans)

        Utils.Print("Creating accounts: %s " % ", ".join(producerKeys.keys()))
        producerKeys.pop(eosioName)
        accounts=[]
        for name, keys in producerKeys.items():
            initx = Account(name)
            initx.ownerPrivateKey=keys["private"]
            initx.ownerPublicKey=keys["public"]
            initx.activePrivateKey=keys["private"]
            initx.activePublicKey=keys["public"]
            trans=biosNode.createAccount(initx, eosioAccount, 0)
            if trans is None:
                Utils.Print("ERROR: Failed to create account %s" % (name))
                return None
            Node.validateTransaction(trans)
            accounts.append(initx)

        transId=Node.getTransId(trans)
        if not biosNode.waitForTransactionInBlock(transId):
            Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, biosNode.port))
            return None

        Utils.Print("Validating system accounts within bootstrap")
        biosNode.validateAccounts(accounts)

        if not onlyBios:
            if prodCount == -1:
                setProdsFile="setprods.json"
                if Utils.Debug: Utils.Print("Reading in setprods file %s." % (setProdsFile))
                with open(setProdsFile, "r") as f:
                    setProdsStr=f.read()

                    Utils.Print("Setting producers.")
                    opts="--permission eosio@active"
                    myTrans=biosNode.pushMessage("eosio", "setprods", setProdsStr, opts)
                    if myTrans is None or not myTrans[0]:
                        Utils.Print("ERROR: Failed to set producers.")
                        return None
            else:
                counts=dict.fromkeys(range(totalNodes), 0) #initialize node prods count to 0
                setProdsStr='{"schedule": '
                prodStanzas=[]
                prodNames=[]
                for name, keys in list(producerKeys.items())[:21]:
                    if counts[keys["node"]] >= prodCount:
                        Utils.Print(f'Count for this node exceeded: {counts[keys["node"]]}')
                        continue
                    prodStanzas.append({ 'producer_name': keys['name'], 'block_signing_key': keys['public'] })
                    prodNames.append(keys["name"])
                    counts[keys["node"]] += 1
                setProdsStr += json.dumps(prodStanzas)
                setProdsStr += ' }'
                if Utils.Debug: Utils.Print("setprods: %s" % (setProdsStr))
                Utils.Print("Setting producers: %s." % (", ".join(prodNames)))
                opts="--permission eosio@active"
                # pylint: disable=redefined-variable-type
                trans=biosNode.pushMessage("eosio", "setprods", setProdsStr, opts)
                if trans is None or not trans[0]:
                    Utils.Print("ERROR: Failed to set producer %s." % (keys["name"]))
                    return None

            trans=trans[1]
            transId=Node.getTransId(trans)
            if not biosNode.waitForTransactionInBlock(transId):
                Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, biosNode.port))
                return None

            # wait for block production handover (essentially a block produced by anyone but eosio).
            lam = lambda: biosNode.getInfo(exitOnError=True)["head_block_producer"] != "eosio"
            ret=Utils.waitForBool(lam)
            if not ret:
                Utils.Print("ERROR: Block production handover failed.")
                return None

        if onlySetProds: return biosNode

        def createSystemAccount(accountName):
            newAccount = copy.deepcopy(eosioAccount)
            newAccount.name = accountName
            trans=biosNode.createAccount(newAccount, eosioAccount, 0)
            if trans is None:
                Utils.Print(f'ERROR: Failed to create account {newAccount.name}')
                return None
            return trans

        systemAccounts = ['eosio.bpay', 'eosio.msig', 'eosio.names', 'eosio.ram', 'eosio.ramfee', 'eosio.saving', 'eosio.stake', 'eosio.token', 'eosio.vpay', 'eosio.wrap']
        acctTrans = list(map(createSystemAccount, systemAccounts))

        for trans in acctTrans:
            Node.validateTransaction(trans)

        transIds = list(map(Node.getTransId, acctTrans))
        if not biosNode.waitForTransactionsInBlock(transIds):
            Utils.Print('ERROR: Failed to validate creation of system accounts')
            return None

        eosioTokenAccount = copy.deepcopy(eosioAccount)
        eosioTokenAccount.name = 'eosio.token'
        contract="eosio.token"
        contractDir=str(self.unittestsContractsPath / contract)
        wasmFile="%s.wasm" % (contract)
        abiFile="%s.abi" % (contract)
        Utils.Print("Publish %s contract" % (contract))
        trans=biosNode.publishContract(eosioTokenAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True)
        if trans is None:
            Utils.Print("ERROR: Failed to publish contract %s." % (contract))
            return None

        # Create currency0000, followed by issue currency0000
        contract=eosioTokenAccount.name
        Utils.Print("push create action to %s contract" % (contract))
        action="create"
        data="{\"issuer\":\"%s\",\"maximum_supply\":\"1000000000.0000 %s\"}" % (eosioAccount.name, CORE_SYMBOL)
        opts="--permission %s@active" % (contract)
        trans=biosNode.pushMessage(contract, action, data, opts)
        if trans is None or not trans[0]:
            Utils.Print("ERROR: Failed to push create action to eosio contract.")
            return None

        Node.validateTransaction(trans[1])
        transId=Node.getTransId(trans[1])
        if not biosNode.waitForTransactionInBlock(transId):
            Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, biosNode.port))
            return None

        contract=eosioTokenAccount.name
        Utils.Print("push issue action to %s contract" % (contract))
        action="issue"
        data="{\"to\":\"%s\",\"quantity\":\"1000000000.0000 %s\",\"memo\":\"initial issue\"}" % (eosioAccount.name, CORE_SYMBOL)
        opts="--permission %s@active" % (eosioAccount.name)
        trans=biosNode.pushMessage(contract, action, data, opts)
        if trans is None or not trans[0]:
            Utils.Print("ERROR: Failed to push issue action to eosio contract.")
            return None

        Node.validateTransaction(trans[1])
        Utils.Print("Wait for issue action transaction to appear in a block.")
        transId=Node.getTransId(trans[1])
        biosNode.waitForTransactionInBlock(transId)

        expectedAmount="1000000000.0000 {0}".format(CORE_SYMBOL)
        Utils.Print("Verify eosio issue, Expected: %s" % (expectedAmount))
        actualAmount=biosNode.getAccountEosBalanceStr(eosioAccount.name)
        if expectedAmount != actualAmount:
            Utils.Print("ERROR: Issue verification failed. Excepted %s, actual: %s" %
                        (expectedAmount, actualAmount))
            return None

        if loadSystemContract:
            contract="eosio.system"
            contractDir=str(self.unittestsContractsPath / contract)
            wasmFile="%s.wasm" % (contract)
            abiFile="%s.abi" % (contract)
            Utils.Print("Publish %s contract" % (contract))
            trans=biosNode.publishContract(eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True)
            if trans is None:
                Utils.Print("ERROR: Failed to publish contract %s." % (contract))
                return None

            Node.validateTransaction(trans)

        initialFunds="1000000.0000 {0}".format(CORE_SYMBOL)
        Utils.Print("Transfer initial fund %s to individual accounts." % (initialFunds))
        trans=None
        contract=eosioTokenAccount.name
        action="transfer"
        for name, keys in producerKeys.items():
            data="{\"from\":\"%s\",\"to\":\"%s\",\"quantity\":\"%s\",\"memo\":\"%s\"}" % (eosioAccount.name, name, initialFunds, "init transfer")
            opts="--permission %s@active" % (eosioAccount.name)
            trans=biosNode.pushMessage(contract, action, data, opts)
            if trans is None or not trans[0]:
                Utils.Print("ERROR: Failed to transfer funds from %s to %s." % (eosioTokenAccount.name, name))
                return None

            Node.validateTransaction(trans[1])

        Utils.Print("Wait for last transfer transaction to appear in a block.")
        transId=Node.getTransId(trans[1])
        if not biosNode.waitForTransactionInBlock(transId):
            Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, biosNode.port))
            return None

        # Only call init if the system contract is loaded
        if loadSystemContract:
            action="init"
            data="{\"version\":0,\"core\":\"4,%s\"}" % (CORE_SYMBOL)
            opts="--permission %s@active" % (eosioAccount.name)
            trans=biosNode.pushMessage(eosioAccount.name, action, data, opts)
            transId=Node.getTransId(trans[1])
            Utils.Print("Wait for system init transaction to be in a block.")
            if not biosNode.waitForTransactionInBlock(transId):
                Utils.Print("ERROR: Failed to validate transaction %s in block on server port %d." % (transId, biosNode.port))
                return None

        Utils.Print("Cluster bootstrap done.")

        return True

    @staticmethod
    def pgrepEosServers(timeout=None):
        cmd=Utils.pgrepCmd(Utils.EosServerName)

        def myFunc():
            psOut=None
            try:
                if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
                psOut=Utils.checkOutput(cmd.split())
                return psOut
            except subprocess.CalledProcessError as ex:
                msg=ex.stderr.decode("utf-8")
                Utils.Print("ERROR: call of \"%s\" failed. %s" % (cmd, msg))
                return None
            return None

        return Utils.waitForObj(myFunc, timeout)

    # Kills a percentange of Eos instances starting from the tail and update eosInstanceInfos state
    def killSomeEosInstances(self, killCount, killSignalStr=Utils.SigKillTag):
        killSignal=signal.SIGKILL
        if killSignalStr == Utils.SigTermTag:
            killSignal=signal.SIGTERM
        Utils.Print("Kill %d %s instances with signal %s." % (killCount, Utils.EosServerName, killSignal))

        killedCount=0
        for node in reversed(self.nodes):
            if not node.kill(killSignal):
                return False

            killedCount += 1
            if killedCount >= killCount:
                break

        time.sleep(1) # Give processes time to stand down
        return True

    def relaunchEosInstances(self, nodeArgs="", waitForTerm=False):

        chainArg=self.__chainSyncStrategy.arg + " " + nodeArgs

        newChain= False if self.__chainSyncStrategy.name in [Utils.SyncHardReplayTag, Utils.SyncNoneTag] else True
        for i in range(0, len(self.nodes)):
            node=self.nodes[i]
            if node.killed and not node.relaunch(chainArg, newChain=newChain, waitForTerm=waitForTerm):
                return False

        return True

    @staticmethod
    def dumpErrorDetailImpl(fileName):
        Utils.Print(Utils.FileDivider)
        Utils.Print("Contents of %s:" % (fileName))
        if os.path.exists(fileName):
            with open(fileName, "r") as f:
                shutil.copyfileobj(f, sys.stdout)
        else:
            Utils.Print("File %s not found." % (fileName))

    def dumpErrorDetails(self):
        fileName=Utils.getNodeConfigDir("bios", "config.ini")
        Cluster.dumpErrorDetailImpl(fileName)
        path=Utils.getNodeDataDir("bios")
        fileNames=Node.findStderrFiles(path)
        for fileName in fileNames:
            Cluster.dumpErrorDetailImpl(fileName)

        for i in range(0, len(self.nodes)):
            configLocation=Utils.getNodeConfigDir(i)
            fileName=os.path.join(configLocation, "config.ini")
            Cluster.dumpErrorDetailImpl(fileName)
            fileName=os.path.join(configLocation, "genesis.json")
            Cluster.dumpErrorDetailImpl(fileName)
            path=Utils.getNodeDataDir(i)
            fileNames=Node.findStderrFiles(path)
            for fileName in fileNames:
                Cluster.dumpErrorDetailImpl(fileName)

    def shutdown(self):
        """Shut down all nodeos instances launched by this Cluster."""
        if not self.keepRunning:
            Utils.Print('Cluster shutting down.')
            for node in self.nodes:
                node.kill(signal.SIGTERM)
            if len(self.nodes) and self.biosNode != self.nodes[0]:
                self.biosNode.kill(signal.SIGTERM)
        else:
            Utils.Print('Cluster left running.')

        # Make sure to cleanup all trx generators that may have been started and still generating trxs
        if self.trxGenLauncher is not None:
            self.trxGenLauncher.killAll()

        self.cleanup()

    def bounce(self, nodes, silent=True):
        """Bounces nodeos instances as indicated by parameter nodes.
        nodes should take the form of a comma-separated list as accepted by the launcher --bounce command (e.g. '00' or '00,01')"""
        cmdArr = Cluster.__LauncherCmdArr.copy()
        cmdArr.append("--bounce")
        cmdArr.append(nodes)
        cmd=" ".join(cmdArr)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
        if 0 != subprocess.call(cmdArr):
            if not silent: Utils.Print("Launcher failed to bounce nodes: %s." % (nodes))
            return False
        return True

    def down(self, nodes, silent=True):
        """Brings down nodeos instances as indicated by parameter nodes.
        nodes should take the form of a comma-separated list as accepted by the launcher --bounce command (e.g. '00' or '00,01')"""
        cmdArr = Cluster.__LauncherCmdArr.copy()
        cmdArr.append("--down")
        cmdArr.append(nodes)
        cmd=" ".join(cmdArr)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
        if 0 != subprocess.call(cmdArr):
            if not silent: Utils.Print("Launcher failed to take down nodes: %s." % (nodes))
            return False
        return True

    def waitForNextBlock(self, timeout=None):
        if timeout is None:
            timeout=Utils.systemWaitTimeout
        node=self.nodes[0]
        return node.waitForNextBlock(timeout)

    def cleanup(self):
        if self.keepRunning or self.keepLogs or self.testFailed:
            return
        for f in glob.glob(Utils.DataPath):
            shutil.rmtree(f, ignore_errors=True)
        for f in glob.glob(Utils.ConfigDir + "node_*"):
            shutil.rmtree(f, ignore_errors=True)

        # Cleanup transaction generator files
        for f in glob.glob(f"{Utils.DataDir}/trx_data_output_*.txt"):
            os.remove(f)
        for f in glob.glob(f"{Utils.DataDir}/first_trx_*.txt"):
            os.remove(f)

        for f in self.filesToCleanup:
            os.remove(f)

    # Create accounts, if account does not already exist, and validates that the last transaction is received on root node
    def createAccounts(self, creator, waitForTransBlock=True, stakedDeposit=1000, validationNodeIndex=-1):
        if self.accounts is None:
            return True
        transId=None
        for account in self.accounts:
            ret = self.biosNode.getEosAccount(account.name)
            if ret is None:
                if Utils.Debug: Utils.Print("Create account %s." % (account.name))
                if Utils.Debug: Utils.Print("Validation node %s" % validationNodeIndex)
                trans=self.createAccountAndVerify(account, creator, stakedDeposit, validationNodeIndex=validationNodeIndex)
                if trans is None:
                    Utils.Print("ERROR: Failed to create account %s." % (account.name))
                    return False
                if Utils.Debug: Utils.Print("Account %s created." % (account.name))
                transId=Node.getTransId(trans)

        if waitForTransBlock and transId is not None:
            node=self.nodes[validationNodeIndex]
            if Utils.Debug: Utils.Print("Wait for transaction id %s on server port %d." % ( transId, node.port))
            if node.waitForTransactionInBlock(transId) is False:
                Utils.Print("ERROR: Failed to validate transaction %s got rolled into a block on server port %d." % (transId, node.port))
                return False

        return True

    def discoverUnstartedLocalNodes(self, unstartedNodes, totalNodes):
        unstarted=[]
        firstUnstartedNode=totalNodes-unstartedNodes
        for nodeId in range(firstUnstartedNode, totalNodes):
            unstarted.append(self.discoverUnstartedLocalNode(nodeId))
        return unstarted

    def discoverUnstartedLocalNode(self, nodeId):
        startFile=Node.unstartedFile(nodeId)
        with open(startFile, 'r') as file:
            cmd=file.read()
            Utils.Print("unstarted local node cmd: %s" % (cmd))
        instance=Node(self.host, port=self.port+nodeId, nodeId=nodeId, pid=None, cmd=cmd, walletMgr=self.walletMgr, nodeosVers=self.nodeosVers)
        if Utils.Debug: Utils.Print("Unstarted Node>", instance)
        return instance

    def getInfos(self, silentErrors=False, exitOnError=False):
        infos=[]
        for node in self.nodes:
            infos.append(node.getInfo(silentErrors=silentErrors, exitOnError=exitOnError))

        return infos

    def reportStatus(self):
        nodes = self.getAllNodes()
        for node in nodes:
            try:
                node.reportStatus()
            except:
                Utils.Print("No reportStatus for nodeId: %s" % (node.nodeId))

    def getBlockLog(self, nodeExtension, blockLogAction=BlockLogAction.return_blocks, outputFile=None, first=None, last=None, throwException=False, silentErrors=False, exitOnError=False):
        blockLogDir=Utils.getNodeDataDir(nodeExtension, "blocks")
        return Utils.getBlockLog(blockLogDir, blockLogAction=blockLogAction, outputFile=outputFile, first=first, last=last,  throwException=throwException, silentErrors=silentErrors, exitOnError=exitOnError)

    def printBlockLog(self):
        blockLogBios=self.getBlockLog("bios")
        Utils.Print(Utils.FileDivider)
        Utils.Print("Block log from %s:\n%s" % ("bios", json.dumps(blockLogBios, indent=1)))

        if not hasattr(self, "nodes"):
            return

        numNodes=len(self.nodes)
        for i in range(numNodes):
            node=self.nodes[i]
            blockLog=self.getBlockLog(i)
            Utils.Print(Utils.FileDivider)
            Utils.Print("Block log from node %s:\n%s" % (i, json.dumps(blockLog, indent=1)))

    def compareBlockLogs(self):
        blockLogs=[]
        blockNameExtensions=[]
        lowestMaxes=[]

        def back(arr):
            return arr[len(arr)-1]

        def sortLowest(maxes,max):
            for i in range(len(maxes)):
                if max < maxes[i]:
                    maxes.insert(i, max)
                    return

            maxes.append(max)

        i="bios"
        blockLog=self.getBlockLog(i)
        if blockLog is None:
            Utils.errorExit("Node %s does not have a block log, all nodes must have a block log" % (i))
        blockLogs.append(blockLog)
        blockNameExtensions.append(i)
        sortLowest(lowestMaxes,back(blockLog)["block_num"])

        if not hasattr(self, "nodes"):
            Utils.errorExit("There are not multiple nodes to compare, this method assumes that two nodes or more are expected")

        numNodes=len(self.nodes)
        for i in range(numNodes):
            node=self.nodes[i]
            blockLog=self.getBlockLog(i)
            if blockLog is None:
                Utils.errorExit("Node %s does not have a block log, all nodes must have a block log" % (i))
            blockLogs.append(blockLog)
            blockNameExtensions.append(i)
            sortLowest(lowestMaxes,back(blockLog)["block_num"])

        numNodes=len(blockLogs)

        if numNodes < 2:
            Utils.errorExit("There are not multiple nodes to compare, this method assumes that two nodes or more are expected")

        if lowestMaxes[0] < 2:
            Utils.errorExit("One or more nodes only has %d blocks, if that is a valid scenario, then compareBlockLogs shouldn't be called" % (lowestMaxes[0]))

        # create a list of block logs and name extensions for the given common block number span
        def identifyCommon(blockLogs, blockNameExtensions, first, last):
            commonBlockLogs=[]
            commonBlockNameExtensions=[]
            for i in range(numNodes):
                if (len(blockLogs[i]) >= last):
                    commonBlockLogs.append(blockLogs[i][first:last])
                    commonBlockNameExtensions.append(blockNameExtensions[i])
            return (commonBlockLogs,commonBlockNameExtensions)

        # compare the contents of the blockLogs for the given common block number span
        def compareCommon(blockLogs, blockNameExtensions, first, last):
            if Utils.Debug: Utils.Print("comparing block num %s through %s" % (first, last))
            commonBlockLogs=None
            commonBlockNameExtensions=None
            (commonBlockLogs,commonBlockNameExtensions) = identifyCommon(blockLogs, blockNameExtensions, first, last)
            numBlockLogs=len(commonBlockLogs)
            if numBlockLogs < 2:
                return False

            ret=None
            for i in range(1,numBlockLogs):
                context="<comparing block logs for node[%s] and node[%s]>" % (commonBlockNameExtensions[0], commonBlockNameExtensions[i])
                if Utils.Debug: Utils.Print("context=%s" % (context))
                ret=Utils.compare(commonBlockLogs[0], commonBlockLogs[i], context)
                if ret is not None:
                    blockLogDir1=Utils.DataDir + Utils.nodeExtensionToName(commonBlockNameExtensions[0]) + "/blocks/"
                    blockLogDir2=Utils.DataDir + Utils.nodeExtensionToName(commonBlockNameExtensions[i]) + "/blocks/"
                    Utils.Print(Utils.FileDivider)
                    Utils.Print("Block log from %s:\n%s" % (blockLogDir1, json.dumps(commonBlockLogs[0], indent=1)))
                    Utils.Print(Utils.FileDivider)
                    Utils.Print("Block log from %s:\n%s" % (blockLogDir2, json.dumps(commonBlockLogs[i], indent=1)))
                    Utils.Print(Utils.FileDivider)
                    Utils.errorExit("Block logs do not match, difference description -> %s" % (ret))

            return True

        def stripValues(lowestMaxes,greaterThan):
            newLowest=[]
            for low in lowestMaxes:
                if low > greaterThan:
                    newLowest.append(low)
            return newLowest

        first=0
        while len(lowestMaxes)>0 and compareCommon(blockLogs, blockNameExtensions, first, lowestMaxes[0]):
            first=lowestMaxes[0]+1
            lowestMaxes=stripValues(lowestMaxes,lowestMaxes[0])

    def launchTrxGenerators(self, contractOwnerAcctName: str, acctNamesList: list, acctPrivKeysList: list,
                            nodeId: int=0, tpsPerGenerator: int=10, numGenerators: int=1, durationSec: int=60,
                            waitToComplete:bool=False, abiFile=None, actionsData=None, actionsAuths=None,
                            trxGenerator=Path("./tests/trx_generator/trx_generator")):
        Utils.Print("Configure txn generators")
        node=self.getNode(nodeId)
        info = node.getInfo()
        chainId = info['chain_id']
        lib_id = info['last_irreversible_block_id']

        targetTps = tpsPerGenerator*numGenerators
        tpsLimitPerGenerator=tpsPerGenerator

        self.preExistingFirstTrxFiles = glob.glob(f"{Utils.DataDir}/first_trx_*.txt")
        connectionPairList = [f"{self.host}:{self.getNodeP2pPort(nodeId)}"]
        tpsTrxGensConfig = TpsTrxGensConfig(targetTps=targetTps, tpsLimitPerGenerator=tpsLimitPerGenerator, connectionPairList=connectionPairList)
        self.trxGenLauncher = TransactionGeneratorsLauncher(trxGenerator=trxGenerator, chainId=chainId, lastIrreversibleBlockId=lib_id,
                                                    contractOwnerAccount=contractOwnerAcctName, accts=','.join(map(str, acctNamesList)),
                                                    privateKeys=','.join(map(str, acctPrivKeysList)), trxGenDurationSec=durationSec, logDir=Utils.DataDir,
                                                    abiFile=abiFile, actionsData=actionsData, actionsAuths=actionsAuths, tpsTrxGensConfig=tpsTrxGensConfig,
                                                    endpointMode="p2p")

        Utils.Print("Launch txn generators and start generating/sending transactions")
        self.trxGenLauncher.launch(waitToComplete=waitToComplete)

    def waitForTrxGeneratorsSpinup(self, nodeId: int, numGenerators: int, timeout: int=None):
        node=self.getNode(nodeId)

        lam = lambda: len([ftf for ftf in glob.glob(f"{Utils.DataDir}/first_trx_*.txt") if ftf not in self.preExistingFirstTrxFiles]) >= numGenerators
        Utils.waitForBool(lam, timeout)

        firstTrxFiles = glob.glob(f"{Utils.DataDir}/first_trx_*.txt")
        curFirstTrxFiles = [ftf for ftf in firstTrxFiles if ftf not in self.preExistingFirstTrxFiles]

        firstTrxs = []
        for fileName in curFirstTrxFiles:
            Utils.Print(f"Found first trx record: {fileName}")
            with open(fileName, 'rt') as f:
                for line in f:
                    firstTrxs.append(line.rstrip('\n'))
        Utils.Print(f"first transactions: {firstTrxs}")
        status = node.waitForTransactionsInBlock(firstTrxs)
        if not status:
            Utils.Print('ERROR: Failed to spin up transaction generators: never received first transactions')
        return status
