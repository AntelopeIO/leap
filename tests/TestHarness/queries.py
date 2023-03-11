#!/usr/bin/env python3

import decimal
import json
import re
import subprocess
import time

import urllib.request
import urllib.parse
import urllib.error

from core_symbol import CORE_SYMBOL
from .testUtils import Account
from .testUtils import EnumType
from .testUtils import addEnum
from .testUtils import ReturnType
from .testUtils import unhandledEnumType
from .testUtils import Utils


class BlockType(EnumType):
    pass

addEnum(BlockType, "head")
addEnum(BlockType, "lib")


class NodeosQueries:
    def __init__(self, host, port, walletMgr=None):
        self.endpointHttp = f'http://{host}:{port}'
        self.endpointArgs = f'--url {self.endpointHttp}'
        self.walletMgr = walletMgr

    def eosClientArgs(self):
        walletArgs=" " + self.walletMgr.getWalletEndpointArgs() if self.walletMgr is not None else ""
        return self.endpointArgs + walletArgs + " " + Utils.MiscEosClientArgs

    class Context:
        def __init__(self, obj, desc):
            self.obj=obj
            self.sections=[obj]
            self.keyContext=[]
            self.desc=desc

        def __json(self):
            return "%s=\n%s" % (self.desc, json.dumps(self.obj, indent=1))

        def __keyContext(self):
            msg = ']['.join(self.keyContext)
            return f'[{msg}]' if len(msg) > 0 else ''

        def __contextDesc(self):
            return "%s%s" % (self.desc, self.__keyContext())

        def hasKey(self, newKey, subSection = None):
            assert isinstance(newKey, str), f"ERROR: Trying to use {newKey} as a key"
            if subSection is None:
                subSection=self.sections[-1]
            assert isinstance(subSection, dict), f"ERROR: Calling 'hasKey' method when context is not a dictionary. {self.__contextDesc()} in {self.__json()}"
            return newKey in subSection

        def isSectionNull(self, key, subSection = None):
            assert isinstance(key, str), f"ERROR: Trying to use {key} as a key"
            if subSection is None:
                subSection=self.sections[-1]
            assert isinstance(subSection, dict), f"ERROR: Calling 'isSectionNull' method when context is not a dictionary. {self.__contextDesc()} in {self.__json()}"
            return subSection[key] == None

        def add(self, newKey):
            subSection=self.sections[-1]
            assert self.hasKey(newKey, subSection), f"ERROR: {self.__contextDesc()} does not contain key '{newKey}'. {self.__json()}"
            current=subSection[newKey]
            self.sections.append(current)
            self.keyContext.append(newKey)
            return current

        def index(self, i):
            assert isinstance(i, int), f"ERROR: Trying to use {i} as a list index"
            cur=self.getCurrent()
            assert isinstance(cur, list), f"ERROR: Calling 'index' method when context is not a list.  {self.__contextDesc()} in {self.__json()}"
            listLen=len(cur)
            assert i < listLen, f"ERROR: Index {i} is beyond the size of the current list ({listLen}).  {self.__contextDesc()} in {self.__json()}"
            return self.sections.append(cur[i])

        def getCurrent(self):
            return self.sections[-1]

    @staticmethod
    def validateTransaction(trans):
        assert trans
        assert isinstance(trans, dict), f"Input type is {type(trans)}"

        executed="executed"

        transStatus=Queries.getTransStatus(trans)
        assert transStatus == executed, f"ERROR: Valid transaction should be '{executed}' but it was '{transStatus}'.\nTransaction: {json.dumps(trans, indent=1)}"

    @staticmethod
    def getTransStatus(trans):
        cntxt=Queries.Context(trans, "trans")
        # could be a transaction response
        if cntxt.hasKey("processed"):
            cntxt.add("processed")
            if not cntxt.isSectionNull("except"):
                return "exception"
            cntxt.add("receipt")
            return cntxt.add("status")

        # or what the history plugin returns
        cntxt.add("trx")
        cntxt.add("receipt")
        return cntxt.add("status")

    @staticmethod
    def getTransBlockNum(trans):
        cntxt=Queries.Context(trans, "trans")
        # could be a transaction response
        if cntxt.hasKey("processed"):
            cntxt.add("processed")
            cntxt.add("action_traces")
            cntxt.index(0)
            if not cntxt.isSectionNull("except"):
                return "no_block"
            return cntxt.add("block_num")

        # or what the trace api plugin returns
        return cntxt.add("block_num")

    @staticmethod
    def getTransId(trans):
        """Retrieve transaction id from dictionary object."""
        assert trans
        assert isinstance(trans, dict), f"Input type is {type(trans)}"

        assert "transaction_id" in trans, f"trans does not contain key 'transaction_id'. trans={json.dumps(trans, indent=2, sort_keys=True)}"
        transId=trans["transaction_id"]
        return transId

    @staticmethod
    def isTrans(obj):
        """Identify if this is a transaction dictionary."""
        if obj is None or not isinstance(obj, dict):
            return False

        return True if "transaction_id" in obj else False

    @staticmethod
    def getRetryCmdArg(retry):
        """Returns the cleos cmd arg for retry"""
        assert retry is None or isinstance(retry, int) or (isinstance(retry, str) and retry == "lib"), "Invalid retry passed"
        cmdRetry = ""
        if retry is not None:
            if retry == "lib":
                cmdRetry = "--retry-irreversible"
            else:
                cmdRetry = "--retry-num-blocks %s" % retry
        return cmdRetry

    @staticmethod
    def currencyStrToInt(balanceStr):
        """Converts currency string of form "12.3456 SYS" to int 123456"""
        assert(isinstance(balanceStr, str))
        balanceStr=balanceStr.split()[0]
        #balance=int(decimal.Decimal(balanceStr[1:])*10000)
        balance=int(decimal.Decimal(balanceStr)*10000)

        return balance

    @staticmethod
    def currencyIntToStr(balance, symbol):
        """Converts currency int of form 123456 to string "12.3456 SYS" where SYS is symbol string"""
        assert(isinstance(balance, int))
        assert(isinstance(symbol, str))
        balanceStr="%.04f %s" % (balance/10000.0, symbol)

        return balanceStr

    @staticmethod
    def sign_str(sign, keys):
        assert(isinstance(sign, bool))
        assert(isinstance(keys, list))
        if not sign:
            return ""

        return "--sign-with '[ \"" + "\", \"".join(keys) + "\" ]'"

    @staticmethod
    def getBlockAttribute(block, key, blockNum, exitOnError=True):
        value=block[key]

        if value is None and exitOnError:
            blockNumStr=" for block number %s" % (blockNum)
            blockStr=" with block content:\n%s" % (json.dumps(block, indent=4, sort_keys=True))
            Utils.cmdError("could not get %s%s%s" % (key, blockNumStr, blockStr))
            Utils.errorExit("Failed to get block's %s" % (key))

        return value

    def getBlock(self, blockNum, silentErrors=False, exitOnError=False):
        """Given a blockId will return block details."""
        assert(isinstance(blockNum, int))
        cmdDesc="get block"
        cmd="%s %d" % (cmdDesc, blockNum)
        msg="(block number=%s)" % (blockNum);
        return self.processCleosCmd(cmd, cmdDesc, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=msg)

    def isBlockPresent(self, blockNum, blockType=BlockType.head):
        """Does node have head_block_num/last_irreversible_block_num >= blockNum"""
        assert isinstance(blockNum, int)
        assert isinstance(blockType, BlockType)
        assert (blockNum > 0)

        info=self.getInfo(silentErrors=True, exitOnError=True)
        node_block_num=0
        try:
            if blockType==BlockType.head:
                node_block_num=int(info["head_block_num"])
            elif blockType==BlockType.lib:
                node_block_num=int(info["last_irreversible_block_num"])
            else:
                unhandledEnumType(blockType)

        except (TypeError, KeyError) as _:
            Utils.Print("Failure in get info parsing %s block. %s" % (blockType.type, info))
            raise

        present = True if blockNum <= node_block_num else False
        if Utils.Debug and blockType==BlockType.lib:
            decorator=""
            if not present:
                decorator="not "
            Utils.Print("Block %d is %sfinalized." % (blockNum, decorator))

        return present

    def isBlockFinalized(self, blockNum):
        """Is blockNum finalized"""
        return self.isBlockPresent(blockNum, blockType=BlockType.lib)

    def getTransaction(self, transId, silentErrors=False, exitOnError=False, delayedRetry=True):
        assert(isinstance(transId, str))
        exitOnErrorForDelayed=not delayedRetry and exitOnError
        timeout=3
        cmdDesc="get transaction_trace"
        cmd="%s %s" % (cmdDesc, transId)
        msg="(transaction id=%s)" % (transId);
        for i in range(0,(int(60/timeout) - 1)):
            trans=self.processCleosCmd(cmd, cmdDesc, silentErrors=True, exitOnError=exitOnErrorForDelayed, exitMsg=msg)
            if trans is not None or not delayedRetry:
                return trans
            if Utils.Debug: Utils.Print("Could not find transaction with id %s, delay and retry" % (transId))
            time.sleep(timeout)

        self.missingTransaction=True
        # either it is there or the transaction has timed out
        return self.processCleosCmd(cmd, cmdDesc, silentErrors=silentErrors, exitOnError=exitOnError, exitMsg=msg)

    def isTransInBlock(self, transId, blockId):
        """Check if transId is within block identified by blockId"""
        assert(transId)
        assert(isinstance(transId, str))
        assert(blockId)
        assert(isinstance(blockId, int))

        block=self.getBlock(blockId, exitOnError=True)

        transactions=None
        key=""
        try:
            key="[transactions]"
            transactions=block["transactions"]
        except (AssertionError, TypeError, KeyError) as _:
            Utils.Print("block%s not found. Block: %s" % (key,block))
            raise

        if transactions is not None:
            for trans in transactions:
                assert(trans)
                try:
                    myTransId=trans["trx"]["id"]
                    if transId == myTransId:
                        return True
                except (TypeError, KeyError) as _:
                    Utils.Print("transaction%s not found. Transaction: %s" % (key, trans))

        return False

    def getBlockNumByTransId(self, transId, exitOnError=True, delayedRetry=True, blocksAhead=5):
        """Given a transaction Id (string), return the block number (int) containing the transaction"""
        assert(transId)
        assert(isinstance(transId, str))
        trans=self.getTransaction(transId, exitOnError=exitOnError, delayedRetry=delayedRetry)

        refBlockNum=None
        key=""
        try:
            key="[transaction][transaction_header][ref_block_num]"
            refBlockNum=trans["transaction_header"]["ref_block_num"]
            refBlockNum=int(refBlockNum)+1
        except (TypeError, ValueError, KeyError) as _:
            Utils.Print("transaction%s not found. Transaction: %s" % (key, trans))
            return None

        headBlockNum=self.getHeadBlockNum()
        assert(headBlockNum)
        try:
            headBlockNum=int(headBlockNum)
        except ValueError:
            Utils.Print("ERROR: Block info parsing failed. %s" % (headBlockNum))
            raise

        if Utils.Debug: Utils.Print("Reference block num %d, Head block num: %d" % (refBlockNum, headBlockNum))
        for blockNum in range(refBlockNum, headBlockNum + blocksAhead):
            self.waitForBlock(blockNum)
            if self.isTransInBlock(transId, blockNum):
                if Utils.Debug: Utils.Print("Found transaction %s in block %d" % (transId, blockNum))
                return blockNum

        return None

    def isTransInAnyBlock(self, transId: str):
        """Check if transaction (transId) is in a block."""
        assert(transId)
        assert(isinstance(transId, str))
        blockId=self.getBlockNumByTransId(transId)
        return True if blockId else False

    def isTransFinalized(self, transId):
        """Check if transaction (transId) has been finalized."""
        assert(transId)
        assert(isinstance(transId, str))
        blockNum=self.getBlockNumByTransId(transId)
        if not blockNum:
            return False

        assert(isinstance(blockNum, int))
        return self.isBlockPresent(blockNum, blockType=BlockType.lib)

    def getEosAccount(self, name, exitOnError=False, returnType=ReturnType.json):
        assert(isinstance(name, str))
        cmdDesc="get account"
        jsonFlag="-j" if returnType==ReturnType.json else ""
        cmd="%s %s %s" % (cmdDesc, jsonFlag, name)
        msg="( getEosAccount(name=%s) )" % (name);
        return self.processCleosCmd(cmd, cmdDesc, silentErrors=False, exitOnError=exitOnError, exitMsg=msg, returnType=returnType)

    def getTable(self, contract, scope, table, exitOnError=False):
        cmdDesc = "get table"
        cmd="%s %s %s %s" % (cmdDesc, contract, scope, table)
        msg="contract=%s, scope=%s, table=%s" % (contract, scope, table);
        return self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)

    def getTableAccountBalance(self, contract, scope):
        assert(isinstance(contract, str))
        assert(isinstance(scope, str))
        table="accounts"
        trans = self.getTable(contract, scope, table, exitOnError=True)
        try:
            return trans["rows"][0]["balance"]
        except (TypeError, KeyError) as _:
            print("transaction[rows][0][balance] not found. Transaction: %s" % (trans))
            raise

    def getCurrencyBalance(self, contract, account, symbol=CORE_SYMBOL, exitOnError=False):
        """returns raw output from get currency balance e.g. '99999.9950 CUR'"""
        assert(contract)
        assert(isinstance(contract, str))
        assert(account)
        assert(isinstance(account, str))
        assert(symbol)
        assert(isinstance(symbol, str))
        cmdDesc = "get currency balance"
        cmd="%s %s %s %s" % (cmdDesc, contract, account, symbol)
        msg="contract=%s, account=%s, symbol=%s" % (contract, account, symbol);
        return self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg, returnType=ReturnType.raw)

    def getCurrencyStats(self, contract, symbol=CORE_SYMBOL, exitOnError=False):
        """returns Json output from get currency stats."""
        assert(contract)
        assert(isinstance(contract, str))
        assert(symbol)
        assert(isinstance(symbol, str))
        cmdDesc = "get currency stats"
        cmd="%s %s %s" % (cmdDesc, contract, symbol)
        msg="contract=%s, symbol=%s" % (contract, symbol);
        return self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)

    # Verifies account. Returns "get account" json return object
    def verifyAccount(self, account):
        assert(account)
        ret = self.getEosAccount(account.name)
        if ret is not None:
            account_name = ret["account_name"]
            if account_name is None:
                Utils.Print("ERROR: Failed to verify account creation.", account.name)
                return None
            return ret

    def verifyAccountMdb(self, account):
        assert(account)
        ret=self.getEosAccountFromDb(account.name)
        if ret is not None:
            account_name=ret["name"]
            if account_name is None:
                Utils.Print("ERROR: Failed to verify account creation.", account.name)
                return None
            return ret

        return None

    def validateFunds(self, initialBalances, transferAmount, source, accounts):
        """Validate each account has the expected SYS balance. Validate cumulative balance matches expectedTotal."""
        assert(source)
        assert(isinstance(source, Account))
        assert(accounts)
        assert(isinstance(accounts, list))
        assert(len(accounts) > 0)
        assert(initialBalances)
        assert(isinstance(initialBalances, dict))
        assert(isinstance(transferAmount, int))

        currentBalances=self.getEosBalances([source] + accounts)
        assert(currentBalances)
        assert(isinstance(currentBalances, dict))
        assert(len(initialBalances) == len(currentBalances))

        if len(currentBalances) != len(initialBalances):
            Utils.Print("ERROR: validateFunds> accounts length mismatch. Initial: %d, current: %d" % (len(initialBalances), len(currentBalances)))
            return False

        for key, value in currentBalances.items():
            initialBalance = initialBalances[key]
            assert(initialBalances)
            expectedInitialBalance = value - transferAmount
            if key is source:
                expectedInitialBalance = value + (transferAmount*len(accounts))

            if (initialBalance != expectedInitialBalance):
                Utils.Print("ERROR: validateFunds> Expected: %d, actual: %d for account %s" %
                            (expectedInitialBalance, initialBalance, key.name))
                return False

    def getEosBalances(self, accounts):
        """Returns a dictionary with account balances keyed by accounts"""
        assert(accounts)
        assert(isinstance(accounts, list))

        balances={}
        for account in accounts:
            balance = self.getAccountEosBalance(account.name)
            balances[account]=balance

        return balances

    # Gets subjective bill info for an account
    def getAccountSubjectiveInfo(self, account):
        acct = self.getEosAccount(account)
        return acct["subjective_cpu_bill_limit"]

    # Gets accounts mapped to key. Returns json object
    def getAccountsByKey(self, key, exitOnError=False):
        cmdDesc = "get accounts"
        cmd="%s %s" % (cmdDesc, key)
        msg="key=%s" % (key);
        return self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)

    # Get actions mapped to an account (cleos get actions)
    def getActions(self, account, pos=-1, offset=-1, exitOnError=False):
        assert(isinstance(account, Account))
        assert(isinstance(pos, int))
        assert(isinstance(offset, int))

        cmdDesc = "get actions"
        cmd = "%s -j %s %d %d" % (cmdDesc, account.name, pos, offset)
        msg = "account=%s, pos=%d, offset=%d" % (account.name, pos, offset);
        return self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)

    # Gets accounts mapped to key. Returns array
    def getAccountsArrByKey(self, key):
        trans=self.getAccountsByKey(key)
        assert(trans)
        assert("account_names" in trans)
        accounts=trans["account_names"]
        return accounts

    def getServants(self, name, exitOnError=False):
        cmdDesc = "get servants"
        cmd="%s %s" % (cmdDesc, name)
        msg="name=%s" % (name);
        return self.processCleosCmd(cmd, cmdDesc, exitOnError=exitOnError, exitMsg=msg)

    def getServantsArr(self, name):
        trans=self.getServants(name, exitOnError=True)
        servants=trans["controlled_accounts"]
        return servants

    def getAccountEosBalanceStr(self, scope):
        """Returns SYS currency0000 account balance from cleos get table command. Returned balance is string following syntax "98.0311 SYS". """
        assert isinstance(scope, str)
        amount=self.getTableAccountBalance("eosio.token", scope)
        if Utils.Debug: Utils.Print("getNodeAccountEosBalance %s %s" % (scope, amount))
        assert isinstance(amount, str)
        return amount

    def getAccountEosBalance(self, scope):
        """Returns SYS currency0000 account balance from cleos get table command. Returned balance is an integer e.g. 980311. """
        balanceStr=self.getAccountEosBalanceStr(scope)
        balance=Queries.currencyStrToInt(balanceStr)
        return balance

    def getAccountCodeHash(self, account):
        cmd="%s %s get code %s" % (Utils.EosClientPath, self.eosClientArgs(), account)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
        start=time.perf_counter()
        try:
            retStr=Utils.checkOutput(cmd.split())
            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
            #Utils.Print ("get code> %s"% retStr)
            p=re.compile(r'code\shash: (\w+)\n', re.MULTILINE)
            m=p.search(retStr)
            if m is None:
                msg="Failed to parse code hash."
                Utils.Print("ERROR: "+ msg)
                return None

            return m.group(1)
        except subprocess.CalledProcessError as ex:
            end=time.perf_counter()
            msg=ex.output.decode("utf-8")
            Utils.Print("ERROR: Exception during code hash retrieval.  cmd Duration: %.3f sec.  %s" % (end-start, msg))
            return None

    def getTableRows(self, contract, scope, table):
        jsonData=self.getTable(contract, scope, table)
        if jsonData is None:
            return None
        rows=jsonData["rows"]
        return rows

    def getTableRow(self, contract, scope, table, idx):
        if idx < 0:
            Utils.Print("ERROR: Table index cannot be negative. idx: %d" % (idx))
            return None
        rows=self.getTableRows(contract, scope, table)
        if rows is None or idx >= len(rows):
            Utils.Print("ERROR: Retrieved table does not contain row %d" % idx)
            return None
        row=rows[idx]
        return row

    def getTableColumns(self, contract, scope, table):
        row=self.getTableRow(contract, scope, table, 0)
        keys=list(row.keys())
        return keys

    def processCleosCmd(self, cmd, cmdDesc, silentErrors=True, exitOnError=False, exitMsg=None, returnType=ReturnType.json):
        assert(isinstance(returnType, ReturnType))
        cmd="%s %s %s" % (Utils.EosClientPath, self.eosClientArgs(), cmd)
        if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
        if exitMsg is not None:
            exitMsg="Context: " + exitMsg
        else:
            exitMsg=""
        trans=None
        start=time.perf_counter()
        try:
            if returnType==ReturnType.json:
                trans=Utils.runCmdReturnJson(cmd, silentErrors=silentErrors)
            elif returnType==ReturnType.raw:
                trans=Utils.runCmdReturnStr(cmd)
            else:
                unhandledEnumType(returnType)

            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
        except subprocess.CalledProcessError as ex:
            if not silentErrors:
                end=time.perf_counter()
                msg=ex.output.decode("utf-8")
                errorMsg="Exception during \"%s\". Exception message: %s.  cmd Duration=%.3f sec. %s" % (cmdDesc, msg, end-start, exitMsg)
                if exitOnError:
                    Utils.cmdError(errorMsg)
                    Utils.errorExit(errorMsg)
                else:
                    Utils.Print("ERROR: %s" % (errorMsg))
            return None

        if exitOnError and trans is None:
            Utils.cmdError("could not \"%s\". %s" % (cmdDesc,exitMsg))
            Utils.errorExit("Failed to \"%s\"" % (cmdDesc))

        return trans

    def processUrllibRequest(self, resource, command, payload={}, silentErrors=False, exitOnError=False, exitMsg=None, returnType=ReturnType.json, endpoint=None):
        if not endpoint:
            endpoint = self.endpointHttp
        cmd = f"{endpoint}/v1/{resource}/{command}"
        req = urllib.request.Request(cmd, method="POST")
        req.add_header('Content-Type', 'application/json')
        data = payload
        data = json.dumps(data)
        data = data.encode()
        if Utils.Debug: Utils.Print("cmd: %s %s" % (cmd, payload))
        rtn=None
        start=time.perf_counter()
        try:
            response = urllib.request.urlopen(req, data=data)
            if returnType==ReturnType.json:
                rtn = {}
                rtn["code"] = response.getcode()
                rtn["payload"] = json.load(response)
            elif returnType==ReturnType.raw:
                rtn = response.read()
            else:
                unhandledEnumType(returnType)

            if Utils.Debug:
                end=time.perf_counter()
                Utils.Print("cmd Duration: %.3f sec" % (end-start))
                printReturn=json.dumps(rtn) if returnType==ReturnType.json else rtn
                Utils.Print("cmd returned: %s" % (printReturn[:1024]))
        except urllib.error.HTTPError as ex:
            if not silentErrors:
                end=time.perf_counter()
                msg=ex.msg
                errorMsg="Exception during \"%s\". %s.  cmd Duration=%.3f sec." % (cmd, msg, end-start)
                if exitOnError:
                    Utils.cmdError(errorMsg)
                    Utils.errorExit(errorMsg)
                else:
                    Utils.Print("ERROR: %s" % (errorMsg))
                    if returnType==ReturnType.json:
                        rtn = json.load(ex)
                    elif returnType==ReturnType.raw:
                        rtn = ex.read()
                    else:
                        unhandledEnumType(returnType)
            else:
                return None

        if exitMsg is not None:
            exitMsg=": " + exitMsg
        else:
            exitMsg=""
        if exitOnError and rtn is None:
            Utils.cmdError("could not \"%s\" - %s" % (cmd,exitMsg))
            Utils.errorExit("Failed to \"%s\"" % (cmd))

        return rtn

    def getInfo(self, silentErrors=False, exitOnError=False):
        cmdDesc = "get info"
        info=self.processCleosCmd(cmdDesc, cmdDesc, silentErrors=silentErrors, exitOnError=exitOnError)
        if info is None:
            self.infoValid=False
        else:
            self.infoValid=True
            self.lastRetrievedHeadBlockNum=int(info["head_block_num"])
            self.lastRetrievedLIB=int(info["last_irreversible_block_num"])
            self.lastRetrievedHeadBlockProducer=info["head_block_producer"]
        return info

    def getTransactionStatus(self, transId, silentErrors=False, exitOnError=True):
        cmdDesc = f"get transaction-status {transId}"
        status=self.processCleosCmd(cmdDesc, cmdDesc, silentErrors=silentErrors, exitOnError=exitOnError)
        return status

    def checkPulse(self, exitOnError=False):
        info=self.getInfo(True, exitOnError=exitOnError)
        return False if info is None else True

    def getHeadBlockNum(self):
        """returns head block number(string) as returned by cleos get info."""
        info = self.getInfo(exitOnError=True)
        if info is not None:
            headBlockNumTag = "head_block_num"
            return info[headBlockNumTag]

    def getIrreversibleBlockNum(self):
        info = self.getInfo(exitOnError=True)
        if info is not None:
            Utils.Print("current lib: %d" % (info["last_irreversible_block_num"]))
            return info["last_irreversible_block_num"]

    def getBlockNum(self, blockType=BlockType.head):
        assert isinstance(blockType, BlockType)
        if blockType==BlockType.head:
            return self.getHeadBlockNum()
        elif blockType==BlockType.lib:
            return self.getIrreversibleBlockNum()
        else:
            unhandledEnumType(blockType)

    def getBlockProducerByNum(self, blockNum, timeout=None, waitForBlock=True, exitOnError=True):
        if waitForBlock:
            self.waitForBlock(blockNum, timeout=timeout, blockType=BlockType.head)
        block=self.getBlock(blockNum, exitOnError=exitOnError)
        return Queries.getBlockAttribute(block, "producer", blockNum, exitOnError=exitOnError)

    def getBlockProducer(self, timeout=None, waitForBlock=True, exitOnError=True, blockType=BlockType.head):
        blockNum=self.getBlockNum(blockType=blockType)
        block=self.getBlock(blockNum, exitOnError=exitOnError, blockType=blockType)
        return Queries.getBlockAttribute(block, "producer", blockNum, exitOnError=exitOnError)

    def getNextCleanProductionCycle(self, trans):
        rounds=21*12*2  # max time to ensure that at least 2/3+1 of producers x blocks per producer x at least 2 times
        if trans is not None:
            transId=Queries.getTransId(trans)
            self.waitForTransFinalization(transId, timeout=rounds/2)
        else:
            transId="Null"
        irreversibleBlockNum=self.getIrreversibleBlockNum()

        # The voted schedule should be promoted now, then need to wait for that to become irreversible
        votingTallyWindow=120  #could be up to 120 blocks before the votes were tallied
        promotedBlockNum=self.getHeadBlockNum()+votingTallyWindow
        # There was waitForIrreversibleBlock but due to bug it was waiting for head and not lib.
        # leaving waitForIrreversibleBlock here slows down voting test by few minutes so since
        # it was fine with head block for few years, switching to waitForBlock instead
        self.waitForBlock(promotedBlockNum, timeout=rounds/2)

        ibnSchedActive=self.getIrreversibleBlockNum()

        blockNum=self.getHeadBlockNum()
        Utils.Print("Searching for clean production cycle blockNum=%s ibn=%s  transId=%s  promoted bn=%s  ibn for schedule active=%s" % (blockNum,irreversibleBlockNum,transId,promotedBlockNum,ibnSchedActive))
        blockProducer=self.getBlockProducerByNum(blockNum)
        blockNum+=1
        Utils.Print("Advance until the next block producer is retrieved")
        while blockProducer == self.getBlockProducerByNum(blockNum):
            blockNum+=1

        blockProducer=self.getBlockProducerByNum(blockNum)
        return blockNum

    # Require producer_api_plugin
    def getSupportedProtocolFeatures(self, excludeDisabled=False, excludeUnactivatable=False):
        param = {
           "exclude_disabled": excludeDisabled,
           "exclude_unactivatable": excludeUnactivatable
        }
        res = self.processUrllibRequest("producer", "get_supported_protocol_features", param)
        return res

    # This will return supported protocol features in a dict (feature codename as the key), i.e.
    # {
    #   "PREACTIVATE_FEATURE": {...},
    #   "ONLY_LINK_TO_EXISTING_PERMISSION": {...},
    # }
    # Require producer_api_plugin
    def getSupportedProtocolFeatureDict(self, excludeDisabled=False, excludeUnactivatable=False):
        protocolFeatureDigestDict = {}
        supportedProtocolFeatures = self.getSupportedProtocolFeatures(excludeDisabled, excludeUnactivatable)
        for protocolFeature in supportedProtocolFeatures["payload"]:
            for spec in protocolFeature["specification"]:
                if (spec["name"] == "builtin_feature_codename"):
                    codename = spec["value"]
                    protocolFeatureDigestDict[codename] = protocolFeature
                    break
        return protocolFeatureDigestDict

    def getLatestBlockHeaderState(self):
        headBlockNum = self.getHeadBlockNum()
        cmdDesc = "get block {} --header-state".format(headBlockNum)
        latestBlockHeaderState = self.processCleosCmd(cmdDesc, cmdDesc)
        return latestBlockHeaderState

    def getActivatedProtocolFeatures(self):
        latestBlockHeaderState = self.getLatestBlockHeaderState()
        return latestBlockHeaderState["activated_protocol_features"]["protocol_features"]

