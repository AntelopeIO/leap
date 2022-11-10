import copy
import decimal
import subprocess
import time
import os
import re
import json
import signal

from datetime import datetime
from datetime import timedelta
from core_symbol import CORE_SYMBOL
from .queries import Queries, BlockType
from .transactions import Transactions
from .testUtils import Utils
from .testUtils import Account
from .testUtils import unhandledEnumType
from .testUtils import ReturnType

# pylint: disable=too-many-public-methods
class Node(Transactions):

    # pylint: disable=too-many-instance-attributes
    # pylint: disable=too-many-arguments
    def __init__(self, host, port, nodeId, pid=None, cmd=None, walletMgr=None):
        super().__init__(host, port, walletMgr)
        self.host=host
        self.port=port
        self.pid=pid
        self.cmd=cmd
        if nodeId != "bios":
            assert isinstance(nodeId, int)
        self.nodeId=nodeId
        if Utils.Debug: Utils.Print("new Node host=%s, port=%s, pid=%s, cmd=%s" % (self.host, self.port, self.pid, self.cmd))
        self.killed=False # marks node as killed

        self.infoValid=None
        self.lastRetrievedHeadBlockNum=None
        self.lastRetrievedLIB=None
        self.lastRetrievedHeadBlockProducer=""
        self.transCache={}
        self.missingTransaction=False
        self.popenProc=None           # initial process is started by launcher, this will only be set on relaunch
        self.lastTrackedTransactionId=None

    def __str__(self):
        return "Host: %s, Port:%d, NodeNum:%s, Pid:%s" % (self.host, self.port, self.nodeId, self.pid)

    @staticmethod
    def __printTransStructureError(trans, context):
        Utils.Print("ERROR: Failure in expected transaction structure. Missing trans%s." % (context))
        Utils.Print("Transaction: %s" % (json.dumps(trans, indent=1)))

    @staticmethod
    def stdinAndCheckOutput(cmd, subcommand):
        """Passes input to stdin, executes cmd. Returns tuple with return code(int), stdout(byte stream) and stderr(byte stream)."""
        assert(cmd)
        assert(isinstance(cmd, list))
        assert(subcommand)
        assert(isinstance(subcommand, str))
        outs=None
        errs=None
        ret=0
        try:
            popen=subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            outs,errs=popen.communicate(input=subcommand.encode("utf-8"))
            ret=popen.wait()
        except subprocess.CalledProcessError as ex:
            msg=ex.output
            return (ex.returncode, msg, None)

        return (ret, outs, errs)

    @staticmethod
    def normalizeJsonObject(extJStr):
        tmpStr=extJStr
        tmpStr=re.sub(r'ObjectId\("(\w+)"\)', r'"ObjectId-\1"', tmpStr)
        tmpStr=re.sub(r'ISODate\("([\w|\-|\:|\.]+)"\)', r'"ISODate-\1"', tmpStr)
        tmpStr=re.sub(r'NumberLong\("(\w+)"\)', r'"NumberLong-\1"', tmpStr)
        tmpStr=re.sub(r'NumberLong\((\w+)\)', r'\1', tmpStr)
        return tmpStr

    @staticmethod
    def byteArrToStr(arr):
        return arr.decode("utf-8")

    def validateAccounts(self, accounts):
        assert(accounts)
        assert(isinstance(accounts, list))

        for account in accounts:
            assert(account)
            assert(isinstance(account, Account))
            if Utils.Debug: Utils.Print("Validating account %s" % (account.name))
            accountInfo=self.getEosAccount(account.name, exitOnError=True)
            try:
                assert(accountInfo["account_name"] == account.name)
            except (AssertionError, TypeError, KeyError) as _:
                Utils.Print("account validation failed. account: %s" % (account.name))
                raise


    def killNodeOnProducer(self, producer, whereInSequence, blockType=BlockType.head, silentErrors=True, exitOnError=False, exitMsg=None, returnType=ReturnType.json):
        assert(isinstance(producer, str))
        assert(isinstance(whereInSequence, int))
        assert(isinstance(blockType, BlockType))
        assert(isinstance(returnType, ReturnType))
        basedOnLib="true" if blockType==BlockType.lib else "false"
        payload="{ \"producer\":\"%s\", \"where_in_sequence\":%d, \"based_on_lib\":\"%s\" }" % (producer, whereInSequence, basedOnLib)
        return self.processCurlCmd("test_control", "kill_node_on_producer", payload, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=exitMsg, returnType=returnType)

    def processCurlCmd(self, resource, command, payload, silentErrors=True, exitOnError=False, exitMsg=None, returnType=ReturnType.json):
        cmd="curl %s/v1/%s/%s -d '%s' -X POST -H \"Content-Type: application/json\"" % \
            (self.endpointHttp, resource, command, payload)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
        rtn=None
        start=time.perf_counter()
        try:
            if returnType==ReturnType.json:
                rtn=Utils.runCmdReturnJson(cmd, silentErrors=silentErrors)
            elif returnType==ReturnType.raw:
                rtn=Utils.runCmdReturnStr(cmd)
            else:
                unhandledEnumType(returnType)

            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
                printReturn=json.dumps(rtn) if returnType==ReturnType.json else rtn
                Utils.Print("cmd returned: %s" % (printReturn))
        except subprocess.CalledProcessError as ex:
            if not silentErrors:
                end=time.perf_counter()
                msg=ex.output.decode("utf-8")
                errorMsg="Exception during \"%s\". %s.  cmd Duration=%.3f sec." % (cmd, msg, end-start)
                if exitOnError:
                    Utils.cmdError(errorMsg)
                    Utils.errorExit(errorMsg)
                else:
                    Utils.Print("ERROR: %s" % (errorMsg))
            return None

        if exitMsg is not None:
            exitMsg=": " + exitMsg
        else:
            exitMsg=""
        if exitOnError and rtn is None:
            Utils.cmdError("could not \"%s\" - %s" % (cmd,exitMsg))
            Utils.errorExit("Failed to \"%s\"" % (cmd))

        return rtn

    def txnGenCreateTestAccounts(self, genAccount, genKey, silentErrors=True, exitOnError=False, exitMsg=None, returnType=ReturnType.json):
        assert(isinstance(genAccount, str))
        assert(isinstance(genKey, str))
        assert(isinstance(returnType, ReturnType))

        payload="[ \"%s\", \"%s\" ]" % (genAccount, genKey)
        return self.processCurlCmd("txn_test_gen", "create_test_accounts", payload, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=exitMsg, returnType=returnType)

    def txnGenStart(self, salt, period, batchSize, silentErrors=True, exitOnError=False, exitMsg=None, returnType=ReturnType.json):
        assert(isinstance(salt, str))
        assert(isinstance(period, int))
        assert(isinstance(batchSize, int))
        assert(isinstance(returnType, ReturnType))

        payload="[ \"%s\", %d, %d ]" % (salt, period, batchSize)
        return self.processCurlCmd("txn_test_gen", "start_generation", payload, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=exitMsg, returnType=returnType)

    def kill(self, killSignal):
        if Utils.Debug: Utils.Print("Killing node: %s" % (self.cmd))
        assert(self.pid is not None)
        try:
            if self.popenProc is not None:
               self.popenProc.send_signal(killSignal)
               self.popenProc.wait()
            else:
               os.kill(self.pid, killSignal)
        except OSError as ex:
            Utils.Print("ERROR: Failed to kill node (%s)." % (self.cmd), ex)
            return False

        # wait for kill validation
        def myFunc():
            try:
                os.kill(self.pid, 0) #check if process with pid is running
            except OSError as _:
                return True
            return False

        if not Utils.waitForBool(myFunc):
            Utils.Print("ERROR: Failed to validate node shutdown.")
            return False

        # mark node as killed
        self.pid=None
        self.killed=True
        return True

    def interruptAndVerifyExitStatus(self, timeout=60):
        if Utils.Debug: Utils.Print("terminating node: %s" % (self.cmd))
        assert self.popenProc is not None, f"node: '{self.cmd}' does not have a popenProc, this may be because it is only set after a relaunch."
        self.popenProc.send_signal(signal.SIGINT)
        try:
            outs, _ = self.popenProc.communicate(timeout=timeout)
            assert self.popenProc.returncode == 0, f"Expected terminating '{self.cmd}' to have an exit status of 0, but got {self.popenProc.returncode}"
        except subprocess.TimeoutExpired:
            Utils.errorExit("Terminate call failed on node: %s" % (self.cmd))

        # mark node as killed
        self.pid=None
        self.killed=True

    def verifyAlive(self, silent=False):
        logStatus=not silent and Utils.Debug
        pid=self.pid
        if logStatus: Utils.Print("Checking if node(pid=%s) is alive(killed=%s): %s" % (self.pid, self.killed, self.cmd))
        if self.killed or self.pid is None:
            self.killed=True
            self.pid=None
            return False

        try:
            os.kill(self.pid, 0)
        except ProcessLookupError as ex:
            # mark node as killed
            self.pid=None
            self.killed=True
            if logStatus: Utils.Print("Determined node(formerly pid=%s) is killed" % (pid))
            return False
        except PermissionError as ex:
            if logStatus: Utils.Print("Determined node(formerly pid=%s) is alive" % (pid))
            return True

        if logStatus: Utils.Print("Determined node(pid=%s) is alive" % (self.pid))
        return True

    # pylint: disable=too-many-locals
    # If nodeosPath is equal to None, it will use the existing nodeos path
    def relaunch(self, chainArg=None, newChain=False, skipGenesis=True, timeout=Utils.systemWaitTimeout, addSwapFlags=None, cachePopen=False, nodeosPath=None, waitForTerm=True):

        assert(self.pid is None)
        assert(self.killed)

        if Utils.Debug: Utils.Print("Launching node process, Id: {}".format(self.nodeId))

        cmdArr=[]
        splittedCmd=self.cmd.split()
        if nodeosPath: splittedCmd[0] = nodeosPath
        myCmd=" ".join(splittedCmd)
        toAddOrSwap=copy.deepcopy(addSwapFlags) if addSwapFlags is not None else {}
        if not newChain:
            skip=False
            swapValue=None
            for i in splittedCmd:
                Utils.Print("\"%s\"" % (i))
                if skip:
                    skip=False
                    continue
                if skipGenesis and ("--genesis-json" == i or "--genesis-timestamp" == i):
                    skip=True
                    continue

                if swapValue is None:
                    cmdArr.append(i)
                else:
                    cmdArr.append(swapValue)
                    swapValue=None

                if i in toAddOrSwap:
                    swapValue=toAddOrSwap[i]
                    del toAddOrSwap[i]
            for k,v in toAddOrSwap.items():
                cmdArr.append(k)
                cmdArr.append(v)
            myCmd=" ".join(cmdArr)

        cmd=myCmd + ("" if chainArg is None else (" " + chainArg))
        self.launchCmd(cmd, cachePopen)

        def isNodeAlive():
            """wait for node to be responsive."""
            try:
                return True if self.checkPulse() else False
            except (TypeError) as _:
                pass
            return False

        def didNodeExitGracefully(popen, timeout):
            try:
                popen.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                return False
            with open(popen.errfile.name, 'r') as f:
                if "Reached configured maximum block 10; terminating" in f.read():
                    return True
                else:
                    return False

        if "terminate-at-block" not in cmd or not waitForTerm:
            isAlive=Utils.waitForBool(isNodeAlive, timeout, sleepTime=1)
        else:
            isAlive=Utils.waitForBoolWithArg(didNodeExitGracefully, self.popenProc, timeout, sleepTime=1)
        if isAlive:
            Utils.Print("Node relaunch was successful.")
        else:
            Utils.Print("ERROR: Node relaunch Failed.")
            # Ensure the node process is really killed
            if self.popenProc:
                self.popenProc.send_signal(signal.SIGTERM)
                self.popenProc.wait()
            self.pid=None
            return False

        self.cmd=cmd
        self.killed=False
        return True

    @staticmethod
    def unstartedFile(nodeId):
        assert(isinstance(nodeId, int))
        startFile=Utils.getNodeDataDir(nodeId, "start.cmd")
        if not os.path.exists(startFile):
            Utils.errorExit("Cannot find unstarted node since %s file does not exist" % startFile)
        return startFile

    def launchUnstarted(self, cachePopen=False):
        Utils.Print("launchUnstarted cmd: %s" % (self.cmd))
        self.launchCmd(self.cmd, cachePopen)

    def launchCmd(self, cmd, cachePopen=False):
        dataDir=Utils.getNodeDataDir(self.nodeId)
        dt = datetime.now()
        dateStr=Utils.getDateString(dt)
        stdoutFile="%s/stdout.%s.txt" % (dataDir, dateStr)
        stderrFile="%s/stderr.%s.txt" % (dataDir, dateStr)
        with open(stdoutFile, 'w') as sout, open(stderrFile, 'w') as serr:
            Utils.Print("cmd: %s" % (cmd))
            popen=subprocess.Popen(cmd.split(), stdout=sout, stderr=serr)
            if cachePopen:
                popen.outfile=sout
                popen.errfile=serr
                self.popenProc=popen
            self.pid=popen.pid
            if Utils.Debug: Utils.Print("start Node host=%s, port=%s, pid=%s, cmd=%s" % (self.host, self.port, self.pid, self.cmd))

    def trackCmdTransaction(self, trans, ignoreNonTrans=False, reportStatus=True):
        if trans is None:
            if Utils.Debug: Utils.Print("  cmd returned transaction: %s" % (trans))
            return

        if ignoreNonTrans and not Queries.isTrans(trans):
            if Utils.Debug: Utils.Print("  cmd returned a non-transaction: %s" % (trans))
            return

        transId=Queries.getTransId(trans)
        self.lastTrackedTransactionId=transId
        if transId in self.transCache.keys():
            replaceMsg="replacing previous trans=\n%s" % json.dumps(self.transCache[transId], indent=2, sort_keys=True)
        else:
            replaceMsg=""

        if Utils.Debug and reportStatus:
            status=Queries.getTransStatus(trans)
            blockNum=Queries.getTransBlockNum(trans)
            Utils.Print("  cmd returned transaction id: %s, status: %s, (possible) block num: %s %s" % (transId, status, blockNum, replaceMsg))
        elif Utils.Debug:
            Utils.Print("  cmd returned transaction id: %s %s" % (transId, replaceMsg))

        self.transCache[transId]=trans

    def getLastTrackedTransactionId(self):
        return self.lastTrackedTransactionId

    def reportStatus(self):
        Utils.Print("Node State:")
        Utils.Print(" cmd   : %s" % (self.cmd))
        self.verifyAlive(silent=True)
        Utils.Print(" killed: %s" % (self.killed))
        Utils.Print(" host  : %s" % (self.host))
        Utils.Print(" port  : %s" % (self.port))
        Utils.Print(" pid   : %s" % (self.pid))
        status="last getInfo returned None" if not self.infoValid else "at last call to getInfo"
        Utils.Print(" hbn   : %s (%s)" % (self.lastRetrievedHeadBlockNum, status))
        Utils.Print(" lib   : %s (%s)" % (self.lastRetrievedLIB, status))

    # Require producer_api_plugin
    def scheduleProtocolFeatureActivations(self, featureDigests=[]):
        param = { "protocol_features_to_activate": featureDigests }
        self.processCurlCmd("producer", "schedule_protocol_feature_activations", json.dumps(param))

    def modifyBuiltinPFSubjRestrictions(self, featureCodename, subjectiveRestriction={}):
        jsonPath = os.path.join(Utils.getNodeConfigDir(self.nodeId),
                                "protocol_features",
                                "BUILTIN-{}.json".format(featureCodename))
        protocolFeatureJson = []
        with open(jsonPath) as f:
            protocolFeatureJson = json.load(f)
        protocolFeatureJson["subjective_restrictions"].update(subjectiveRestriction)
        with open(jsonPath, "w") as f:
            json.dump(protocolFeatureJson, f, indent=2)

    # Require producer_api_plugin
    def createSnapshot(self):
        param = { }
        return self.processCurlCmd("producer", "create_snapshot", json.dumps(param))

    # kill all existing nodeos in case lingering from previous test
    @staticmethod
    def killAllNodeos():
        # kill the eos server
        cmd="pkill -9 %s" % (Utils.EosServerName)
        ret_code = subprocess.call(cmd.split(), stdout=Utils.FNull)
        Utils.Print("cmd: %s, ret:%d" % (cmd, ret_code))

    @staticmethod
    def findStderrFiles(path):
        files=[]
        it=os.scandir(path)
        for entry in it:
            if entry.is_file(follow_symlinks=False):
                match=re.match("stderr\..+\.txt", entry.name)
                if match:
                    files.append(os.path.join(path, entry.name))
        files.sort()
        return files

    def analyzeProduction(self, specificBlockNum=None, thresholdMs=500):
        dataDir=Utils.getNodeDataDir(self.nodeId)
        files=Node.findStderrFiles(dataDir)
        blockAnalysis={}
        anyBlockStr=r'[0-9]+'
        initialTimestamp=r'\s+([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3})\s'
        producedBlockPreStr=r'.+Produced\sblock\s+.+\s#('
        producedBlockPostStr=r')\s@\s([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}.[0-9]{3})'
        anyBlockPtrn=re.compile(initialTimestamp + producedBlockPreStr + anyBlockStr + producedBlockPostStr)
        producedBlockPtrn=re.compile(initialTimestamp + producedBlockPreStr + str(specificBlockNum) + producedBlockPostStr) if specificBlockNum is not None else anyBlockPtrn
        producedBlockDonePtrn=re.compile(initialTimestamp + r'.+Producing\sBlock\s+#' + anyBlockStr + '\sreturned:\strue')
        for file in files:
            with open(file, 'r') as f:
                line = f.readline()
                while line:
                    readLine=True  # assume we need to read the next line before the next pass
                    match = producedBlockPtrn.search(line)
                    if match:
                        prodTimeStr = match.group(1)
                        slotTimeStr = match.group(3)
                        blockNum = int(match.group(2))

                        line = f.readline()
                        while line:
                            matchNextBlock = anyBlockPtrn.search(line)
                            if matchNextBlock:
                                readLine=False  #already have the next line ready to check on next pass
                                break

                            matchBlockActuallyProduced = producedBlockDonePtrn.search(line)
                            if matchBlockActuallyProduced:
                                prodTimeStr = matchBlockActuallyProduced.group(1)
                                break

                            line = f.readline()

                        prodTime = datetime.strptime(prodTimeStr, Utils.TimeFmt)
                        slotTime = datetime.strptime(slotTimeStr, Utils.TimeFmt)
                        delta = prodTime - slotTime
                        limit = timedelta(milliseconds=thresholdMs)
                        if delta > limit:
                            if blockNum in blockAnalysis:
                                Utils.errorExit("Found repeat production of the same block num: %d in one of the stderr files in: %s" % (blockNum, dataDir))
                            blockAnalysis[blockNum] = { "slot": slotTimeStr, "prod": prodTimeStr }

                        if specificBlockNum is not None:
                            return blockAnalysis

                    if readLine:
                        line = f.readline()

        if specificBlockNum is not None and specificBlockNum not in blockAnalysis:
            blockAnalysis[specificBlockNum] = { "slot": None, "prod": None}

        return blockAnalysis
