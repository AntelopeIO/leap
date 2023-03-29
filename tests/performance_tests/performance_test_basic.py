#!/usr/bin/env python3

import argparse
import dataclasses
import os
import re
import sys
import shutil
import signal
import json
import log_reader
import inspect
import traceback

from pathlib import Path, PurePath
sys.path.append(str(PurePath(PurePath(Path(__file__).absolute()).parent).parent))

from NodeosPluginArgs import ChainPluginArgs, HttpPluginArgs, NetPluginArgs, ProducerPluginArgs, ResourceMonitorPluginArgs, SignatureProviderPluginArgs, StateHistoryPluginArgs, TraceApiPluginArgs
from TestHarness import Account, Cluster, TestHelper, Utils, WalletMgr, TransactionGeneratorsLauncher, TpsTrxGensConfig
from TestHarness.TestHelper import AppArgs
from dataclasses import dataclass, asdict, field
from datetime import datetime
from pathlib import Path

class PerformanceTestBasic:
    @dataclass
    class PtbTpsTestResult:
        completedRun: bool = False
        numGeneratorsUsed: int = 0
        targetTpsPerGenList: list = field(default_factory=list)
        trxGenExitCodes: list = field(default_factory=list)

    @dataclass
    class TestHelperConfig:
        killAll: bool = True # clean_run
        dontKill: bool = False # leave_running
        keepLogs: bool = True
        dumpErrorDetails: bool = False
        delay: int = 1
        nodesFile: str = None
        verbose: bool = False
        unshared: bool = False
        _killEosInstances: bool = True
        _killWallet: bool = True

        def __post_init__(self):
            self._killEosInstances = not self.dontKill
            self._killWallet = not self.dontKill

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

        pnodes: int = 1
        totalNodes: int = 2
        topo: str = "mesh"
        extraNodeosArgs: ExtraNodeosArgs = field(default_factory=ExtraNodeosArgs)
        specifiedContract: SpecifiedContract = field(default_factory=SpecifiedContract)
        genesisPath: Path = Path("tests")/"performance_tests"/"genesis.json"
        maximumP2pPerHost: int = 5000
        maximumClients: int = 0
        loggingLevel: str = "info"
        loggingDict: dict = field(default_factory=lambda: { "bios": "off" })
        prodsEnableTraceApi: bool = False
        nodeosVers: str = ""
        specificExtraNodeosArgs: dict = field(default_factory=dict)
        _totalNodes: int = 2
        nonProdsEosVmOcEnable: bool = False

        def log_transactions(self, trxDataFile, block):
            for trx in block['payload']['transactions']:
                for actions in trx['actions']:
                    if actions['account'] != 'eosio' or actions['action'] != 'onblock':
                        trxDataFile.write(f"{trx['id']},{trx['block_num']},{trx['block_time']},{trx['cpu_usage_us']},{trx['net_usage_words']},{trx['actions']}\n")

        def __post_init__(self):
            self._totalNodes = self.pnodes + 1 if self.totalNodes <= self.pnodes else self.totalNodes
            nonProdsSpecificNodeosStr = ""
            if not self.prodsEnableTraceApi:
                nonProdsSpecificNodeosStr += "--plugin eosio::trace_api_plugin "
            if self.nonProdsEosVmOcEnable:
                nonProdsSpecificNodeosStr += "--eos-vm-oc-enable "
            self.specificExtraNodeosArgs.update({f"{node}" : nonProdsSpecificNodeosStr for node in range(self.pnodes, self._totalNodes)})
            assert self.nodeosVers != "v1" and self.nodeosVers != "v0", f"nodeos version {Utils.getNodeosVersion().split('.')[0]} is unsupported by performance test"
            if self.nodeosVers == "v2":
                self.writeTrx = lambda trxDataFile, block, blockNum: [trxDataFile.write(f"{trx['trx']['id']},{blockNum},{trx['cpu_usage_us']},{trx['net_usage_words']}\n") for trx in block['payload']['transactions'] if block['payload']['transactions']]
                self.writeBlock = lambda blockDataFile, block: blockDataFile.write(f"{block['payload']['block_num']},{block['payload']['id']},{block['payload']['producer']},{block['payload']['confirmed']},{block['payload']['timestamp']}\n")
                self.specificExtraNodeosArgs.update({f"{node}" : '--plugin eosio::history_api_plugin --filter-on "*"' for node in range(self.pnodes, self._totalNodes)})
            else:
                self.writeTrx = lambda trxDataFile, block, blockNum:[ self.log_transactions(trxDataFile, block) ]
                self.writeBlock = lambda blockDataFile, block: blockDataFile.write(f"{block['payload']['number']},{block['payload']['id']},{block['payload']['producer']},{block['payload']['status']},{block['payload']['timestamp']}\n")

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

        def __post_init__(self):
            self.expectedTransactionsSent = self.testTrxGenDurationSec * self.targetTps

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

        self.testHelperConfig.keepLogs = not self.ptbConfig.delPerfLogs

        Utils.Debug = self.testHelperConfig.verbose
        self.errorExit = Utils.errorExit
        self.emptyBlockGoal = 1

        self.testStart = datetime.utcnow()
        self.testNamePath = testNamePath
        self.loggingConfig = PerformanceTestBasic.LoggingConfig(logDirBase=Path(self.ptbConfig.logDirRoot)/self.testNamePath,
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

        # Setup Expectations for Producer and Validation Node IDs
        # Producer Nodes are index [0, pnodes) and validation nodes/non-producer nodes [pnodes, _totalNodes)
        # Use first producer node and first non-producer node
        self.producerNodeId = 0
        self.validationNodeId = self.clusterConfig.pnodes
        pid = os.getpid()
        self.nodeosLogPath = Path(self.loggingConfig.logDirPath)/"var"/f"{self.testNamePath}{pid}"/f"node_{str(self.validationNodeId).zfill(2)}"/"stderr.txt"

        # Setup cluster and its wallet manager
        self.walletMgr=WalletMgr(True)
        self.cluster=Cluster(walletd=True, loggingLevel=self.clusterConfig.loggingLevel, loggingLevelDict=self.clusterConfig.loggingDict,
                             nodeosVers=self.clusterConfig.nodeosVers,unshared=self.testHelperConfig.unshared)
        self.cluster.setWalletMgr(self.walletMgr)

    def cleanupOldClusters(self):
        self.cluster.killall(allInstances=self.testHelperConfig.killAll)
        self.cluster.cleanup()

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

    def queryBlockTrxData(self, node, blockDataPath, blockTrxDataPath, startBlockNum, endBlockNum):
        for blockNum in range(startBlockNum, endBlockNum):
            block = node.fetchBlock(blockNum)
            btdf_append_write = self.fileOpenMode(blockTrxDataPath)
            with open(blockTrxDataPath, btdf_append_write) as trxDataFile:
                self.clusterConfig.writeTrx(trxDataFile, block, blockNum)

            bdf_append_write = self.fileOpenMode(blockDataPath)
            with open(blockDataPath, bdf_append_write) as blockDataFile:
                self.clusterConfig.writeBlock(blockDataFile, block)

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
            pnodes=self.clusterConfig.pnodes,
            totalNodes=self.clusterConfig._totalNodes,
            topo=self.clusterConfig.topo,
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
        for producer in range(0, self.clusterConfig.pnodes):
            self.connectionPairList.append(f"{self.cluster.getNode(producer).host}:{self.cluster.getNodeP2pPort(producer)}")
        self.validationNode = self.cluster.getNode(self.validationNodeId)
        self.wallet = self.walletMgr.create('default')
        self.setupContract()
        info = self.producerNode.getInfo()
        chainId = info['chain_id']
        lib_id = info['last_irreversible_block_id']
        self.data = log_reader.chainData()

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

        self.cluster.trxGenLauncher = TransactionGeneratorsLauncher(chainId=chainId, lastIrreversibleBlockId=lib_id, contractOwnerAccount=self.clusterConfig.specifiedContract.account.name,
                                                       accts=','.join(map(str, self.accountNames)), privateKeys=','.join(map(str, self.accountPrivKeys)),
                                                       trxGenDurationSec=self.ptbConfig.testTrxGenDurationSec, logDir=self.trxGenLogDirPath,
                                                       abiFile=abiFile, actionsData=actionsDataJson, actionsAuths=actionsAuthsJson,
                                                       tpsTrxGensConfig=tpsTrxGensConfig)

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
        log_reader.scrapeTrxGenTrxSentDataLogs(trxSent, self.trxGenLogDirPath, self.ptbConfig.quiet)
        if len(trxSent) != self.ptbConfig.expectedTransactionsSent:
            print(f"ERROR: Transactions generated: {len(trxSent)} does not match the expected number of transactions: {self.ptbConfig.expectedTransactionsSent}")
        blocksToWait = 2 * self.ptbConfig.testTrxGenDurationSec + 10
        trxSent = self.validationNode.waitForTransactionsInBlockRange(trxSent, self.data.startBlock, blocksToWait)
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
            pid = os.getpid()
            shutil.move(f"{self.cluster.nodeosLogPath}", f"{self.varLogsDirPath}")
        except Exception as e:
            print(f"Failed to move '{self.cluster.nodeosLogPath}' to '{self.varLogsDirPath}': {type(e)}: {e}")

        etcEosioDir = Path("etc")/"eosio"
        for path in os.listdir(etcEosioDir):
            if path == "launcher":
                try:
                    # Need to copy here since testnet.template is only generated at compile time then reused, therefore
                    # it needs to remain in etc/eosio/launcher for subsequent tests.
                    shutil.copytree(etcEosioDir/Path(path), self.etcEosioLogsDirPath/Path(path))
                except Exception as e:
                    print(f"Failed to copy '{etcEosioDir}/{path}' to '{self.etcEosioLogsDirPath}/{path}': {type(e)}: {e}")
            else:
                try:
                    shutil.move(etcEosioDir/Path(path), self.etcEosioLogsDirPath/Path(path))
                except Exception as e:
                    print(f"Failed to move '{etcEosioDir}/{path}' to '{self.etcEosioLogsDirPath}/{path}': {type(e)}: {e}")


    def analyzeResultsAndReport(self, testResult: PtbTpsTestResult):
        args = self.prepArgs()
        artifactsLocate = log_reader.ArtifactPaths(nodeosLogPath=self.nodeosLogPath, trxGenLogDirPath=self.trxGenLogDirPath, blockTrxDataPath=self.blockTrxDataPath,
                                                   blockDataPath=self.blockDataPath, transactionMetricsDataPath=self.transactionMetricsDataPath)
        tpsTestConfig = log_reader.TpsTestConfig(targetTps=self.ptbConfig.targetTps, testDurationSec=self.ptbConfig.testTrxGenDurationSec, tpsLimitPerGenerator=self.ptbConfig.tpsLimitPerGenerator,
                                                 numBlocksToPrune=self.ptbConfig.numAddlBlocksToPrune, numTrxGensUsed=testResult.numGeneratorsUsed,
                                                 targetTpsPerGenList=testResult.targetTpsPerGenList, quiet=self.ptbConfig.quiet)
        self.report = log_reader.calcAndReport(data=self.data, tpsTestConfig=tpsTestConfig, artifacts=artifactsLocate, argsDict=args, testStart=self.testStart,
                                               completedRun=testResult.completedRun,nodeosVers=self.clusterConfig.nodeosVers)

        jsonReport = None
        if not self.ptbConfig.quiet or not self.ptbConfig.delReport:
            jsonReport = log_reader.reportAsJSON(self.report)

        if not self.ptbConfig.quiet:
            print(self.data)

            print(f"Report:\n{jsonReport}")

        if not self.ptbConfig.delReport:
            log_reader.exportReportAsJSON(jsonReport, self.reportPath)

    def preTestSpinup(self):
        self.cleanupOldClusters()
        self.testDirsCleanup()
        self.testDirsSetup()

        if self.launchCluster() == False:
            self.errorExit('Failed to stand up cluster.')

    def postTpsTestSteps(self):
        self.queryBlockTrxData(self.validationNode, self.blockDataPath, self.blockTrxDataPath, self.data.startBlock, self.data.ceaseBlock)

    def runTest(self) -> bool:
        testSuccessful = False

        try:
            # Kill any existing instances and launch cluster
            TestHelper.printSystemInfo("BEGIN")
            self.preTestSpinup()

            self.ptbTestResult = self.runTpsTest()

            self.postTpsTestSteps()

            self.captureLowLevelArtifacts()
            self.analyzeResultsAndReport(self.ptbTestResult)

            testSuccessful = self.ptbTestResult.completedRun

            if not self.PtbTpsTestResult.completedRun:
                for exitCode in self.ptbTestResult.trxGenExitCodes:
                    if exitCode != 0:
                        print(f"Error: Transaction Generator exited with error {exitCode}")

            if testSuccessful and self.ptbConfig.expectedTransactionsSent != self.data.totalTransactions:
                testSuccessful = False
                print(f"Error: Transactions received: {self.data.totalTransactions} did not match expected total: {self.ptbConfig.expectedTransactionsSent}")

        except:
            traceback.print_exc()

        finally:
            # Despite keepLogs being hardcoded to False, logs will still appear on test failure in TestLogs
            # due to testSuccessful being False
            TestHelper.shutdown(
                cluster=self.cluster,
                walletMgr=self.walletMgr,
                testSuccessful=testSuccessful,
                killEosInstances=self.testHelperConfig._killEosInstances,
                killWallet=self.testHelperConfig._killWallet,
                keepLogs=False,
                cleanRun=self.testHelperConfig.killAll,
                dumpErrorDetails=self.testHelperConfig.dumpErrorDetails
                )

            if self.ptbConfig.delPerfLogs:
                print(f"Cleaning up logs directory: {self.loggingConfig.logDirPath}")
                self.testDirsCleanup(self.ptbConfig.delReport)

            return testSuccessful

class PtbArgumentsHandler(object):
    @staticmethod
    def createBaseArgumentParser():
        testHelperArgParser=TestHelper.createArgumentParser(includeArgs={"-p","-n","-d","-s","--nodes-file"
                                                        ,"--dump-error-details","-v","--leave-running"
                                                        ,"--clean-run","--unshared"})
        ptbBaseParser = argparse.ArgumentParser(parents=[testHelperArgParser], add_help=False, formatter_class=argparse.ArgumentDefaultsHelpFormatter)

        ptbBaseGrpTitle="Performance Test Basic Base"
        ptbBaseGrpDescription="Performance Test Basic base configuration items."
        ptbBaseParserGroup = ptbBaseParser.add_argument_group(title=ptbBaseGrpTitle, description=ptbBaseGrpDescription)

        ptbBaseParserGroup.add_argument("--tps-limit-per-generator", type=int, help="Maximum amount of transactions per second a single generator can have.", default=4000)
        ptbBaseParserGroup.add_argument("--genesis", type=str, help="Path to genesis.json", default="tests/performance_tests/genesis.json")
        ptbBaseParserGroup.add_argument("--num-blocks-to-prune", type=int, help=("The number of potentially non-empty blocks, in addition to leading and trailing size 0 blocks, "
                                                                "to prune from the beginning and end of the range of blocks of interest for evaluation."), default=2)
        ptbBaseParserGroup.add_argument("--signature-cpu-billable-pct", type=int, help="Percentage of actual signature recovery cpu to bill. Whole number percentages, e.g. 50 for 50%%", default=0)
        ptbBaseParserGroup.add_argument("--chain-threads", type=int, help="Number of worker threads in controller thread pool", default=2)
        ptbBaseParserGroup.add_argument("--database-map-mode", type=str, help="Database map mode (\"mapped\", \"heap\", or \"locked\"). \
                                                                In \"mapped\" mode database is memory mapped as a file. \
                                                                In \"heap\" mode database is preloaded in to swappable memory and will use huge pages if available. \
                                                                In \"locked\" mode database is preloaded, locked in to memory, and will use huge pages if available.",
                                                                choices=["mapped", "heap", "locked"], default="mapped")
        ptbBaseParserGroup.add_argument("--cluster-log-lvl", type=str, help="Cluster log level (\"all\", \"debug\", \"info\", \"warn\", \"error\", or \"off\"). \
                                                                Performance Harness Test Basic relies on some logging at \"info\" level, so it is recommended lowest logging level to use. \
                                                                However, there are instances where more verbose logging can be useful.",
                                                                choices=["all", "debug", "info", "warn", "error", "off"], default="info")
        ptbBaseParserGroup.add_argument("--net-threads", type=int, help="Number of worker threads in net_plugin thread pool", default=4)
        ptbBaseParserGroup.add_argument("--disable-subjective-billing", type=bool, help="Disable subjective CPU billing for API/P2P transactions", default=True)
        ptbBaseParserGroup.add_argument("--last-block-time-offset-us", type=int, help="Offset of last block producing time in microseconds. Valid range 0 .. -block_time_interval.", default=0)
        ptbBaseParserGroup.add_argument("--produce-time-offset-us", type=int, help="Offset of non last block producing time in microseconds. Valid range 0 .. -block_time_interval.", default=0)
        ptbBaseParserGroup.add_argument("--cpu-effort-percent", type=int, help="Percentage of cpu block production time used to produce block. Whole number percentages, e.g. 80 for 80%%", default=100)
        ptbBaseParserGroup.add_argument("--last-block-cpu-effort-percent", type=int, help="Percentage of cpu block production time used to produce last block. Whole number percentages, e.g. 80 for 80%%", default=100)
        ptbBaseParserGroup.add_argument("--producer-threads", type=int, help="Number of worker threads in producer thread pool", default=2)
        ptbBaseParserGroup.add_argument("--http-max-response-time-ms", type=int, help="Maximum time for processing a request, -1 for unlimited", default=-1)
        ptbBaseParserGroup.add_argument("--http-max-bytes-in-flight-mb", type=int, help="Maximum size in megabytes http_plugin should use for processing http requests. -1 for unlimited. 429\
                                         error response when exceeded.", default=-1)
        ptbBaseParserGroup.add_argument("--del-perf-logs", help="Whether to delete performance test specific logs.", action='store_true')
        ptbBaseParserGroup.add_argument("--del-report", help="Whether to delete overarching performance run report.", action='store_true')
        ptbBaseParserGroup.add_argument("--quiet", help="Whether to quiet printing intermediate results and reports to stdout", action='store_true')
        ptbBaseParserGroup.add_argument("--prods-enable-trace-api", help="Determines whether producer nodes should have eosio::trace_api_plugin enabled", action='store_true')
        ptbBaseParserGroup.add_argument("--print-missing-transactions", help="Toggles if missing transactions are be printed upon test completion.", action='store_true')
        ptbBaseParserGroup.add_argument("--account-name", type=str, help="Name of the account to create and assign a contract to", default="eosio")
        ptbBaseParserGroup.add_argument("--contract-dir", type=str, help="Path to contract dir", default="unittests/contracts/eosio.system")
        ptbBaseParserGroup.add_argument("--wasm-file", type=str, help="WASM file name for contract", default="eosio.system.wasm")
        ptbBaseParserGroup.add_argument("--abi-file", type=str, help="ABI file name for contract", default="eosio.system.abi")
        ptbBaseParserGroup.add_argument("--user-trx-data-file", type=str, help="Path to transaction data JSON file")
        ptbBaseParserGroup.add_argument("--wasm-runtime", type=str, help="Override default WASM runtime (\"eos-vm-jit\", \"eos-vm\")\
                                         \"eos-vm-jit\" : A WebAssembly runtime that compiles WebAssembly code to native x86 code prior to\
                                         execution. \"eos-vm\" : A WebAssembly interpreter.",
                                         choices=["eos-vm-jit", "eos-vm"], default="eos-vm-jit")
        ptbBaseParserGroup.add_argument("--contracts-console", help="print contract's output to console", action='store_true')
        ptbBaseParserGroup.add_argument("--eos-vm-oc-cache-size-mb", type=int, help="Maximum size (in MiB) of the EOS VM OC code cache", default=1024)
        ptbBaseParserGroup.add_argument("--eos-vm-oc-compile-threads", type=int, help="Number of threads to use for EOS VM OC tier-up", default=1)
        ptbBaseParserGroup.add_argument("--non-prods-eos-vm-oc-enable", help="Enable EOS VM OC tier-up runtime on non producer nodes", action='store_true')
        ptbBaseParserGroup.add_argument("--block-log-retain-blocks", type=int, help="If set to greater than 0, periodically prune the block log to\
                                         store only configured number of most recent blocks. If set to 0, no blocks are be written to the block log;\
                                         block log file is removed after startup.", default=None)
        ptbBaseParserGroup.add_argument("--http-threads", type=int, help="Number of worker threads in http thread pool", default=2)
        ptbBaseParserGroup.add_argument("--chain-state-db-size-mb", type=int, help="Maximum size (in MiB) of the chain state database", default=25600)

        return ptbBaseParser

    @staticmethod
    def createArgumentParser():
        ptbBaseParser = PtbArgumentsHandler.createBaseArgumentParser()

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

def main():

    args = PtbArgumentsHandler.parseArgs()
    Utils.Debug = args.v

    testHelperConfig = PerformanceTestBasic.TestHelperConfig(killAll=args.clean_run, dontKill=args.leave_running, keepLogs=not args.del_perf_logs,
                                                             dumpErrorDetails=args.dump_error_details, delay=args.d, nodesFile=args.nodes_file, verbose=args.v, unshared=args.unshared)

    chainPluginArgs = ChainPluginArgs(signatureCpuBillablePct=args.signature_cpu_billable_pct,
                                      chainThreads=args.chain_threads, databaseMapMode=args.database_map_mode,
                                      wasmRuntime=args.wasm_runtime, contractsConsole=args.contracts_console,
                                      eosVmOcCacheSizeMb=args.eos_vm_oc_cache_size_mb, eosVmOcCompileThreads=args.eos_vm_oc_compile_threads,
                                      blockLogRetainBlocks=args.block_log_retain_blocks,
                                      chainStateDbSizeMb=args.chain_state_db_size_mb, abiSerializerMaxTimeMs=990000)

    lbto = args.last_block_time_offset_us
    lbcep = args.last_block_cpu_effort_percent
    if args.p > 1 and lbto == 0 and lbcep == 100:
        print("Overriding defaults for last_block_time_offset_us and last_block_cpu_effort_percent to ensure proper production windows.")
        lbto = -200000
        lbcep = 80
    producerPluginArgs = ProducerPluginArgs(disableSubjectiveBilling=args.disable_subjective_billing,
                                            lastBlockTimeOffsetUs=lbto, produceTimeOffsetUs=args.produce_time_offset_us,
                                            cpuEffortPercent=args.cpu_effort_percent, lastBlockCpuEffortPercent=lbcep,
                                            producerThreads=args.producer_threads, maxTransactionTime=-1)
    httpPluginArgs = HttpPluginArgs(httpMaxResponseTimeMs=args.http_max_response_time_ms, httpMaxBytesInFlightMb=args.http_max_bytes_in_flight_mb,
                                    httpThreads=args.http_threads)
    netPluginArgs = NetPluginArgs(netThreads=args.net_threads, maxClients=0)
    nodeosVers=Utils.getNodeosVersion().split('.')[0]
    resourceMonitorPluginArgs = ResourceMonitorPluginArgs(resourceMonitorNotShutdownOnThresholdExceeded=not nodeosVers == "v2")
    ENA = PerformanceTestBasic.ClusterConfig.ExtraNodeosArgs
    extraNodeosArgs = ENA(chainPluginArgs=chainPluginArgs, httpPluginArgs=httpPluginArgs, producerPluginArgs=producerPluginArgs, netPluginArgs=netPluginArgs,
                          resourceMonitorPluginArgs=resourceMonitorPluginArgs)
    SC = PerformanceTestBasic.ClusterConfig.SpecifiedContract
    specifiedContract=SC(contractDir=args.contract_dir, wasmFile=args.wasm_file, abiFile=args.abi_file, account=Account(args.account_name))
    testClusterConfig = PerformanceTestBasic.ClusterConfig(pnodes=args.p, totalNodes=args.n, topo=args.s, genesisPath=args.genesis,
                                                           prodsEnableTraceApi=args.prods_enable_trace_api, extraNodeosArgs=extraNodeosArgs,
                                                           specifiedContract=specifiedContract, loggingLevel=args.cluster_log_lvl,
                                                           nodeosVers=nodeosVers, nonProdsEosVmOcEnable=args.non_prods_eos_vm_oc_enable)

    if args.contracts_console and testClusterConfig.loggingLevel != "debug" and testClusterConfig.loggingLevel != "all":
        print("Enabling contracts-console will not print anything unless debug level is 'debug' or higher."
              f" Current debug level is: {testClusterConfig.loggingLevel}")
    ptbConfig = PerformanceTestBasic.PtbConfig(targetTps=args.target_tps, testTrxGenDurationSec=args.test_duration_sec, tpsLimitPerGenerator=args.tps_limit_per_generator,
                                  numAddlBlocksToPrune=args.num_blocks_to_prune, logDirRoot=".", delReport=args.del_report, quiet=args.quiet, delPerfLogs=args.del_perf_logs,
                                  printMissingTransactions=args.print_missing_transactions,
                                  userTrxDataFile=Path(args.user_trx_data_file) if args.user_trx_data_file is not None else None)
    myTest = PerformanceTestBasic(testHelperConfig=testHelperConfig, clusterConfig=testClusterConfig, ptbConfig=ptbConfig)

    testSuccessful = myTest.runTest()

    exitCode = 0 if testSuccessful else 1
    exit(exitCode)

if __name__ == '__main__':
    main()
