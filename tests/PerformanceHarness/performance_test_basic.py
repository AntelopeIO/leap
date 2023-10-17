#!/usr/bin/env python3

import argparse
import dataclasses
import os
import re
import sys
import shutil
import signal
import json
import traceback

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

from .log_reader import blockData, trxData, chainData, scrapeTrxGenTrxSentDataLogs, JsonReportHandler, analyzeLogResults, TpsTestConfig, ArtifactPaths, LogAnalysis
from .NodeosPluginArgs import ChainPluginArgs, HttpPluginArgs, NetPluginArgs, ProducerPluginArgs, ResourceMonitorPluginArgs, SignatureProviderPluginArgs, StateHistoryPluginArgs, TraceApiPluginArgs
from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr, TransactionGeneratorsLauncher, TpsTrxGensConfig
from TestHarness.TestHelper import AppArgs
from dataclasses import dataclass, asdict, field
from datetime import datetime, timedelta
from pathlib import Path
from platform import release, system

class PerformanceTestBasic:
    @dataclass
    class PtbTpsTestResult:
        completedRun: bool = False
        numGeneratorsUsed: int = 0
        targetTpsPerGenList: list = field(default_factory=list)
        trxGenExitCodes: list = field(default_factory=list)

    @dataclass
    class PerfTestBasicResult:
        testStart: datetime = None
        testEnd: datetime = None
        testDuration: timedelta = None
        testPassed: bool = False
        testRunSuccessful: bool = False
        testRunCompleted: bool = False
        tpsExpectMet: bool = False
        trxExpectMet: bool = False
        targetTPS: int = 0
        resultAvgTps: float = 0
        expectedTxns: int = 0
        resultTxns: int = 0
        testAnalysisBlockCnt: int = 0
        logsDir: Path = Path("")

        def __post_init__(self):
            self.testDuration = None if self.testStart is None or self.testEnd is None else self.testEnd - self.testStart
            self.tpsExpectMet=True if self.resultAvgTps >= self.targetTPS else abs(self.targetTPS - self.resultAvgTps) < 100
            self.trxExpectMet=self.expectedTxns == self.resultTxns
            self.testRunSuccessful = self.testRunCompleted and self.expectedTxns == self.resultTxns
            self.testPassed = self.testRunSuccessful and self.tpsExpectMet and self.trxExpectMet

    @dataclass
    class TestHelperConfig:
        dumpErrorDetails: bool = False
        delay: int = 1
        nodesFile: str = None
        verbose: bool = False
        unshared: bool = False

    @dataclass
    class ClusterConfig:
        @dataclass
        class ExtraNodeosArgs:

            chainPluginArgs: ChainPluginArgs = field(default_factory=ChainPluginArgs)
            httpPluginArgs: HttpPluginArgs = field(default_factory=HttpPluginArgs)
            netPluginArgs: NetPluginArgs = field(default_factory=NetPluginArgs)
            producerPluginArgs: ProducerPluginArgs = field(default_factory=ProducerPluginArgs)
            resourceMonitorPluginArgs: ResourceMonitorPluginArgs = field(default_factory=ResourceMonitorPluginArgs)
            signatureProviderPluginArgs: SignatureProviderPluginArgs = field(default_factory=SignatureProviderPluginArgs)
            stateHistoryPluginArgs: StateHistoryPluginArgs = field(default_factory=StateHistoryPluginArgs)
            traceApiPluginArgs: TraceApiPluginArgs = field(default_factory=TraceApiPluginArgs)

            def __str__(self) -> str:
                args = []
                for field in dataclasses.fields(self):
                    match = re.search("\w*PluginArgs", field.name)
                    if match is not None:
                        args.append(f"{getattr(self, field.name)}")
                return " ".join(args)

        @dataclass
        class SpecifiedContract:
            contractDir: str = "unittests/contracts/eosio.system"
            wasmFile: str = "eosio.system.wasm"
            abiFile: str = "eosio.system.abi"
            account: Account = Account("eosio")

        producerNodeCount: int = 1
        validationNodeCount: int = 1
        apiNodeCount: int = 0
        dontKill: bool = False # leave_running
        extraNodeosArgs: ExtraNodeosArgs = field(default_factory=ExtraNodeosArgs)
        specifiedContract: SpecifiedContract = field(default_factory=SpecifiedContract)
        genesisPath: Path = Path("tests")/"PerformanceHarness"/"genesis.json"
        maximumP2pPerHost: int = 5000
        maximumClients: int = 0
        keepLogs: bool = True
        loggingLevel: str = "info"
        loggingDict: dict = field(default_factory=lambda: { "bios": "off" })
        prodsEnableTraceApi: bool = False
        nodeosVers: str = ""
        specificExtraNodeosArgs: dict = field(default_factory=dict)
        _totalNodes: int = 2
        _pNodes: int = 1
        _producerNodeIds: list = field(default_factory=list)
        _validationNodeIds: list = field(default_factory=list)
        _apiNodeIds: list = field(default_factory=list)
        nonProdsEosVmOcEnable: bool = False
        apiNodesReadOnlyThreadCount: int = 0

        def __post_init__(self):
            self._totalNodes = self.producerNodeCount + self.validationNodeCount + self.apiNodeCount
            # Setup Expectations for Producer and Validation Node IDs
            # Producer Nodes are index [0, producerNodeCount) and non-producer nodes (validationNodeCount, apiNodeCount) nodes follow the producer nodes [producerNodeCount, _totalNodes)
            self._producerNodeIds = list(range(0, self.producerNodeCount))
            self._validationNodeIds = list(range(self.producerNodeCount, self.producerNodeCount + self.validationNodeCount))
            self._apiNodeIds = list(range(self.producerNodeCount + self.validationNodeCount, self.producerNodeCount + self.validationNodeCount + self.validationNodeCount))

            def configureValidationNodes():
                validationNodeSpecificNodeosStr = ""
                validationNodeSpecificNodeosStr += '--p2p-accept-transactions false '
                if "v2" in self.nodeosVers:
                    validationNodeSpecificNodeosStr += '--plugin eosio::history_api_plugin --filter-on "*" '
                else:
                    #If prodsEnableTraceApi, then Cluster configures all nodes with trace_api_plugin so no need to duplicate here
                    if not self.prodsEnableTraceApi:
                        validationNodeSpecificNodeosStr += "--plugin eosio::trace_api_plugin "
                if self.nonProdsEosVmOcEnable:
                    validationNodeSpecificNodeosStr += "--eos-vm-oc-enable all "
                if validationNodeSpecificNodeosStr:
                    self.specificExtraNodeosArgs.update({f"{nodeId}" : validationNodeSpecificNodeosStr for nodeId in self._validationNodeIds})

            def configureApiNodes():
                apiNodeSpecificNodeosStr = ""
                apiNodeSpecificNodeosStr += "--p2p-accept-transactions false "
                apiNodeSpecificNodeosStr += "--plugin eosio::chain_api_plugin "
                apiNodeSpecificNodeosStr += "--plugin eosio::net_api_plugin "
                if "v4" in self.nodeosVers:
                    apiNodeSpecificNodeosStr += f"--read-only-threads {self.apiNodesReadOnlyThreadCount} "
                if apiNodeSpecificNodeosStr:
                    self.specificExtraNodeosArgs.update({f"{nodeId}" : apiNodeSpecificNodeosStr for nodeId in self._apiNodeIds})

            if self.validationNodeCount > 0:
                configureValidationNodes()
            if self.apiNodeCount > 0:
                configureApiNodes()

            assert "v1" not in self.nodeosVers and "v0" not in self.nodeosVers, f"nodeos version {Utils.getNodeosVersion()} is unsupported by performance test"
            if "v2" in self.nodeosVers:
                self.writeTrx = lambda trxDataFile, blockNum, trx: [trxDataFile.write(f"{trx['trx']['id']},{blockNum},{trx['cpu_usage_us']},{trx['net_usage_words']}\n")]
                self.createBlockData = lambda block, blockTransactionTotal, blockNetTotal, blockCpuTotal: blockData(blockId=block["payload"]["id"], blockNum=block['payload']['block_num'], transactions=blockTransactionTotal, net=blockNetTotal, cpu=blockCpuTotal, producer=block["payload"]["producer"], status=block["payload"]["confirmed"], _timestamp=block["payload"]["timestamp"])
                self.updateTrxDict = lambda blockNum, transaction, trxDict: trxDict.update(dict([(transaction['trx']['id'], trxData(blockNum, transaction['cpu_usage_us'], transaction['net_usage_words']))]))
            else:
                self.writeTrx = lambda trxDataFile, blockNum, trx:[ trxDataFile.write(f"{trx['id']},{trx['block_num']},{trx['block_time']},{trx['cpu_usage_us']},{trx['net_usage_words']},{trx['actions']}\n") ]
                self.createBlockData = lambda block, blockTransactionTotal, blockNetTotal, blockCpuTotal: blockData(blockId=block["payload"]["id"], blockNum=block['payload']['number'], transactions=blockTransactionTotal, net=blockNetTotal, cpu=blockCpuTotal, producer=block["payload"]["producer"], status=block["payload"]["status"], _timestamp=block["payload"]["timestamp"])
                self.updateTrxDict = lambda blockNum, transaction, trxDict: trxDict.update(dict([(transaction["id"], trxData(blockNum=transaction["block_num"], cpuUsageUs=transaction["cpu_usage_us"], netUsageUs=transaction["net_usage_words"], blockTime=transaction["block_time"]))]))
    @dataclass
    class PtbConfig:
        targetTps: int=8000
        testTrxGenDurationSec: int=30
        tpsLimitPerGenerator: int=4000
        numAddlBlocksToPrune: int=2
        logDirRoot: Path=Path(".")
        delReport: bool=False
        quiet: bool=False
        delPerfLogs: bool=False
        expectedTransactionsSent: int = field(default_factory=int, init=False)
        printMissingTransactions: bool=False
        userTrxDataFile: Path=None
        endpointMode: str="p2p"
        apiEndpoint: str=None
        trxGenerator: Path=Path(".")
        saveState: bool=False

        def __post_init__(self):
            self.expectedTransactionsSent = self.testTrxGenDurationSec * self.targetTps
            if (self.endpointMode == "http"):
                self.apiEndpoint="/v1/chain/send_transaction2"

    @dataclass
    class LoggingConfig:
        logDirBase: Path = Path(".")/PurePath(PurePath(__file__).name).stem
        logDirTimestamp: str = f"{datetime.utcnow().strftime('%Y-%m-%d_%H-%M-%S')}"
        logDirTimestampedOptSuffix: str = ""
        logDirPath: Path = field(default_factory=Path, init=False)

        def __post_init__(self):
            self.logDirPath = self.logDirBase/Path(f"{self.logDirTimestamp}{self.logDirTimestampedOptSuffix}")

    def __init__(self, testHelperConfig: TestHelperConfig=TestHelperConfig(), clusterConfig: ClusterConfig=ClusterConfig(), ptbConfig=PtbConfig(), testNamePath="performance_test_basic"):
        self.testHelperConfig = testHelperConfig
        self.clusterConfig = clusterConfig
        self.ptbConfig = ptbConfig

        #Results
        self.ptbTpsTestResult = PerformanceTestBasic.PtbTpsTestResult()
        self.testResult = PerformanceTestBasic.PerfTestBasicResult()

        self.testHelperConfig.keepLogs = not self.ptbConfig.delPerfLogs

        Utils.Debug = self.testHelperConfig.verbose
        self.errorExit = Utils.errorExit
        self.emptyBlockGoal = 1

        self.testStart = datetime.utcnow()
        self.testEnd = self.testStart
        self.testNamePath = testNamePath
        self.loggingConfig = PerformanceTestBasic.LoggingConfig(logDirBase=Path(self.ptbConfig.logDirRoot)/f"{self.testNamePath}Logs",
                                                                logDirTimestamp=f"{self.testStart.strftime('%Y-%m-%d_%H-%M-%S')}",
                                                                logDirTimestampedOptSuffix = f"-{self.ptbConfig.targetTps}")

        self.trxGenLogDirPath = self.loggingConfig.logDirPath/Path("trxGenLogs")
        self.varLogsDirPath = self.loggingConfig.logDirPath/Path("var")
        self.etcLogsDirPath = self.loggingConfig.logDirPath/Path("etc")
        self.etcEosioLogsDirPath = self.etcLogsDirPath/Path("eosio")
        self.blockDataLogDirPath = self.loggingConfig.logDirPath/Path("blockDataLogs")
        self.blockDataPath = self.blockDataLogDirPath/Path("blockData.txt")
        self.transactionMetricsDataPath = self.blockDataLogDirPath/Path("transaction_metrics.csv")
        self.blockTrxDataPath = self.blockDataLogDirPath/Path("blockTrxData.txt")
        self.reportPath = self.loggingConfig.logDirPath/Path("data.json")

        # Use first producer node and first non-producer node
        self.producerNodeId = self.clusterConfig._producerNodeIds[0]
        self.validationNodeId = self.clusterConfig._validationNodeIds[0]
        pid = os.getpid()
        self.nodeosLogDir =  Path(self.loggingConfig.logDirPath)/"var"/f"{Utils.DataRoot}{Utils.PID}"
        self.nodeosLogPath = self.nodeosLogDir/f"node_{str(self.validationNodeId).zfill(2)}"/"stderr.txt"

        # Setup cluster and its wallet manager
        self.walletMgr=WalletMgr(True)
        self.cluster=Cluster(loggingLevel=self.clusterConfig.loggingLevel, loggingLevelDict=self.clusterConfig.loggingDict,
                             nodeosVers=self.clusterConfig.nodeosVers,unshared=self.testHelperConfig.unshared,
                             keepRunning=self.clusterConfig.dontKill, keepLogs=self.clusterConfig.keepLogs)
        self.cluster.setWalletMgr(self.walletMgr)

    def testDirsCleanup(self, delReport: bool=False):
        try:
            def removeArtifacts(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if Path(path).is_dir():
                    print(f"Cleaning up test artifacts dir and all contents of: {path}")
                    shutil.rmtree(f"{path}")

            def removeAllArtifactsExceptFinalReport():
                removeArtifacts(self.trxGenLogDirPath)
                removeArtifacts(self.varLogsDirPath)
                removeArtifacts(self.etcEosioLogsDirPath)
                removeArtifacts(self.etcLogsDirPath)
                removeArtifacts(self.blockDataLogDirPath)

            if not delReport:
                removeAllArtifactsExceptFinalReport()
            else:
                removeArtifacts(self.loggingConfig.logDirPath)
        except OSError as error:
            print(error)

    def testDirsCleanupState(self):
        try:
            def removeArtifacts(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if Path(path).is_dir():
                    print(f"Cleaning up test artifacts dir and all contents of: {path}")
                    shutil.rmtree(f"{path}")
            nodeDirPaths = list(Path(self.varLogsDirPath).rglob("node_*"))
            print(f"nodeDirPaths: {nodeDirPaths}")
            for nodeDirPath in nodeDirPaths:
                stateDirs = list(Path(nodeDirPath).rglob("state"))
                print(f"stateDirs: {stateDirs}")
                for stateDirPath in stateDirs:
                    removeArtifacts(stateDirPath)

        except OSError as error:
            print(error)

    def testDirsSetup(self):
        try:
            def createArtifactsDir(path):
                print(f"Checking if test artifacts dir exists: {path}")
                if not Path(path).is_dir():
                    print(f"Creating test artifacts dir: {path}")
                    os.mkdir(f"{path}")

            createArtifactsDir(self.ptbConfig.logDirRoot)
            createArtifactsDir(self.loggingConfig.logDirBase)
            createArtifactsDir(self.loggingConfig.logDirPath)
            createArtifactsDir(self.trxGenLogDirPath)
            createArtifactsDir(self.varLogsDirPath)
            createArtifactsDir(self.etcLogsDirPath)
            createArtifactsDir(self.etcEosioLogsDirPath)
            createArtifactsDir(self.blockDataLogDirPath)

        except OSError as error:
            print(error)

    def fileOpenMode(self, filePath) -> str:
        if filePath.exists():
            append_write = 'a'
        else:
            append_write = 'w'
        return append_write

    def isOnBlockTransaction(self, transaction):
        # v2 history does not include onblock
        if "v2" in self.clusterConfig.nodeosVers:
            return False
        else:
            if transaction['actions'][0]['account'] != 'eosio' or transaction['actions'][0]['action'] != 'onblock':
                return False
        return True

    def queryBlockTrxData(self, node, blockDataPath, blockTrxDataPath, startBlockNum, endBlockNum):
        for blockNum in range(startBlockNum, endBlockNum + 1):
            blockCpuTotal, blockNetTotal, blockTransactionTotal = 0, 0, 0
            block = node.fetchBlock(blockNum)
            btdf_append_write = self.fileOpenMode(blockTrxDataPath)
            with open(blockTrxDataPath, btdf_append_write) as trxDataFile:
                for transaction in block['payload']['transactions']:
                    if not self.isOnBlockTransaction(transaction):
                        self.clusterConfig.updateTrxDict(blockNum, transaction, self.data.trxDict)
                        self.clusterConfig.writeTrx(trxDataFile, blockNum, transaction)
                        blockCpuTotal += transaction["cpu_usage_us"]
                        blockNetTotal += transaction["net_usage_words"]
                        blockTransactionTotal += 1
            blockData = self.clusterConfig.createBlockData(block=block, blockTransactionTotal=blockTransactionTotal,
                                                           blockNetTotal=blockNetTotal, blockCpuTotal=blockCpuTotal)
            self.data.blockList.append(blockData)
            self.data.blockDict[str(blockNum)] = blockData
            bdf_append_write = self.fileOpenMode(blockDataPath)
            with open(blockDataPath, bdf_append_write) as blockDataFile:
                blockDataFile.write(f"{blockData.blockNum},{blockData.blockId},{blockData.producer},{blockData.status},{blockData._timestamp}\n")

    def waitForEmptyBlocks(self, node, numEmptyToWaitOn):
        emptyBlocks = 0
        while emptyBlocks < numEmptyToWaitOn:
            headBlock = node.getHeadBlockNum()
            block = node.fetchHeadBlock(node, headBlock)
            node.waitForHeadToAdvance()
            if block['payload']['transaction_mroot'] == "0000000000000000000000000000000000000000000000000000000000000000":
                emptyBlocks += 1
            else:
                emptyBlocks = 0
        return node.getHeadBlockNum()

    def launchCluster(self):
        return self.cluster.launch(
            pnodes=self.clusterConfig._pNodes,
            totalNodes=self.clusterConfig._totalNodes,
            genesisPath=self.clusterConfig.genesisPath,
            maximumP2pPerHost=self.clusterConfig.maximumP2pPerHost,
            maximumClients=self.clusterConfig.maximumClients,
            extraNodeosArgs=str(self.clusterConfig.extraNodeosArgs),
            prodsEnableTraceApi=self.clusterConfig.prodsEnableTraceApi,
            specificExtraNodeosArgs=self.clusterConfig.specificExtraNodeosArgs
            )

    def setupWalletAndAccounts(self, accountCnt: int=2, accountNames: list=None):
        self.accountNames=[]
        newAccountNames=[]
        self.accountPrivKeys=[]
        if accountNames is not None:
            for name in accountNames:
                if name == self.clusterConfig.specifiedContract.account.name:
                    self.cluster.accounts.append(self.clusterConfig.specifiedContract.account)
                    self.accountNames.append(self.clusterConfig.specifiedContract.account.name)
                    self.accountPrivKeys.append(self.clusterConfig.specifiedContract.account.ownerPrivateKey)
                    self.accountPrivKeys.append(self.clusterConfig.specifiedContract.account.activePrivateKey)
                else:
                    ret = self.cluster.biosNode.getEosAccount(name)
                    if ret is None:
                        newAccountNames.append(name)
            self.cluster.populateWallet(accountsCount=len(newAccountNames), wallet=self.wallet, accountNames=newAccountNames)
            self.cluster.createAccounts(self.cluster.eosioAccount, stakedDeposit=0, validationNodeIndex=self.validationNodeId)
            if len(newAccountNames) != 0:
                for index in range(len(self.accountNames), len(accountNames)):
                    self.accountNames.append(self.cluster.accounts[index].name)
                    self.accountPrivKeys.append(self.cluster.accounts[index].activePrivateKey)
                    self.accountPrivKeys.append(self.cluster.accounts[index].ownerPrivateKey)
        else:
            self.cluster.populateWallet(accountsCount=accountCnt, wallet=self.wallet)
            self.cluster.createAccounts(self.cluster.eosioAccount, stakedDeposit=0, validationNodeIndex=self.validationNodeId)
            for index in range(0, accountCnt):
                self.accountNames.append(self.cluster.accounts[index].name)
                self.accountPrivKeys.append(self.cluster.accounts[index].activePrivateKey)

    def readUserTrxDataFromFile(self, userTrxDataFile: Path):
        with open(userTrxDataFile) as f:
            self.userTrxDataDict = json.load(f)

    def setupContract(self):
        if self.clusterConfig.specifiedContract.account.name != self.cluster.eosioAccount.name:
            self.cluster.populateWallet(accountsCount=1, wallet=self.wallet, accountNames=[self.clusterConfig.specifiedContract.account.name], createProducerAccounts=False)
            self.cluster.createAccounts(self.cluster.eosioAccount, stakedDeposit=0, validationNodeIndex=self.validationNodeId)
            self.clusterConfig.specifiedContract.account = self.cluster.accounts[0]
            print("Publishing contract")
            transaction=self.cluster.biosNode.publishContract(self.clusterConfig.specifiedContract.account, self.clusterConfig.specifiedContract.contractDir,
                                                            self.clusterConfig.specifiedContract.wasmFile,
                                                            self.clusterConfig.specifiedContract.abiFile, waitForTransBlock=True)
            if transaction is None:
                print("ERROR: Failed to publish contract.")
                return None
        else:
            print(f"setupContract: default {self.clusterConfig.specifiedContract.account.name} \
                    activePrivateKey: {self.clusterConfig.specifiedContract.account.activePrivateKey} \
                    activePublicKey: {self.clusterConfig.specifiedContract.account.activePublicKey} \
                    ownerPrivateKey: {self.clusterConfig.specifiedContract.account.ownerPrivateKey} \
                    ownerPublicKey: {self.clusterConfig.specifiedContract.account.ownerPublicKey}")

    def runTpsTest(self) -> PtbTpsTestResult:
        completedRun = False
        self.producerNode = self.cluster.getNode(self.producerNodeId)
        self.connectionPairList = []

        def configureConnections():
            if(self.ptbConfig.endpointMode == "http"):
                for apiNodeId in self.clusterConfig._apiNodeIds:
                    self.connectionPairList.append(f"{self.cluster.getNode(apiNodeId).host}:{self.cluster.getNode(apiNodeId).port}")
            else: # endpointMode == p2p
                for producerId in self.clusterConfig._producerNodeIds:
                    self.connectionPairList.append(f"{self.cluster.getNode(producerId).host}:{self.cluster.getNodeP2pPort(producerId)}")

        configureConnections()
        self.validationNode = self.cluster.getNode(self.validationNodeId)
        self.wallet = self.walletMgr.create('default')
        self.setupContract()
        info = self.producerNode.getInfo()
        chainId = info['chain_id']
        lib_id = info['last_irreversible_block_id']
        self.data = chainData()
        self.data.numNodes = self.clusterConfig._totalNodes

        abiFile=None
        actionsDataJson=None
        actionsAuthsJson=None
        self.accountNames=[]
        self.accountPrivKeys=[]
        if (self.ptbConfig.userTrxDataFile is not None):
            self.readUserTrxDataFromFile(self.ptbConfig.userTrxDataFile)
            if self.userTrxDataDict['initAccounts']:
                print(f"Creating accounts specified in userTrxData: {self.userTrxDataDict['initAccounts']}")
                self.setupWalletAndAccounts(accountCnt=len(self.userTrxDataDict['initAccounts']), accountNames=self.userTrxDataDict['initAccounts'])
            abiFile = self.userTrxDataDict['abiFile']
            if 'apiEndpoint' in self.userTrxDataDict:
                self.ptbConfig.apiEndpoint = self.userTrxDataDict['apiEndpoint']
                print(f'API Endpoint specified: {self.ptbConfig.apiEndpoint}')

            actionsDataJson = json.dumps(self.userTrxDataDict['actions'])

            authorizations={}
            for act in self.userTrxDataDict['actions']:
                actionAuthAcct=act["actionAuthAcct"]
                actionAuthPrivKey=None
                if actionAuthAcct == self.cluster.eosioAccount.name:
                    actionAuthPrivKey = self.cluster.eosioAccount.activePrivateKey
                else:
                    for account in self.cluster.accounts:
                        if actionAuthAcct == account.name:
                            actionAuthPrivKey = account.activePrivateKey
                            break

                if actionAuthPrivKey is not None:
                    authorizations[actionAuthAcct]=actionAuthPrivKey
            actionsAuthsJson = json.dumps(authorizations)
        else:
            self.setupWalletAndAccounts()

        self.cluster.biosNode.kill(signal.SIGTERM)

        self.data.startBlock = self.waitForEmptyBlocks(self.validationNode, self.emptyBlockGoal)
        tpsTrxGensConfig = TpsTrxGensConfig(targetTps=self.ptbConfig.targetTps, tpsLimitPerGenerator=self.ptbConfig.tpsLimitPerGenerator, connectionPairList=self.connectionPairList)

        self.cluster.trxGenLauncher = TransactionGeneratorsLauncher(trxGenerator=self.ptbConfig.trxGenerator, chainId=chainId, lastIrreversibleBlockId=lib_id, contractOwnerAccount=self.clusterConfig.specifiedContract.account.name,
                                                       accts=','.join(map(str, self.accountNames)), privateKeys=','.join(map(str, self.accountPrivKeys)),
                                                       trxGenDurationSec=self.ptbConfig.testTrxGenDurationSec, logDir=self.trxGenLogDirPath,
                                                       abiFile=abiFile, actionsData=actionsDataJson, actionsAuths=actionsAuthsJson,
                                                       tpsTrxGensConfig=tpsTrxGensConfig, endpointMode=self.ptbConfig.endpointMode, apiEndpoint=self.ptbConfig.apiEndpoint)

        trxGenExitCodes = self.cluster.trxGenLauncher.launch()
        print(f"Transaction Generator exit codes: {trxGenExitCodes}")
        for exitCode in trxGenExitCodes:
            if exitCode != 0:
                completedRun = False
                break
        else:
            completedRun = True

        # Get stats after transaction generation stops
        trxSent = {}
        scrapeTrxGenTrxSentDataLogs(trxSent, self.trxGenLogDirPath, self.ptbConfig.quiet)
        if len(trxSent) != self.ptbConfig.expectedTransactionsSent:
            print(f"ERROR: Transactions generated: {len(trxSent)} does not match the expected number of transactions: {self.ptbConfig.expectedTransactionsSent}")
        blocksToWait = 2 * self.ptbConfig.testTrxGenDurationSec + 10
        trxNotFound = self.validationNode.waitForTransactionsInBlockRange(trxSent, self.data.startBlock, blocksToWait)
        self.data.ceaseBlock = self.validationNode.getHeadBlockNum()

        return PerformanceTestBasic.PtbTpsTestResult(completedRun=completedRun, numGeneratorsUsed=tpsTrxGensConfig.numGenerators,
                                                     targetTpsPerGenList=tpsTrxGensConfig.targetTpsPerGenList, trxGenExitCodes=trxGenExitCodes)

    def prepArgs(self) -> dict:
        args = {}
        args.update({"rawCmdLine ": ' '.join(sys.argv[0:])})
        args.update(asdict(self.testHelperConfig))
        args.update(asdict(self.clusterConfig))
        args.update(asdict(self.ptbConfig))
        args.update(asdict(self.loggingConfig))
        return args

    def captureLowLevelArtifacts(self):
        try:
            shutil.move(f"{self.cluster.nodeosLogPath}", f"{self.varLogsDirPath}")
        except Exception as e:
            print(f"Failed to move '{self.cluster.nodeosLogPath}' to '{self.varLogsDirPath}': {type(e)}: {e}")

    def createReport(self, logAnalysis: LogAnalysis, tpsTestConfig: TpsTestConfig, argsDict: dict, testResult: PerfTestBasicResult) -> dict:
        report = {}
        report['targetApiEndpointType'] = self.ptbConfig.endpointMode
        report['targetApiEndpoint'] = self.ptbConfig.apiEndpoint if self.ptbConfig.apiEndpoint is not None else "NA for P2P"
        report['Result'] = asdict(testResult)
        report['Analysis'] = {}
        report['Analysis']['BlockSize'] = asdict(logAnalysis.blockSizeStats)
        report['Analysis']['BlocksGuide'] = asdict(logAnalysis.guide)
        report['Analysis']['TPS'] = asdict(logAnalysis.tpsStats)
        report['Analysis']['TPS']['configTps'] = tpsTestConfig.targetTps
        report['Analysis']['TPS']['configTestDuration'] = tpsTestConfig.testDurationSec
        report['Analysis']['TPS']['tpsPerGenerator'] = tpsTestConfig.targetTpsPerGenList
        report['Analysis']['TPS']['generatorCount'] = tpsTestConfig.numTrxGensUsed
        report['Analysis']['TrxCPU'] = asdict(logAnalysis.trxCpuStats)
        report['Analysis']['TrxLatency'] = asdict(logAnalysis.trxLatencyStats)
        report['Analysis']['TrxLatency']['units'] = "seconds"
        report['Analysis']['TrxNet'] = asdict(logAnalysis.trxNetStats)
        report['Analysis']['TrxAckResponseTime'] = asdict(logAnalysis.trxAckStats)
        report['Analysis']['TrxAckResponseTime']['measurementApplicable'] = logAnalysis.trxAckStatsApplicable
        report['Analysis']['TrxAckResponseTime']['units'] = "microseconds"
        report['Analysis']['ExpectedTransactions'] = testResult.expectedTxns
        report['Analysis']['DroppedTransactions'] = len(logAnalysis.notFound)
        report['Analysis']['ProductionWindowsTotal'] = logAnalysis.prodWindows.totalWindows
        report['Analysis']['ProductionWindowsAverageSize'] = logAnalysis.prodWindows.averageWindowSize
        report['Analysis']['ProductionWindowsMissed'] = logAnalysis.prodWindows.missedWindows
        report['Analysis']['ForkedBlocks'] = {}
        report['Analysis']['ForksCount'] = {}
        report['Analysis']['DroppedBlocks'] = {}
        report['Analysis']['DroppedBlocksCount'] = {}
        for nodeNum in range(0, self.data.numNodes):
            formattedNodeNum = str(nodeNum).zfill(2)
            report['Analysis']['ForkedBlocks'][formattedNodeNum] = self.data.forkedBlocks[formattedNodeNum]
            report['Analysis']['ForksCount'][formattedNodeNum] = len(self.data.forkedBlocks[formattedNodeNum])
            report['Analysis']['DroppedBlocks'][formattedNodeNum] = self.data.droppedBlocks[formattedNodeNum]
            report['Analysis']['DroppedBlocksCount'][formattedNodeNum] = len(self.data.droppedBlocks[formattedNodeNum])
        report['args'] =  argsDict
        report['args']['userTrxData'] = self.userTrxDataDict if self.ptbConfig.userTrxDataFile is not None else "NOT CONFIGURED"
        report['env'] = {'system': system(), 'os': os.name, 'release': release(), 'logical_cpu_count': os.cpu_count()}
        report['nodeosVersion'] = self.clusterConfig.nodeosVers
        return report

    def analyzeResultsAndReport(self, testResult: PtbTpsTestResult):
        args = self.prepArgs()
        artifactsLocate = ArtifactPaths(nodeosLogDir=self.nodeosLogDir, nodeosLogPath=self.nodeosLogPath, trxGenLogDirPath=self.trxGenLogDirPath, blockTrxDataPath=self.blockTrxDataPath,
                                                   blockDataPath=self.blockDataPath, transactionMetricsDataPath=self.transactionMetricsDataPath)
        tpsTestConfig = TpsTestConfig(targetTps=self.ptbConfig.targetTps, testDurationSec=self.ptbConfig.testTrxGenDurationSec, tpsLimitPerGenerator=self.ptbConfig.tpsLimitPerGenerator,
                                                 numBlocksToPrune=self.ptbConfig.numAddlBlocksToPrune, numTrxGensUsed=testResult.numGeneratorsUsed, targetTpsPerGenList=testResult.targetTpsPerGenList,
                                                 quiet=self.ptbConfig.quiet, printMissingTransactions=self.ptbConfig.printMissingTransactions)
        self.logAnalysis = analyzeLogResults(data=self.data, tpsTestConfig=tpsTestConfig, artifacts=artifactsLocate)
        self.testEnd = datetime.utcnow()

        self.testResult = PerformanceTestBasic.PerfTestBasicResult(targetTPS=self.ptbConfig.targetTps, resultAvgTps=self.logAnalysis.tpsStats.avg, expectedTxns=self.ptbConfig.expectedTransactionsSent,
                                                                   resultTxns=self.logAnalysis.trxLatencyStats.samples, testRunCompleted=self.ptbTpsTestResult.completedRun,
                                                                   testAnalysisBlockCnt=self.logAnalysis.guide.testAnalysisBlockCnt, logsDir=self.loggingConfig.logDirPath,
                                                                   testStart=self.testStart, testEnd=self.testEnd)

        print(f"targetTPS: {self.testResult.targetTPS} expectedTxns: {self.testResult.expectedTxns} resultAvgTps: {self.testResult.resultAvgTps} resultTxns: {self.testResult.resultTxns}")

        if not self.ptbTpsTestResult.completedRun:
            for exitCode in self.ptbTpsTestResult.trxGenExitCodes:
                if exitCode != 0:
                    print(f"Error: Transaction Generator exited with error {exitCode}")

        if self.ptbTpsTestResult.completedRun and self.ptbConfig.expectedTransactionsSent != self.data.totalTransactions:
            print(f"Error: Transactions received: {self.data.totalTransactions} did not match expected total: {self.ptbConfig.expectedTransactionsSent}")

        print(f"testRunSuccessful: {self.testResult.testRunSuccessful} testPassed: {self.testResult.testPassed} tpsExpectationMet: {self.testResult.tpsExpectMet} trxExpectationMet: {self.testResult.trxExpectMet}")

        self.report = self.createReport(logAnalysis=self.logAnalysis, tpsTestConfig=tpsTestConfig, argsDict=args, testResult=self.testResult)

        jsonReport = None
        if not self.ptbConfig.quiet or not self.ptbConfig.delReport:
            jsonReport = JsonReportHandler.reportAsJSON(self.report)

        if not self.ptbConfig.quiet:
            print(self.data)

            print(f"Report:\n{jsonReport}")

        if not self.ptbConfig.delReport:
            JsonReportHandler.exportReportAsJSON(jsonReport, self.reportPath)

    def preTestSpinup(self):
        self.testDirsCleanup()
        self.testDirsSetup()

        self.walletMgr.launch()
        if self.launchCluster() == False:
            self.errorExit('Failed to stand up cluster.')

    def postTpsTestSteps(self):
        self.queryBlockTrxData(self.validationNode, self.blockDataPath, self.blockTrxDataPath, self.data.startBlock, self.data.ceaseBlock)
        self.cluster.shutdown()
        self.walletMgr.shutdown()

    def runTest(self) -> bool:

        try:
            # Kill any existing instances and launch cluster
            TestHelper.printSystemInfo("BEGIN")
            self.preTestSpinup()

            self.ptbTpsTestResult = self.runTpsTest()

            self.postTpsTestSteps()

            self.captureLowLevelArtifacts()
            self.analyzeResultsAndReport(self.ptbTpsTestResult)

        except:
            traceback.print_exc()

        finally:
            # Despite keepLogs being hardcoded to False, logs will still appear on test failure in TestLogs
            # due to testSuccessful being False
            TestHelper.shutdown(
                cluster=self.cluster,
                walletMgr=self.walletMgr,
                testSuccessful=self.testResult.testRunSuccessful,
                dumpErrorDetails=self.testHelperConfig.dumpErrorDetails
                )

            if self.ptbConfig.delPerfLogs:
                print(f"Cleaning up logs directory: {self.loggingConfig.logDirPath}")
                self.testDirsCleanup(self.ptbConfig.delReport)

            if not self.ptbConfig.saveState:
                print(f"Cleaning up state directories: {self.varLogsDirPath}")
                self.testDirsCleanupState()

            return self.testResult.testRunSuccessful

    def setupTestHelperConfig(args) -> TestHelperConfig:
        return PerformanceTestBasic.TestHelperConfig(dumpErrorDetails=args.dump_error_details, delay=args.d, verbose=args.v)

    def setupClusterConfig(args) -> ClusterConfig:

        chainPluginArgs = ChainPluginArgs(signatureCpuBillablePct=args.signature_cpu_billable_pct,
                                        chainThreads=args.chain_threads, databaseMapMode=args.database_map_mode,
                                        wasmRuntime=args.wasm_runtime, contractsConsole=args.contracts_console,
                                        eosVmOcCacheSizeMb=args.eos_vm_oc_cache_size_mb, eosVmOcCompileThreads=args.eos_vm_oc_compile_threads,
                                        blockLogRetainBlocks=args.block_log_retain_blocks,
                                        chainStateDbSizeMb=args.chain_state_db_size_mb, abiSerializerMaxTimeMs=990000)

        producerPluginArgs = ProducerPluginArgs(disableSubjectiveApiBilling=args.disable_subjective_billing,
                                                disableSubjectiveP2pBilling=args.disable_subjective_billing,
                                                cpuEffortPercent=args.cpu_effort_percent,
                                                producerThreads=args.producer_threads, maxTransactionTime=-1,
                                                readOnlyWriteWindowTimeUs=args.read_only_write_window_time_us,
                                                readOnlyReadWindowTimeUs=args.read_only_read_window_time_us)
        httpPluginArgs = HttpPluginArgs(httpMaxBytesInFlightMb=args.http_max_bytes_in_flight_mb, httpMaxInFlightRequests=args.http_max_in_flight_requests,
                                        httpMaxResponseTimeMs=args.http_max_response_time_ms, httpThreads=args.http_threads)
        netPluginArgs = NetPluginArgs(netThreads=args.net_threads, maxClients=0)
        nodeosVers=Utils.getNodeosVersion()
        resourceMonitorPluginArgs = ResourceMonitorPluginArgs(resourceMonitorNotShutdownOnThresholdExceeded=not "v2" in nodeosVers)
        ENA = PerformanceTestBasic.ClusterConfig.ExtraNodeosArgs
        extraNodeosArgs = ENA(chainPluginArgs=chainPluginArgs, httpPluginArgs=httpPluginArgs, producerPluginArgs=producerPluginArgs, netPluginArgs=netPluginArgs,
                            resourceMonitorPluginArgs=resourceMonitorPluginArgs)
        SC = PerformanceTestBasic.ClusterConfig.SpecifiedContract
        specifiedContract=SC(contractDir=args.contract_dir, wasmFile=args.wasm_file, abiFile=args.abi_file, account=Account(args.account_name))
        return PerformanceTestBasic.ClusterConfig(dontKill=args.leave_running, keepLogs=not args.del_perf_logs,
                                                            producerNodeCount=args.producer_nodes, validationNodeCount=args.validation_nodes, apiNodeCount=args.api_nodes,
                                                            genesisPath=args.genesis, prodsEnableTraceApi=args.prods_enable_trace_api, extraNodeosArgs=extraNodeosArgs,
                                                            specifiedContract=specifiedContract, loggingLevel=args.cluster_log_lvl,
                                                            nodeosVers=nodeosVers, nonProdsEosVmOcEnable=args.non_prods_eos_vm_oc_enable,
                                                            apiNodesReadOnlyThreadCount=args.api_nodes_read_only_threads)

class PtbArgumentsHandler(object):
    @staticmethod
    def _createBaseArgumentParser(defEndpointApiDef: str, defProdNodeCnt: int, defValidationNodeCnt: int, defApiNodeCnt: int, suppressHelp: bool=False):
        testHelperArgParser=TestHelper.createArgumentParser(includeArgs={"-d","--dump-error-details","-v","--leave-running"
                                                            ,"--unshared"}, suppressHelp=suppressHelp)
        ptbBaseParser = argparse.ArgumentParser(parents=[testHelperArgParser], add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptbBaseGrpTitle="Performance Test Basic Base"
        ptbBaseGrpDescription="Performance Test Basic base configuration items."
        ptbBaseParserGroup = ptbBaseParser.add_argument_group(title=None if suppressHelp else ptbBaseGrpTitle, description=None if suppressHelp else ptbBaseGrpDescription)

        ptbBaseParserGroup.add_argument("--endpoint-mode", type=str, help=argparse.SUPPRESS if suppressHelp else "Endpoint mode (\"p2p\", \"http\"). \
                                                                In \"p2p\" mode transactions will be directed to the p2p endpoint on a producer node. \
                                                                In \"http\" mode transactions will be directed to the http endpoint on an api node.",
                                                                choices=["p2p", "http"], default=defEndpointApiDef)
        ptbBaseParserGroup.add_argument("--producer-nodes", type=int, help=argparse.SUPPRESS if suppressHelp else "Producing nodes count", default=defProdNodeCnt)
        ptbBaseParserGroup.add_argument("--validation-nodes", type=int, help=argparse.SUPPRESS if suppressHelp else "Validation nodes count", default=defValidationNodeCnt)
        ptbBaseParserGroup.add_argument("--api-nodes", type=int, help=argparse.SUPPRESS if suppressHelp else "API nodes count", default=defApiNodeCnt)
        ptbBaseParserGroup.add_argument("--api-nodes-read-only-threads", type=int, help=argparse.SUPPRESS if suppressHelp else "API nodes read only threads count for use with read-only transactions", default=0)
        ptbBaseParserGroup.add_argument("--tps-limit-per-generator", type=int, help=argparse.SUPPRESS if suppressHelp else "Maximum amount of transactions per second a single generator can have.", default=4000)
        ptbBaseParserGroup.add_argument("--genesis", type=str, help=argparse.SUPPRESS if suppressHelp else "Path to genesis.json", default="tests/PerformanceHarness/genesis.json")
        ptbBaseParserGroup.add_argument("--num-blocks-to-prune", type=int, help=argparse.SUPPRESS if suppressHelp else ("The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, "
                                                                "to prune from the beginning and end of the range of blocks of interest for evaluation."), default=2)
        ptbBaseParserGroup.add_argument("--signature-cpu-billable-pct", type=int, help=argparse.SUPPRESS if suppressHelp else "Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50%%", default=0)
        ptbBaseParserGroup.add_argument("--chain-threads", type=int, help=argparse.SUPPRESS if suppressHelp else "Number of worker threads in controller thread pool", default=2)
        ptbBaseParserGroup.add_argument("--database-map-mode", type=str, help=argparse.SUPPRESS if suppressHelp else "Database map mode (\"mapped\", \"heap\", or \"locked\"). \
                                                                In \"mapped\" mode database is memory mapped as a file. \
                                                                In \"heap\" mode database is preloaded in to swappable memory and will use huge pages if available. \
                                                                In \"locked\" mode database is preloaded, locked in to memory, and will use huge pages if available.",
                                                                choices=["mapped", "heap", "locked"], default="mapped")
        ptbBaseParserGroup.add_argument("--cluster-log-lvl", type=str, help=argparse.SUPPRESS if suppressHelp else "Cluster log level (\"all\", \"debug\", \"info\", \"warn\", \"error\", or \"off\"). \
                                                                Performance Harness Test Basic relies on some logging at \"info\" level, so it is recommended lowest logging level to use. \
                                                                However, there are instances where more verbose logging can be useful.",
                                                                choices=["all", "debug", "info", "warn", "error", "off"], default="info")
        ptbBaseParserGroup.add_argument("--net-threads", type=int, help=argparse.SUPPRESS if suppressHelp else "Number of worker threads in net_plugin thread pool", default=4)
        ptbBaseParserGroup.add_argument("--disable-subjective-billing", type=bool, help=argparse.SUPPRESS if suppressHelp else "Disable subjective CPU billing for API/P2P transactions", default=True)
        ptbBaseParserGroup.add_argument("--cpu-effort-percent", type=int, help=argparse.SUPPRESS if suppressHelp else "Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%%", default=100)
        ptbBaseParserGroup.add_argument("--producer-threads", type=int, help=argparse.SUPPRESS if suppressHelp else "Number of worker threads in producer thread pool", default=2)
        ptbBaseParserGroup.add_argument("--read-only-write-window-time-us", type=int, help=argparse.SUPPRESS if suppressHelp else "Time in microseconds the write window lasts.", default=200000)
        ptbBaseParserGroup.add_argument("--read-only-read-window-time-us", type=int, help=argparse.SUPPRESS if suppressHelp else "Time in microseconds the read window lasts.", default=60000)
        ptbBaseParserGroup.add_argument("--http-max-in-flight-requests", type=int, help=argparse.SUPPRESS if suppressHelp else "Maximum number of requests http_plugin should use for processing http requests. 429 error response when exceeded. -1 for unlimited", default=-1)
        ptbBaseParserGroup.add_argument("--http-max-response-time-ms", type=int, help=argparse.SUPPRESS if suppressHelp else "Maximum time for processing a request, -1 for unlimited", default=-1)
        ptbBaseParserGroup.add_argument("--http-max-bytes-in-flight-mb", type=int, help=argparse.SUPPRESS if suppressHelp else "Maximum size in megabytes http_plugin should use for processing http requests. -1 for unlimited. 429\
                                         error response when exceeded.", default=-1)
        ptbBaseParserGroup.add_argument("--del-perf-logs", help=argparse.SUPPRESS if suppressHelp else "Whether to delete performance test specific logs.", action='store_true')
        ptbBaseParserGroup.add_argument("--del-report", help=argparse.SUPPRESS if suppressHelp else "Whether to delete overarching performance run report.", action='store_true')
        ptbBaseParserGroup.add_argument("--save-state", help=argparse.SUPPRESS if suppressHelp else "Whether to save node state. (Warning: large disk usage)", action='store_true')
        ptbBaseParserGroup.add_argument("--quiet", help=argparse.SUPPRESS if suppressHelp else "Whether to quiet printing intermediate results and reports to stdout", action='store_true')
        ptbBaseParserGroup.add_argument("--prods-enable-trace-api", help=argparse.SUPPRESS if suppressHelp else "Determines whether producer nodes should have eosio::trace_api_plugin enabled", action='store_true')
        ptbBaseParserGroup.add_argument("--print-missing-transactions", help=argparse.SUPPRESS if suppressHelp else "Toggles if missing transactions are be printed upon test completion.", action='store_true')
        ptbBaseParserGroup.add_argument("--account-name", type=str, help=argparse.SUPPRESS if suppressHelp else "Name of the account to create and assign a contract to", default="eosio")
        ptbBaseParserGroup.add_argument("--contract-dir", type=str, help=argparse.SUPPRESS if suppressHelp else "Path to contract dir", default="unittests/contracts/eosio.system")
        ptbBaseParserGroup.add_argument("--wasm-file", type=str, help=argparse.SUPPRESS if suppressHelp else "WASM file name for contract", default="eosio.system.wasm")
        ptbBaseParserGroup.add_argument("--abi-file", type=str, help=argparse.SUPPRESS if suppressHelp else "ABI file name for contract", default="eosio.system.abi")
        ptbBaseParserGroup.add_argument("--user-trx-data-file", type=str, help=argparse.SUPPRESS if suppressHelp else "Path to transaction data JSON file")
        ptbBaseParserGroup.add_argument("--wasm-runtime", type=str, help=argparse.SUPPRESS if suppressHelp else "Override default WASM runtime (\"eos-vm-jit\", \"eos-vm\")\
                                         \"eos-vm-jit\" : A WebAssembly runtime that compiles WebAssembly code to native x86 code prior to\
                                         execution. \"eos-vm\" : A WebAssembly interpreter.",
                                         choices=["eos-vm-jit", "eos-vm"], default="eos-vm-jit")
        ptbBaseParserGroup.add_argument("--contracts-console", help=argparse.SUPPRESS if suppressHelp else "print contract's output to console", action='store_true')
        ptbBaseParserGroup.add_argument("--eos-vm-oc-cache-size-mb", type=int, help=argparse.SUPPRESS if suppressHelp else "Maximum size (in MiB) of the EOS VM OC code cache", default=1024)
        ptbBaseParserGroup.add_argument("--eos-vm-oc-compile-threads", type=int, help=argparse.SUPPRESS if suppressHelp else "Number of threads to use for EOS VM OC tier-up", default=1)
        ptbBaseParserGroup.add_argument("--non-prods-eos-vm-oc-enable", help=argparse.SUPPRESS if suppressHelp else "Enable EOS VM OC tier-up runtime on non producer nodes", action='store_true')
        ptbBaseParserGroup.add_argument("--block-log-retain-blocks", type=int, help=argparse.SUPPRESS if suppressHelp else "If set to greater than 0, periodically prune the block log to\
                                         store only configured number of most recent blocks. If set to 0, no blocks are be written to the block log;\
                                         block log file is removed after startup.", default=None)
        ptbBaseParserGroup.add_argument("--http-threads", type=int, help=argparse.SUPPRESS if suppressHelp else "Number of worker threads in http thread pool", default=2)
        ptbBaseParserGroup.add_argument("--chain-state-db-size-mb", type=int, help=argparse.SUPPRESS if suppressHelp else "Maximum size (in MiB) of the chain state database", default=25600)
        ptbBaseParserGroup.add_argument("--trx-generator", type=str, help=argparse.SUPPRESS if suppressHelp else "Transaction Generator executable", default="./tests/trx_generator/trx_generator")

        return ptbBaseParser

    @staticmethod
    def createBaseBpP2pArgumentParser(suppressHelp: bool=False):
        return PtbArgumentsHandler._createBaseArgumentParser(defEndpointApiDef="p2p", defProdNodeCnt=1, defValidationNodeCnt=1, defApiNodeCnt=0, suppressHelp=suppressHelp)

    @staticmethod
    def createBaseApiHttpArgumentParser(suppressHelp: bool=False):
        return PtbArgumentsHandler._createBaseArgumentParser(defEndpointApiDef="http", defProdNodeCnt=1, defValidationNodeCnt=1, defApiNodeCnt=1, suppressHelp=suppressHelp)

    @staticmethod
    def createArgumentParser():
        ptbBaseParser = PtbArgumentsHandler.createBaseBpP2pArgumentParser()

        ptbParser = argparse.ArgumentParser(parents=[ptbBaseParser], add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptbGrpTitle="Performance Test Basic Single Test"
        ptbGrpDescription="Performance Test Basic single test configuration items. Useful for running a single test directly. \
                           These items may not be directly configurable from higher level scripts as the scripts themselves may configure these internally."
        ptbParserGroup = ptbParser.add_argument_group(title=ptbGrpTitle, description=ptbGrpDescription)

        ptbParserGroup.add_argument("--target-tps", type=int, help="The target transfers per second to send during test", default=8000)
        ptbParserGroup.add_argument("--test-duration-sec", type=int, help="The duration of transfer trx generation for the test in seconds", default=90)

        return ptbParser

    @staticmethod
    def parseArgs():
        ptbParser=PtbArgumentsHandler.createArgumentParser()
        args=ptbParser.parse_args()
        return args
