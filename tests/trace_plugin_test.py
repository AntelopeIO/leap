#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import time
import unittest
import os
import signal

from TestHarness import Cluster, Node, TestHelper, Utils, WalletMgr, CORE_SYMBOL, createAccountKeys

testSuccessful = True

class TraceApiPluginTest(unittest.TestCase):
    cluster=Cluster(defproduceraPrvtKey=None)
    walletMgr=WalletMgr(True)
    accounts = []
    cluster.setWalletMgr(walletMgr)

    # start keosd and nodeos
    def startEnv(self) :
        account_names = ["alice", "bob", "charlie"]
        abs_path = os.path.abspath(os.getcwd() + '/unittests/contracts/eosio.token/eosio.token.abi')
        traceNodeosArgs = " --verbose-http-errors --trace-rpc-abi eosio.token=" + abs_path
        self.cluster.launch(totalNodes=2, extraNodeosArgs=traceNodeosArgs)
        self.walletMgr.launch()
        testWalletName="testwallet"
        testWallet=self.walletMgr.create(testWalletName, [self.cluster.eosioAccount, self.cluster.defproduceraAccount])
        self.cluster.validateAccounts(None)
        self.accounts=createAccountKeys(len(account_names))
        node = self.cluster.getNode(1)
        for idx in range(len(account_names)):
            self.accounts[idx].name =  account_names[idx]
            self.walletMgr.importKey(self.accounts[idx], testWallet)
        for account in self.accounts:
            node.createInitializeAccount(account, self.cluster.eosioAccount, buyRAM=1000000, stakedDeposit=5000000, waitForTransBlock=True if account == self.accounts[-1] else False, exitOnError=True)

    def get_block(self, params: str, node: Node) -> json:
        resource = "trace_api"
        command = "get_block"
        payload = {"block_num" : params}
        return node.processUrllibRequest(resource, command, payload)

    def test_TraceApi(self) :
        node = self.cluster.getNode(0)
        for account in self.accounts:
            self.assertIsNotNone(node.verifyAccount(account))

        expectedAmount = Node.currencyIntToStr(5000000, CORE_SYMBOL)
        account_balances = []
        for account in self.accounts:
            amount = node.getAccountEosBalanceStr(account.name)
            self.assertEqual(amount, expectedAmount)
            account_balances.append(amount)

        xferAmount = Node.currencyIntToStr(123456, CORE_SYMBOL)
        trans = node.transferFunds(self.accounts[0], self.accounts[1], xferAmount, "test transfer a->b")
        transId = Node.getTransId(trans)
        blockNum = Node.getTransBlockNum(trans)

        self.assertEqual(node.getAccountEosBalanceStr(self.accounts[0].name), Utils.deduceAmount(expectedAmount, xferAmount))
        self.assertEqual(node.getAccountEosBalanceStr(self.accounts[1].name), Utils.addAmount(expectedAmount, xferAmount))
        node.waitForBlock(blockNum)

        # verify trans via node api before calling trace_api RPC
        blockFromNode = node.getBlock(blockNum)
        self.assertIn("transactions", blockFromNode)
        isTrxInBlockFromNode = False
        for trx in blockFromNode["transactions"]:
            self.assertIn("trx", trx)
            self.assertIn("id", trx["trx"])
            if (trx["trx"]["id"] == transId) :
                isTrxInBlockFromNode = True
                break
        self.assertTrue(isTrxInBlockFromNode)

        # verify trans via trace_api by calling get_block RPC
        blockFromTraceApi = self.get_block(blockNum, node)
        self.assertIn("transactions", blockFromTraceApi["payload"])
        isTrxInBlockFromTraceApi = False
        for trx in blockFromTraceApi["payload"]["transactions"]:
            self.assertIn("id", trx)
            if (trx["id"] == transId) :
                isTrxInBlockFromTraceApi = True
                self.assertIn('actions', trx)
                actions = trx['actions']
                for act in actions:
                    self.assertIn('params', act)
                    prms = act['params']
                    self.assertIn('from', prms)
                    self.assertIn('to', prms)
                    self.assertIn('quantity', prms)
                    self.assertIn('memo', prms)
                break
        self.assertTrue(isTrxInBlockFromTraceApi)
        global testSuccessful
        testSuccessful = True

        # relaunch with no time allocated for http response & abi-serializer. Will fail because get_info fails.
        node.kill(signal.SIGTERM)
        Utils.Print("Ignore expected: ERROR: Node relaunch Failed")
        isRelaunchSuccess = node.relaunch(timeout=10, addSwapFlags={"--http-max-response-time-ms": "0", "--abi-serializer-max-time-ms": "0"})

        # Verify get block_trace still works even with no time for http-max-response-time-ms and no time for bi-serializer-max-time-ms
        cmdDesc="get block_trace"
        cmd=" --print-response %s %d" % (cmdDesc, blockNum)
        cmd="%s %s %s" % (Utils.EosClientPath, node.eosClientArgs(), cmd)
        result=Utils.runCmdReturnStr(cmd, ignoreError=True)

        Utils.Print(f"{cmdDesc} returned: {result}")
        self.assertIn("test transfer a->b", result)

    @classmethod
    def setUpClass(self):
        self.startEnv(self)

    @classmethod
    def tearDownClass(self):
        TraceApiPluginTest.cluster.testFailed = not testSuccessful

if __name__ == "__main__":
    unittest.main()
