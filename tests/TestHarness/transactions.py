#!/usr/bin/env python3

import json
import subprocess
import time

from .core_symbol import CORE_SYMBOL
from .depresolver import dep
from .queries import NodeosQueries
from .accounts import Account
from .testUtils import Utils

class Transactions(NodeosQueries):
    def __init__(self, host, port, walletMgr=None):
        super().__init__(host, port, walletMgr)

    # Create & initialize account and return creation transactions. Return transaction json object
    def createInitializeAccount(self, account, creatorAccount, stakedDeposit=1000, waitForTransBlock=False, stakeNet=100, stakeCPU=100, buyRAM=10000, exitOnError=False, sign=False, additionalArgs=''):
        signStr = NodeosQueries.sign_str(sign, [ creatorAccount.activePublicKey ])
        cmdDesc="system newaccount"
        cmd=(f'{cmdDesc} -j {signStr} {creatorAccount.name} {account.name} \'{account.ownerPublicKey}\' '
             f'\'{account.activePublicKey}\' --stake-net "{stakeNet} {CORE_SYMBOL}" --stake-cpu '
             f'"{stakeCPU} {CORE_SYMBOL}" --buy-ram "{buyRAM} {CORE_SYMBOL}" {additionalArgs}')
        msg="(creator account=%s, account=%s)" % (creatorAccount.name, account.name);
        trans=self.processCleosCmd(cmd, cmdDesc, silentErrors=False, exitOnError=exitOnError, exitMsg=msg)
        self.trackCmdTransaction(trans)
        transId=NodeosQueries.getTransId(trans)

        if stakedDeposit > 0:
            self.waitForTransactionInBlock(transId) # seems like account creation needs to be finalized before transfer can happen
            trans = self.transferFunds(creatorAccount, account, NodeosQueries.currencyIntToStr(stakedDeposit, CORE_SYMBOL), "init")
            transId=NodeosQueries.getTransId(trans)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    def createAccount(self, account, creatorAccount, stakedDeposit=1000, waitForTransBlock=False, exitOnError=False, sign=False):
        """Create account and return creation transactions. Return transaction json object.
        waitForTransBlock: wait on creation transaction id to appear in a block."""
        signStr = NodeosQueries.sign_str(sign, [ creatorAccount.activePublicKey ])
        cmdDesc="create account"
        cmd="%s -j %s %s %s %s %s" % (
            cmdDesc, signStr, creatorAccount.name, account.name, account.ownerPublicKey, account.activePublicKey)
        msg="(creator account=%s, account=%s)" % (creatorAccount.name, account.name);
        trans=self.processCleosCmd(cmd, cmdDesc, silentErrors=False, exitOnError=exitOnError, exitMsg=msg)
        self.trackCmdTransaction(trans)
        transId=NodeosQueries.getTransId(trans)

        if stakedDeposit > 0:
            self.waitForTransactionInBlock(transId) # seems like account creation needs to be finlized before transfer can happen
            trans = self.transferFunds(creatorAccount, account, "%0.04f %s" % (stakedDeposit/10000, CORE_SYMBOL), "init")
            self.trackCmdTransaction(trans)
            transId=NodeosQueries.getTransId(trans)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    def transferFundsCmdArr(self, source, destination, amountStr, memo, force, retry, sign, dontSend, expiration, skipSign):
        assert isinstance(amountStr, str)
        assert(source)
        assert(isinstance(source, Account))
        assert(destination)
        assert(isinstance(destination, Account))
        assert(expiration is None or isinstance(expiration, int))

        dontSendStr = ""
        if dontSend:
            dontSendStr = "--dont-broadcast "
            if expiration is None:
                # default transaction expiration to be 4 minutes in the future
                expiration = 240

        expirationStr = ""
        if expiration is not None:
            expirationStr = "--expiration %d " % (expiration)

        cmd="%s %s -v transfer %s -j %s %s" % (
            Utils.EosClientPath, self.eosClientArgs(), self.getRetryCmdArg(retry), dontSendStr, expirationStr)
        cmdArr=cmd.split()
        # not using sign_str, since cmdArr messes up the string
        if sign:
            cmdArr.append("--sign-with")
            cmdArr.append("[ \"%s\" ]" % (source.activePublicKey))

        if skipSign:
            cmdArr.append("--skip-sign")

        cmdArr.append(source.name)
        cmdArr.append(destination.name)
        cmdArr.append(amountStr)
        cmdArr.append(memo)
        if force:
            cmdArr.append("-f")
        s=" ".join(cmdArr)
        if Utils.Debug: Utils.Print("cmd: %s" % (s))
        return cmdArr

    # Trasfer funds. Returns "transfer" json return object
    def transferFunds(self, source, destination, amountStr, memo="memo", force=False, waitForTransBlock=False, exitOnError=True, reportStatus=True, retry=None, sign=False, dontSend=False, expiration=90, skipSign=False):
        cmdArr = self.transferFundsCmdArr(source, destination, amountStr, memo, force, retry, sign, dontSend, expiration, skipSign)
        trans=None
        start=time.perf_counter()
        try:
            trans=Utils.runCmdArrReturnJson(cmdArr)
            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
            if not dontSend:
                self.trackCmdTransaction(trans, reportStatus=reportStatus)
        except subprocess.CalledProcessError as ex:
            end=time.perf_counter()
            msg=ex.stderr.decode("utf-8")
            Utils.Print("ERROR: Exception during funds transfer.  cmd Duration: %.3f sec.  %s" % (end-start, msg))
            if exitOnError:
                Utils.cmdError("could not transfer \"%s\" from %s to %s" % (amountStr, source, destination))
                Utils.errorExit("Failed to transfer \"%s\" from %s to %s" % (amountStr, source, destination))
            return None

        if trans is None:
            Utils.cmdError("could not transfer \"%s\" from %s to %s" % (amountStr, source, destination))
            Utils.errorExit("Failed to transfer \"%s\" from %s to %s" % (amountStr, source, destination))

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    # Trasfer funds. Returns (popen, cmdArr) for checkDelayedOutput
    def transferFundsAsync(self, source, destination, amountStr, memo="memo", force=False, exitOnError=True, retry=None, sign=False, dontSend=False, expiration=90, skipSign=False):
        cmdArr = self.transferFundsCmdArr(source, destination, amountStr, memo, force, retry, sign, dontSend, expiration, skipSign)
        start=time.perf_counter()
        try:
            popen=Utils.delayedCheckOutput(cmdArr)
            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
        except subprocess.CalledProcessError as ex:
            end=time.perf_counter()
            msg=ex.stderr.decode("utf-8")
            Utils.Print("ERROR: Exception during spawn of funds transfer.  cmd Duration: %.3f sec.  %s" % (end-start, msg))
            if exitOnError:
                Utils.cmdError("could not transfer \"%s\" from %s to %s" % (amountStr, source, destination))
                Utils.errorExit("Failed to transfer \"%s\" from %s to %s" % (amountStr, source, destination))
            return None, None

        return popen, cmdArr

    # publish contract and return transaction as json object
    def publishContract(self, account, contractDir, wasmFile, abiFile, waitForTransBlock=False, shouldFail=False, sign=False):
        signStr = NodeosQueries.sign_str(sign, [ account.activePublicKey ])
        cmd="%s %s -v set contract -j %s %s %s" % (Utils.EosClientPath, self.eosClientArgs(), signStr, account.name, contractDir)
        cmd += "" if wasmFile is None else (" "+ wasmFile)
        cmd += "" if abiFile is None else (" " + abiFile)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
        trans=None
        start=time.perf_counter()
        try:
            trans=Utils.runCmdReturnJson(cmd, trace=False)
            self.trackCmdTransaction(trans)
            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
        except subprocess.CalledProcessError as ex:
            if not shouldFail:
                end=time.perf_counter()
                out=ex.output.decode("utf-8")
                msg=ex.stderr.decode("utf-8")
                Utils.Print("ERROR: Exception during set contract. stderr: %s.  stdout: %s.  cmd Duration: %.3f sec." % (msg, out, end-start))
                return None
            else:
                retMap={}
                retMap["returncode"]=ex.returncode
                retMap["cmd"]=ex.cmd
                retMap["output"]=ex.output
                retMap["stderr"]=ex.stderr
                return retMap

        if shouldFail:
            if trans["processed"]["except"] != None:
                retMap={}
                retMap["returncode"]=0
                retMap["cmd"]=cmd
                retMap["output"]=bytes(str(trans),'utf-8')
                return retMap
            else:
                Utils.Print("ERROR: The publish contract did not fail as expected.")
                return None

        NodeosQueries.validateTransaction(trans)
        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=False)

    # returns tuple with indication if transaction was successfully sent and either the transaction or else the exception output
    def pushTransaction(self, trans, opts="", silentErrors=False, permissions=None):
        assert(isinstance(trans, dict))
        if isinstance(permissions, str):
            permissions=[permissions]

        cmd="%s %s push transaction -j" % (Utils.EosClientPath, self.eosClientArgs())
        cmdArr=cmd.split()
        transStr = json.dumps(trans, separators=(',', ':'))
        transStr = transStr.replace("'", '"')
        cmdArr.append(transStr)
        if opts is not None:
            cmdArr += opts.split()
        if permissions is not None:
            for permission in permissions:
                cmdArr.append("-p")
                cmdArr.append(permission)

        s=" ".join(cmdArr)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmdArr))
        start=time.perf_counter()
        try:
            retTrans=Utils.runCmdArrReturnJson(cmdArr)
            self.trackCmdTransaction(retTrans, ignoreNonTrans=True)
            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
            return (NodeosQueries.getTransStatus(retTrans) == 'executed', retTrans)
        except subprocess.CalledProcessError as ex:
            msg=ex.stderr.decode("utf-8")
            if not silentErrors:
                end=time.perf_counter()
                Utils.Print("ERROR: Exception during push transaction.  cmd Duration=%.3f sec.  %s" % (end - start, msg))
            return (False, msg)

    # returns tuple with transaction execution status and transaction
    def pushMessage(self, account, action, data, opts, silentErrors=False, signatures=None, expectTrxTrace=True, force=False):
        cmd="%s %s push action -j %s %s" % (Utils.EosClientPath, self.eosClientArgs(), account, action)
        cmdArr=cmd.split()
        # not using sign_str, since cmdArr messes up the string
        if signatures is not None:
            cmdArr.append("--sign-with")
            cmdArr.append("[ \"%s\" ]" % ("\", \"".join(signatures)))
        if force:
            cmdArr.append("-f")
        if data is not None:
            cmdArr.append(data)
        if opts is not None:
            cmdArr += opts.split()
        if Utils.Debug: Utils.Print("cmd: %s" % (cmdArr))
        start=time.perf_counter()
        try:
            trans=Utils.runCmdArrReturnJson(cmdArr)
            self.trackCmdTransaction(trans, ignoreNonTrans=True)
            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
            return (NodeosQueries.getTransStatus(trans) == 'executed' if expectTrxTrace else True, trans)
        except subprocess.CalledProcessError as ex:
            msg=ex.stderr.decode("utf-8")
            output=ex.output.decode("utf-8")
            if not silentErrors:
                end=time.perf_counter()
                Utils.Print("ERROR: Exception during push message. stderr: %s. stdout: %s.  cmd Duration=%.3f sec." % (msg, output, end - start))
            return (False, msg)

    def setPermission(self, account, code, pType, requirement, waitForTransBlock=False, exitOnError=False, sign=False):
        assert(isinstance(account, Account))
        assert(isinstance(code, Account))
        signStr = NodeosQueries.sign_str(sign, [ account.activePublicKey ])
        cmdDesc="set action permission"
        cmd="%s -j %s %s %s %s %s" % (cmdDesc, signStr, account.name, code.name, pType, requirement)
        trans=self.processCleosCmd(cmd, cmdDesc, silentErrors=False, exitOnError=exitOnError)
        self.trackCmdTransaction(trans)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    def delegatebw(self, fromAccount, netQuantity, cpuQuantity, toAccount=None, transferTo=False, waitForTransBlock=False, exitOnError=False, reportStatus=True, sign=False):
        if toAccount is None:
            toAccount=fromAccount

        signStr = NodeosQueries.sign_str(sign, [ fromAccount.activePublicKey ])
        cmdDesc="system delegatebw"
        transferStr="--transfer" if transferTo else ""
        cmd="%s -j %s %s %s \"%s %s\" \"%s %s\" %s" % (
            cmdDesc, signStr, fromAccount.name, toAccount.name, netQuantity, CORE_SYMBOL, cpuQuantity, CORE_SYMBOL, transferStr)
        msg="fromAccount=%s, toAccount=%s" % (fromAccount.name, toAccount.name);
        trans=self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)
        self.trackCmdTransaction(trans, reportStatus=reportStatus)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    def undelegatebw(self, fromAccount, netQuantity, cpuQuantity, toAccount=None, waitForTransBlock=False, silentErrors=True, exitOnError=False, sign=False):
        if toAccount is None:
            toAccount=fromAccount

        signStr = NodeosQueries.sign_str(sign, [ fromAccount.activePublicKey ])
        cmdDesc="system undelegatebw"
        cmd="%s -j %s %s %s \"%s %s\" \"%s %s\"" % (
            cmdDesc, signStr, fromAccount.name, toAccount.name, netQuantity, CORE_SYMBOL, cpuQuantity, CORE_SYMBOL)
        msg="fromAccount=%s, toAccount=%s" % (fromAccount.name, toAccount.name);
        trans=self.processCleosCmd(cmd, cmdDesc, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=msg)
        self.trackCmdTransaction(trans)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    def regproducer(self, producer, url, location, waitForTransBlock=False, silentErrors=True, exitOnError=False, sign=False):
        signStr = NodeosQueries.sign_str(sign, [ producer.activePublicKey ])
        cmdDesc="system regproducer"
        cmd="%s -j %s %s %s %s %s" % (
            cmdDesc, signStr, producer.name, producer.activePublicKey, url, location)
        msg="producer=%s" % (producer.name);
        trans=self.processCleosCmd(cmd, cmdDesc, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=msg)
        self.trackCmdTransaction(trans)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    def vote(self, account, producers, waitForTransBlock=False, exitOnError=False, sign=False):
        signStr = NodeosQueries.sign_str(sign, [ account.activePublicKey ])
        cmdDesc = "system voteproducer prods"
        cmd="%s -j %s %s %s" % (
            cmdDesc, signStr, account.name, " ".join(producers))
        msg="account=%s, producers=[ %s ]" % (account.name, ", ".join(producers));
        trans=self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)
        self.trackCmdTransaction(trans)

        return self.waitForTransBlockIfNeeded(trans, waitForTransBlock, exitOnError=exitOnError)

    # Require producer_api_plugin
    def activatePreactivateFeature(self):
        protocolFeatureDigestDict = self.getSupportedProtocolFeatureDict()
        preactivateFeatureDigest = protocolFeatureDigestDict["PREACTIVATE_FEATURE"]["feature_digest"]
        assert preactivateFeatureDigest

        self.scheduleProtocolFeatureActivations([preactivateFeatureDigest])

        # Wait for the next block to be produced so the scheduled protocol feature is activated
        assert self.waitForHeadToAdvance(blocksToAdvance=2), "ERROR: TIMEOUT WAITING FOR PREACTIVATE"

    # Return an array of feature digests to be preactivated in a correct order respecting dependencies
    # Require producer_api_plugin
    def getAllBuiltinFeatureDigestsToPreactivate(self):
        protocolFeatures = {}
        supportedProtocolFeatures = self.getSupportedProtocolFeatures()
        for protocolFeature in supportedProtocolFeatures["payload"]:
            for spec in protocolFeature["specification"]:
                if (spec["name"] == "builtin_feature_codename"):
                    codename = spec["value"]
                    # Filter out "PREACTIVATE_FEATURE"
                    if codename != "PREACTIVATE_FEATURE":
                        protocolFeatures[protocolFeature["feature_digest"]] = protocolFeature["dependencies"]
                    break
        return dep(protocolFeatures)


    # Require PREACTIVATE_FEATURE to be activated and require eosio.bios with preactivate_feature
    def preactivateProtocolFeatures(self, featureDigests:list):
        for group in featureDigests:
            for digest in group:
                Utils.Print("push activate action with digest {}".format(digest))
                data="{{\"feature_digest\":{}}}".format(digest)
                opts="--permission eosio@active"
                success, trans=self.pushMessage("eosio", "activate", data, opts)
                if not success:
                    Utils.Print("ERROR: Failed to preactive digest {}".format(digest))
                    return None
            self.waitForTransactionInBlock(trans['transaction_id'])

    # Require PREACTIVATE_FEATURE to be activated and require eosio.bios with preactivate_feature
    def preactivateAllBuiltinProtocolFeature(self):
        allBuiltinProtocolFeatureDigests = self.getAllBuiltinFeatureDigestsToPreactivate()
        self.preactivateProtocolFeatures(allBuiltinProtocolFeatureDigests)

