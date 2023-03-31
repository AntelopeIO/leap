#!/usr/bin/env python3

import json
import os
import shutil
import time
import unittest
import socket
import re

from TestHarness import Account, Node, TestHelper, Utils, WalletMgr, ReturnType

class PluginHttpTest(unittest.TestCase):
    sleep_s = 2
    base_wallet_cmd_str = f"http://{TestHelper.LOCAL_HOST}:{TestHelper.DEFAULT_WALLET_PORT}"
    keosd = WalletMgr(True, TestHelper.DEFAULT_PORT, TestHelper.LOCAL_HOST, TestHelper.DEFAULT_WALLET_PORT, TestHelper.LOCAL_HOST)
    node_id = 1
    nodeos = Node(TestHelper.LOCAL_HOST, TestHelper.DEFAULT_PORT, node_id, walletMgr=keosd)
    data_dir = Utils.getNodeDataDir(node_id)
    config_dir = Utils.getNodeConfigDir(node_id)
    empty_content_dict = {}
    http_post_invalid_param = '{invalid}'
    EOSIO_ACCT_PRIVATE_DEFAULT_KEY = "5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3"
    EOSIO_ACCT_PUBLIC_DEFAULT_KEY = "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"

    # make a fresh data dir
    def createDataDir(self):
        if os.path.exists(self.data_dir):
            shutil.rmtree(self.data_dir)
        os.makedirs(self.data_dir)

    # make a fresh config dir
    def createConfigDir(self):
        if os.path.exists(self.config_dir):
            shutil.rmtree(self.config_dir)
        os.makedirs(self.config_dir)

    # kill nodeos and keosd and clean up dirs
    def cleanEnv(self) :
        self.keosd.killall(True)
        WalletMgr.cleanup()
        Node.killAllNodeos()
        if os.path.exists(Utils.DataPath):
            shutil.rmtree(Utils.DataPath)
        if os.path.exists(self.config_dir):
            shutil.rmtree(self.config_dir)
        time.sleep(self.sleep_s)

    # start keosd and nodeos
    def startEnv(self) :
        self.createDataDir(self)
        self.createConfigDir(self)
        self.keosd.launch()
        plugin_names = ["trace_api_plugin", "test_control_api_plugin", "test_control_plugin", "net_plugin",
                        "net_api_plugin", "producer_plugin", "producer_api_plugin", "chain_api_plugin",
                        "http_plugin", "db_size_api_plugin", "prometheus_plugin"]
        nodeos_plugins = "--plugin eosio::" +  " --plugin eosio::".join(plugin_names)
        nodeos_flags = (" --data-dir=%s --config-dir=%s --trace-dir=%s --trace-no-abis --access-control-allow-origin=%s "
                        "--contracts-console --http-validate-host=%s --verbose-http-errors --max-transaction-time -1 --abi-serializer-max-time-ms 30000 --http-max-response-time-ms 30000 "
                        "--p2p-peer-address localhost:9011 --resource-monitor-not-shutdown-on-threshold-exceeded ") % (self.data_dir, self.config_dir, self.data_dir, "\'*\'", "false")
        start_nodeos_cmd = ("%s -e -p eosio %s %s ") % (Utils.EosServerPath, nodeos_plugins, nodeos_flags)
        self.nodeos.launchCmd(start_nodeos_cmd, self.node_id)
        time.sleep(self.sleep_s*2)
        self.nodeos.waitForBlock(1, timeout=30)

    def activateAllBuiltinProtocolFeatures(self):
        self.nodeos.activatePreactivateFeature()

        contract = "eosio.bios"
        contractDir = "libraries/testing/contracts/old_versions/v1.7.0-develop-preactivate_feature/%s" % (contract)
        wasmFile = "%s.wasm" % (contract)
        abiFile = "%s.abi" % (contract)

        eosioAccount = Account("eosio")
        eosioAccount.ownerPrivateKey = eosioAccount.activePrivateKey = self.EOSIO_ACCT_PRIVATE_DEFAULT_KEY
        eosioAccount.ownerPublicKey = eosioAccount.activePublicKey = self.EOSIO_ACCT_PUBLIC_DEFAULT_KEY

        testWalletName = "test"
        walletAccounts = [eosioAccount]
        self.keosd.create(testWalletName, walletAccounts)

        retMap = self.nodeos.publishContract(eosioAccount, contractDir, wasmFile, abiFile, waitForTransBlock=True)

        self.nodeos.preactivateAllBuiltinProtocolFeature()

    # test all chain api
    def test_ChainApi(self) :
        resource = "chain"
        command = "get_info"
        # get_info without parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("server_version", ret_json["payload"])
        # get_info with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("server_version", ret_json["payload"])
        # get_info with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)

        # activate the builtin protocol features and get some useful data
        self.activateAllBuiltinProtocolFeatures()
        allProtocolFeatures = self.nodeos.getSupportedProtocolFeatures()
        allFeatureDigests = [d['feature_digest'] for d in allProtocolFeatures["payload"]]
        allFeatureCodenames = []
        for s in allProtocolFeatures["payload"]:
           if 'specification' in s and len(s['specification']) > 0 and 'name' in s['specification'][0] and s['specification'][0]['name'] == 'builtin_feature_codename':
              allFeatureCodenames.append(s['specification'][0]['value'])
        self.assertEqual(len(allFeatureDigests), len(allFeatureCodenames))

        # Default limit set in get_activated_protocol_features_params
        ACT_FEATURE_DEFAULT_LIMIT = 10 if len(allFeatureCodenames) > 10 else len(allFeatureCodenames)

        # Actual expected activated features total
        ACT_FEATURE_CURRENT_EXPECTED_TOTAL = len(allFeatureCodenames)

        # Extemely high value to attempt to always get full list of activated features
        ACT_FEATURE_EXTREME = 10000

        # get_consensus_parameters without parameter
        command = "get_consensus_parameters"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("chain_config", ret_json["payload"])
        self.assertIn("wasm_config", ret_json["payload"])
        # get_consensus_parameters with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("chain_config", ret_json["payload"])
        self.assertIn("wasm_config", ret_json["payload"])
        # get_consensus_parameters with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)

        # get_activated_protocol_features without parameter
        command = "get_activated_protocol_features"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_DEFAULT_LIMIT)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_activated_protocol_features with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_DEFAULT_LIMIT)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)
        for index, _ in enumerate(ret_json["payload"]["activated_protocol_features"]):
            if index - 1 >= 0:
                self.assertTrue(ret_json["payload"]["activated_protocol_features"][index - 1]["activation_ordinal"] < ret_json["payload"]["activated_protocol_features"][index]["activation_ordinal"])

        # get_activated_protocol_features with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)

        # get_activated_protocol_features with 1st param
        payload = {"lower_bound":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_DEFAULT_LIMIT)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_activated_protocol_features with 2nd param
        payload = {"upper_bound":1000}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_DEFAULT_LIMIT)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_activated_protocol_features with 2nd param
        upper_bound_param = 7
        payload = {"upper_bound":upper_bound_param}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)
            self.assertTrue(dict_feature['activation_ordinal'] <= upper_bound_param)

        # get_activated_protocol_features with 3rd param
        payload = {"limit":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), 1)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_activated_protocol_features with 3rd param to get expected full list of activated features
        payload = {"limit":ACT_FEATURE_CURRENT_EXPECTED_TOTAL}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_CURRENT_EXPECTED_TOTAL)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)
        for feature in allFeatureCodenames:
            assert feature in str(ret_json["payload"]["activated_protocol_features"]), f"ERROR: Expected active feature \'{feature}\' not found in returned list."
        for digest in allFeatureDigests:
            assert digest in str(ret_json["payload"]["activated_protocol_features"]), f"ERROR: Expected active feature \'{feature}\' not found in returned list."

        # get_activated_protocol_features with 3rd param set extremely high to attempt to catch the
        # addition of new features and fail and cause this test to be updated.
        payload = {"limit":ACT_FEATURE_EXTREME}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_CURRENT_EXPECTED_TOTAL)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_activated_protocol_features with 4th param
        payload = {"search_by_block_num":"true"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_DEFAULT_LIMIT)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_activated_protocol_features with 5th param
        payload = {"reverse":"true"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        self.assertEqual(len(ret_json["payload"]["activated_protocol_features"]), ACT_FEATURE_DEFAULT_LIMIT)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)
        for index, _ in enumerate(ret_json["payload"]["activated_protocol_features"]):
            if index - 1 >= 0:
                self.assertTrue(ret_json["payload"]["activated_protocol_features"][index - 1]["activation_ordinal"] > ret_json["payload"]["activated_protocol_features"][index]["activation_ordinal"])

        # get_activated_protocol_features with valid parameter
        payload = {"lower_bound":1,
                   "upper_bound":1000,
                   "limit":10,
                   "search_by_block_num":"true",
                   "reverse":"true"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["activated_protocol_features"]), list)
        for dict_feature in ret_json["payload"]["activated_protocol_features"]:
            self.assertTrue(dict_feature['feature_digest'] in allFeatureDigests)

        # get_block with empty parameter
        command = "get_block"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        # get_block with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        # get_block with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        # get_block with valid parameter
        payload = {"block_num_or_id":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["payload"]["block_num"], 1)

        # get_raw_block with empty parameter
        command = "get_raw_block"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        # get_block with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        # get_block with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        # get_block with valid parameter
        payload = {"block_num_or_id":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertTrue("action_mroot" in ret_json["payload"])

        # get_block_header with empty parameter
        command = "get_block_header"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        # get_block with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        # get_block with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        # get_block with valid parameters
        payload = {"block_num_or_id":1, "include_extensions": True}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertTrue("id" in ret_json["payload"])
        self.assertTrue("signed_block_header" in ret_json["payload"])
        self.assertTrue("block_extensions" in ret_json["payload"])
        payload = {"block_num_or_id":1, "include_extensions": False}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertTrue("id" in ret_json["payload"])
        self.assertTrue("signed_block_header" in ret_json["payload"])
        self.assertFalse("block_extensions" in ret_json["payload"])

        # get_block_info with empty parameter
        command =  "get_block_info"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        # get_block_info with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        # get_block_info with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        # get_block_info with valid parameter
        payload = {"block_num":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["payload"]["block_num"], 1)

        # get_block_header_state with empty parameter
        command = "get_block_header_state"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_block_header_state with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_block_header_state with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_block_header_state with valid parameter, the irreversible is not available, unknown block number
        payload = {"block_num_or_id":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3100002)

        # get_account with empty parameter
        command = "get_account"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_account with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_account with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_account with valid parameter
        payload = {"account_name":"default"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_code with empty parameter
        command = "get_code"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_code with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_code with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_code with valid parameter
        payload = {"account_name":"default"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_code_hash with empty parameter
        command = "get_code_hash"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_code_hash with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_code_hash with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_code_hash with valid parameter
        payload = {"account_name":"default"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_abi with empty parameter
        command = "get_abi"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_abi with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_abi with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_abi with valid parameter
        payload = {"account_name":"default"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_raw_code_and_abi with empty parameter
        command = "get_raw_code_and_abi"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_raw_code_and_abi with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_raw_code_and_abi with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_raw_code_and_abi with valid parameter
        payload = {"account_name":"default"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_raw_abi with empty parameter
        command = "get_raw_abi"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_raw_abi with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_raw_abi with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_raw_abi with valid parameter
        payload = {"account_name":"default"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_table_rows with empty parameter
        command = "get_table_rows"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_table_rows with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_table_rows with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_table_rows with valid parameter
        payload = {"json":"true",
                   "code":"cancancan345",
                   "scope":"cancancan345",
                   "table":"vote",
                   "index_position":2,
                   "key_type":"i128",
                   "lower_bound":"0x0000000000000000D0F2A472A8EB6A57",
                   "upper_bound":"0xFFFFFFFFFFFFFFFFD0F2A472A8EB6A57"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_table_by_scope with empty parameter
        command = "get_table_by_scope"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_table_by_scope with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_table_by_scope with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_table_by_scope with valid parameter
        payload = {"code":"cancancan345",
                   "table":"vote",
                   "index_position":2,
                   "lower_bound":"0x0000000000000000D0F2A472A8EB6A57",
                   "upper_bound":"0xFFFFFFFFFFFFFFFFD0F2A472A8EB6A57"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_currency_balance with empty parameter
        command = "get_currency_balance"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_currency_balance with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_currency_balance with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_currency_balance with valid parameter
        payload = {"code":"eosio.token", "account":"unknown"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_currency_stats with empty parameter
        command = "get_currency_stats"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_currency_stats with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_currency_stats with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_currency_stats with valid parameter
        payload = {"code":"eosio.token","symbol":"SYS"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_producers with empty parameter
        command = "get_producers"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_producers with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_producers with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_producers with valid parameter
        payload = {"json":"true","lower_bound":""}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["rows"]), list)

        # get_producer_schedule with empty parameter
        command = "get_producer_schedule"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(type(ret_json["payload"]["active"]), dict)
        # get_producer_schedule with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(type(ret_json["payload"]["active"]), dict)
        # get_producer_schedule with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # get_scheduled_transactions with empty parameter
        command = "get_scheduled_transactions"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_scheduled_transactions with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_scheduled_transactions with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_scheduled_transactions with valid parameter
        payload = {"json":"true","lower_bound":""}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(type(ret_json["payload"]["transactions"]), list)

        # get_required_keys with empty parameter
        command = "get_required_keys"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_required_keys with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_required_keys with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_required_keys with valid parameter
        payload = {"ref_block_num":"100",
                   "ref_block_prefix": "137469861",
                   "expiration": "2020-09-25T06:28:49",
                   "scope":["initb", "initc"],
                   "actions": [{"code": "currency","type":"transfer","recipients": ["initb", "initc"],"authorization": [{"account": "initb", "permission": "active"}],"data":"000000000041934b000000008041934be803000000000000"}],
                   "signatures": [],
                   "authorizations": [],
                   "available_keys":["EOS4toFS3YXEQCkuuw1aqDLrtHim86Gz9u3hBdcBw5KNPZcursVHq",
                   "EOS7d9A3uLe6As66jzN8j44TXJUqJSK3bFjjEEqR4oTvNAB3iM9SA",
                   "EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV"]}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # get_transaction_id with empty parameter
        command = "get_transaction_id"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_transaction_id with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_transaction_id with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_transaction_id with valid parameter
        payload = {"expiration":"2020-08-01T07:15:49",
                   "ref_block_num": 34881,
                   "ref_block_prefix":2972818865,
                   "max_net_usage_words":0,
                   "max_cpu_usage_ms":0,
                   "delay_sec":0,
                   "context_free_actions":[],
                   "actions":[{"account":"eosio.token","name": "transfer","authorization": [{"actor": "han","permission": "active"}],"data": "000000000000a6690000000000ea305501000000000000000453595300000000016d"}],
                   "transaction_extensions": [],
                   "signatures": ["SIG_K1_KeqfqiZu1GwUxQb7jzK9Fdks6HFaVBQ9AJtCZZj56eG9qGgvVMVtx8EerBdnzrhFoX437sgwtojf2gfz6S516Ty7c22oEp"],
                   "context_free_data": []}
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw).decode('ascii')
        self.assertEqual(ret_str, "\"0be762a6406bab15530e87f21e02d1c58e77944ee55779a76f4112e3b65cac48\"")

        # push_block with empty parameter
        command = "push_block"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_block with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_block with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_block with valid parameter
        payload = {"block":"signed_block"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(len(ret_json["payload"]), 0)

        # push_transaction with empty parameter
        command = "push_transaction"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_transaction with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_transaction with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_transaction with valid parameter
        payload = {"signatures":["SIG_K1_KeqfqiZu1GwUxQb7jzK9Fdks6HFaVBQ9AJtCZZj56eG9qGgvVMVtx8EerBdnzrhFoX437sgwtojf2gfz6S516Ty7c22oEp"],
                   "compression": "true",
                   "packed_context_free_data": "context_free_data",
                   "packed_trx": "packed_trx"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)

        # push_transactions with empty parameter
        command = "push_transactions"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_transactions with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_transactions with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # push_transactions with valid parameter
        payload = [{"signatures":["SIG_K1_KeqfqiZu1GwUxQb7jzK9Fdks6HFaVBQ9AJtCZZj56eG9qGgvVMVtx8EerBdnzrhFoX437sgwtojf2gfz6S516Ty7c22oEp"],
                    "compression": "true",
                    "packed_context_free_data": "context_free_data",
                    "packed_trx": "packed_trx"}]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn("transaction_id", ret_json["payload"][0])

        # send_transaction with empty parameter
        command = "send_transaction"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # send_transaction with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # send_transaction with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # send_transaction with valid parameter
        payload = {"signatures":["SIG_K1_KeqfqiZu1GwUxQb7jzK9Fdks6HFaVBQ9AJtCZZj56eG9qGgvVMVtx8EerBdnzrhFoX437sgwtojf2gfz6S516Ty7c22oEp"],
                   "compression": "true",
                   "packed_context_free_data": "context_free_data",
                   "packed_trx": "packed_trx"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 500)


    # test all net api
    def test_NetApi(self) :
        resource = "net"

        # connect with empty parameter
        command = "connect"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # connect with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        payload = "localhost"
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw).decode('ascii')
        self.assertEqual("\"added connection\"", ret_str)

        # disconnect with empty parameter
        command = "disconnect"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # disconnect with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # disconnect with valid parameter
        payload = "localhost123"
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw).decode('ascii')
        self.assertEqual("\"no known connection for host\"", ret_str)

        # status with empty parameter
        command = "status"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # status with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # status with valid parameter
        payload = "localhost"
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw).decode('ascii')
        self.assertEqual(ret_str, "null")

        # connections with empty parameter
        command = "connections"
        ret_str = self.nodeos.processUrllibRequest(resource, command, returnType=ReturnType.raw).decode('ascii')
        self.assertIn("\"peer\":\"localhost:9011\"", ret_str)
        # connections with empty content parameter
        ret_str = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, returnType=ReturnType.raw).decode('ascii')
        self.assertIn("\"peer\":\"localhost:9011\"", ret_str)
        # connections with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

    # test all producer api
    def test_ProducerApi(self) :
        resource = "producer"

        # pause with empty parameter
        command = "pause"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["payload"]["result"], "ok")
        # pause with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["payload"]["result"], "ok")
        # pause with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # resume with empty parameter
        command = "resume"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["payload"]["result"], "ok")
        # resume with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["payload"]["result"], "ok")
        # resume with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # paused with empty parameter
        command = "paused"
        ret_str = self.nodeos.processUrllibRequest(resource, command, returnType=ReturnType.raw).decode('ascii')
        self.assertEqual(ret_str, "false")
        # paused with empty content parameter
        ret_str = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, returnType=ReturnType.raw).decode('ascii')
        self.assertEqual(ret_str, "false")
        # paused with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # get_runtime_options with empty parameter
        command = "get_runtime_options"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("max_transaction_time", ret_json["payload"])
        # get_runtime_options with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("max_transaction_time", ret_json["payload"])
        # get_runtime_options with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # update_runtime_options with empty parameter
        command = "update_runtime_options"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # update_runtime_options with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # update_runtime_options with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # update_runtime_options with valid parameter
        payload = {"max_transaction_time":30,
                   "max_irreversible_block_age":1,
                   "produce_time_offset_us":10000,
                   "last_block_time_offset_us":0,
                   "max_scheduled_transaction_time_per_block_ms":10000,
                   "subjective_cpu_leeway_us":0,
                   "incoming_defer_ratio":1.0,
                   "greylist_limit":100}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn(ret_json["payload"]["result"], "ok")

        # add_greylist_accounts with empty parameter
        command = "add_greylist_accounts"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # add_greylist_accounts with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # add_greylist_accounts with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # add_greylist_accounts with valid parameter
        payload = {"accounts":["test1", "test2"]}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn(ret_json["payload"]["result"], "ok")

        # remove_greylist_accounts with empty parameter
        command = "remove_greylist_accounts"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # remove_greylist_accounts with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # remove_greylist_accounts with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # remove_greylist_accounts with valid parameter
        payload = {"accounts":["test1", "test2"]}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn(ret_json["payload"]["result"], "ok")

        # get_greylist with empty parameter
        command = "get_greylist"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("accounts", ret_json["payload"])
        # get_greylist with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("accounts", ret_json["payload"])
        # get_greylist with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # get_whitelist_blacklist with empty parameter
        command = "get_whitelist_blacklist"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("actor_whitelist", ret_json["payload"])
        self.assertIn("actor_blacklist", ret_json["payload"])
        self.assertIn("contract_whitelist", ret_json["payload"])
        self.assertIn("contract_blacklist", ret_json["payload"])
        self.assertIn("action_blacklist", ret_json["payload"])
        self.assertIn("key_blacklist", ret_json["payload"])
        # get_whitelist_blacklist with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("actor_whitelist", ret_json["payload"])
        self.assertIn("actor_blacklist", ret_json["payload"])
        self.assertIn("contract_whitelist", ret_json["payload"])
        self.assertIn("contract_blacklist", ret_json["payload"])
        self.assertIn("action_blacklist", ret_json["payload"])
        self.assertIn("key_blacklist", ret_json["payload"])
        # get_whitelist_blacklist with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # set_whitelist_blacklist with empty parameter
        command = "set_whitelist_blacklist"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # set_whitelist_blacklist with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # set_whitelist_blacklist with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # set_whitelist_blacklist with valid parameter
        payload = {"actor_whitelist":["test2"],
                   "actor_blacklist":["test3"],
                   "contract_whitelist":["test4"],
                   "contract_blacklist":["test5"],
                   "action_blacklist":[],
                   "key_blacklist":[]}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn(ret_json["payload"]["result"], "ok")

        # get_integrity_hash with empty parameter
        command = "get_integrity_hash"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("head_block_id", ret_json["payload"])
        self.assertIn("integrity_hash", ret_json["payload"])
        # get_integrity_hash with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("head_block_id", ret_json["payload"])
        self.assertIn("integrity_hash", ret_json["payload"])
        # get_integrity_hash with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # create_snapshot with empty parameter
        command = "create_snapshot"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("head_block_id", ret_json["payload"])
        self.assertIn("snapshot_name", ret_json["payload"])

        # get_scheduled_protocol_feature_activations with empty parameter
        command = "get_scheduled_protocol_feature_activations"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("protocol_features_to_activate", ret_json["payload"])
        # get_scheduled_protocol_feature_activations with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("protocol_features_to_activate", ret_json["payload"])
        # get_scheduled_protocol_feature_activations with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # schedule_protocol_feature_activations with empty parameter
        command = "schedule_protocol_feature_activations"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # schedule_protocol_feature_activations with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # schedule_protocol_feature_activations with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # schedule_protocol_feature_activations with valid parameter
        payload = {"protocol_features_to_activate":[]}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn(ret_json["payload"]["result"], "ok")

        # get_supported_protocol_features with empty parameter
        command = "get_supported_protocol_features"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("feature_digest", ret_json["payload"][0])
        self.assertIn("subjective_restrictions", ret_json["payload"][0])
        # get_supported_protocol_features with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("feature_digest", ret_json["payload"][0])
        self.assertIn("subjective_restrictions", ret_json["payload"][0])
        # get_supported_protocol_features with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_supported_protocol_features with 1st parameter
        payload = {"exclude_disabled":"true"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn("feature_digest", ret_json["payload"][0])
        self.assertIn("subjective_restrictions", ret_json["payload"][0])
        # get_supported_protocol_features with 2nd parameter
        payload = {"exclude_unactivatable":"true"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn("feature_digest", ret_json["payload"][0])
        self.assertIn("subjective_restrictions", ret_json["payload"][0])
        # get_supported_protocol_features with valid parameter
        payload = {"exclude_disabled":"true", "exclude_unactivatable":"true"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn("feature_digest", ret_json["payload"][0])
        self.assertIn("subjective_restrictions", ret_json["payload"][0])

        # get_account_ram_corrections with empty parameter
        command = "get_account_ram_corrections"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_account_ram_corrections with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_account_ram_corrections with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_account_ram_corrections with valid parameter
        payload = {"lower_bound":"", "upper_bound":"", "limit":1, "reverse":"false"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn("rows", ret_json["payload"])

        # get_unapplied_transactions with empty parameter
        command = "get_unapplied_transactions"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("size", ret_json["payload"])
        self.assertIn("incoming_size", ret_json["payload"])
        # get_unapplied_transactions with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("size", ret_json["payload"])
        self.assertIn("incoming_size", ret_json["payload"])
        # get_unapplied_transactions with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # get_unapplied_transactions with valid parameter
        payload = {"lower_bound":"", "limit":1, "time_limit_ms":500}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertIn("trxs", ret_json["payload"])

    # test all wallet api
    def test_WalletApi(self) :
        endpoint = self.base_wallet_cmd_str
        resource = "wallet"

        # set_timeout with empty parameter
        command = "set_timeout"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # set_timeout with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # set_timeout with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # set_timeout with valid parameter
        payload = "1234567"
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw, endpoint=endpoint).decode('ascii')
        self.assertEqual("{}", ret_str)

        # sign_transaction with empty parameter
        command = "sign_transaction"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # sign_transaction with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # sign_transaction with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # sign_transaction with valid parameter
        signed_transaction = {"expiration":"2020-08-01T07:15:49",
                              "ref_block_num": 34881,
                              "ref_block_prefix":2972818865,
                              "max_net_usage_words":0,
                              "max_cpu_usage_ms":0,
                              "delay_sec":0,
                              "context_free_actions":[],
                              "actions": [],
                              "transaction_extensions": [],
                              "signatures": ["SIG_K1_KeqfqiZu1GwUxQb7jzK9Fdks6HFaVBQ9AJtCZZj56eG9qGgvVMVtx8EerBdnzrhFoX437sgwtojf2gfz6S516Ty7c22oEp"],
                              "context_free_data": []}
        payload = [signed_transaction,
                   ["EOS696giL6VxeJhtEgKtWPK8aQeT8YXNjw2a7vE5wHunffhfa5QSQ"],
                   "cf057bbfb72640471fd910bcb67639c22df9f92470936cddc1ade0e2f2e7dc4f"]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)
        self.assertEqual(ret_json["error"]["code"], 3120004)

        # create with empty parameter
        command = "create"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # create with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # create with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)
        self.assertEqual(ret_json["error"]["code"], 3120000)
        # create with valid parameter
        payload = "test1"
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw, endpoint=endpoint).decode('ascii')
        self.assertTrue( ret_str)

        # open with empty parameter
        command = "open"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # open with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # create with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)
        self.assertEqual(ret_json["error"]["code"], 3120000)
        # create with valid parameter
        payload = "fakeacct"
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)

        # lock_all with empty parameter
        command = "lock_all"
        ret_str = self.nodeos.processUrllibRequest(resource, command, returnType=ReturnType.raw, endpoint=endpoint).decode('ascii')
        self.assertEqual("{}", ret_str)
        # lock_all with empty content parameter
        ret_str = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, returnType=ReturnType.raw, endpoint=endpoint).decode('ascii')
        self.assertEqual("{}", ret_str)
        # lock_all with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # lock with empty parameter
        command = "lock"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # lock with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # lock with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)
        self.assertEqual(ret_json["error"]["code"], 3120002)
        # lock with valid parameter
        payload = {"name":"auser"}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)

        # unlock with empty parameter
        command = "unlock"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # unlock with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # unlock with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # unlock with valid parameter
        payload = ["auser", "nopassword"]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)

        # import_key with empty parameter
        command = "import_key"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # import_key with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # import_key with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # import_key with valid parameter
        payload = ["auser", "nokey"]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)

        # remove_key with empty parameter
        command = "remove_key"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # remove_key with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # remove_key with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # remove_key with valid parameter
        payload = ["auser", "none", "none"]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)

        # create_key with empty parameter
        command = "create_key"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # create_key with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # create_key with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # create_key with valid parameter
        payload = ["auser", "none"]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)

        # list_wallets with empty parameter
        command = "list_wallets"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(type(ret_json["payload"]), list)
        # list_wallets with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(type(ret_json["payload"]), list)
        # list_wallets with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

        # list_keys with empty parameter
        command = "list_keys"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # list_keys with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # list_keys with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # list_keys with valid parameter
        payload = ["auser", "unknownkey"]
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)

        # get_public_keys with empty parameter
        command = "get_public_keys"
        ret_json = self.nodeos.processUrllibRequest(resource, command, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)
        # get_public_keys with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 500)
        # list_wallets with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param, endpoint=endpoint)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)

    # test all test control api
    def test_TestControlApi(self) :
        resource = "test_control"

        # kill_node_on_producer with empty parameter
        command = "kill_node_on_producer"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # kill_node_on_producer with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # kill_node_on_producer with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        self.assertEqual(ret_json["error"]["code"], 3200006)
        # kill_node_on_producer with valid parameter
        payload = {"name":"auser", "where_in_sequence":12, "based_on_lib":"true"}
        ret_str = self.nodeos.processUrllibRequest(resource, command, payload, returnType=ReturnType.raw).decode('ascii')
        self.assertIn("{}", ret_str)

    # test all trace api
    def test_TraceApi(self) :
        resource = "trace_api"

        # get_block with empty parameter
        command = "get_block"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertEqual(ret_json["code"], 400)
        # get_block with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertEqual(ret_json["code"], 400)
        # get_block with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)
        # get_block with valid parameter
        payload = {"block_num":1}
        ret_json = self.nodeos.processUrllibRequest(resource, command, payload)
        self.assertEqual(ret_json["code"], 404)
        self.assertEqual(ret_json["error"]["code"], 0)

    # test all db_size api
    def test_DbSizeApi(self) :
        resource = "db_size"

        # get with empty parameter
        command = "get"
        ret_json = self.nodeos.processUrllibRequest(resource, command)
        self.assertIn("free_bytes", ret_json["payload"])
        self.assertIn("used_bytes", ret_json["payload"])
        self.assertIn("size", ret_json["payload"])
        self.assertIn("indices", ret_json["payload"])
        # get with empty content parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.empty_content_dict)
        self.assertIn("free_bytes", ret_json["payload"])
        self.assertIn("used_bytes", ret_json["payload"])
        self.assertIn("size", ret_json["payload"])
        self.assertIn("indices", ret_json["payload"])
        # get with invalid parameter
        ret_json = self.nodeos.processUrllibRequest(resource, command, self.http_post_invalid_param)
        self.assertEqual(ret_json["code"], 400)

    # test prometheus api
    def test_prometheusApi(self) :
        resource = "prometheus"
        command = "metrics"

        ret_text = self.nodeos.processUrllibRequest(resource, command, returnType = ReturnType.raw ).decode()
        # filter out all empty lines or lines starting with '#'
        data_lines = filter(lambda line: len(line) > 0 and line[0]!='#', ret_text.split('\n'))
        # converting each line into a key value pair and then construct a dictionay out of all the pairs
        metrics = dict(map(lambda line: tuple(line.split(' ')), data_lines))

        self.assertTrue(int(metrics["head_block_num"]) > 1)
        self.assertTrue(int(metrics["blocks_produced"]) > 1)
        self.assertTrue(int(metrics["last_irreversible"]) > 1)


    def test_multipleRequests(self):
        """Test keep-alive ability of HTTP plugin.  Handle multiple requests in a single session"""
        host = self.nodeos.host
        port = self.nodeos.port
        addr = (host, port)
        body1 = '{ "block_num_or_id": "1" }\r\n'
        body2 = '{ "block_num_or_id": "2" }\r\n'
        body3 = '{ "block_num_or_id": "3" }\r\n'
        api_call = "/v1/chain/get_block"
        req1 = Utils.makeHTTPReqStr(host, str(port), api_call, body1, True)
        req2 = Utils.makeHTTPReqStr(host, str(port), api_call, body2, True)
        req3 = Utils.makeHTTPReqStr(host, str(port), api_call, body3, False)

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        try:
            sock.connect(addr)
        except Exception as e:
            print(f"unable to connect to {host}:{port}")
            print(e)
            Utils.errorExit("Failed to connect to nodeos.")

        enc = "utf-8"
        sock.settimeout(3)
        maxMsgSize = 2048
        resp1_data, resp2_data, resp3_data = None, None, None
        try:
            # send first request
            Utils.Print('sending request 1')
            sock.send(bytes(req1, enc))
            resp1_data = Utils.readSocketDataStr(sock, maxMsgSize, enc)
            Utils.Print('resp1_data= \n', resp1_data)

            # send second request
            Utils.Print('sending request 2')
            sock.send(bytes(req2, enc))
            resp2_data = Utils.readSocketDataStr(sock, maxMsgSize, enc)
            Utils.Print('resp2_data= \n', resp2_data)

            # send third request
            Utils.Print('sending request 3')
            sock.send(bytes(req3, enc))
            resp3_data = Utils.readSocketDataStr(sock, maxMsgSize, enc)
            Utils.Print('resp3_data= \n', resp3_data)


            # wait for socket to close
            time.sleep(0.5)
            # send request 2 again.  this should fail because request 3 has "Connection: close" in header
            Utils.Print('sending request 2 again')
            try:
                sock.settimeout(3)
                sock.send(bytes(req2, enc))
                d = sock.recv(64)
                if(len(d) > 0):
                    Utils.errorExit('Socket still open after "Connection: close" in header')
            except Exception as e:
                pass

            Utils.Print("Socket connection closed as expected")

        except Exception as e:
            Utils.Print(e)
            Utils.errorExit("Failed to send/receive on socket")

        # extract response body
        resp1_json, resp2_json, resp3_json = None, None, None
        try:
            (hdr, resp1_json) = re.split('\r\n\r\n', resp1_data)
            (hdr, resp2_json) = re.split('\r\n\r\n', resp2_data)
            (hdr, resp3_json) = re.split('\r\n\r\n', resp3_data)
        except Exception as e:
            Utils.Print(e)
            Utils.errorExit("Improper HTTP response(s)")

        resp1, resp2, resp3 = None, None, None
        try:
            resp1 = json.loads(resp1_json)
            resp2 = json.loads(resp2_json)
            resp3 = json.loads(resp3_json)
        except Exception as e:
            Utils.Print(e)
            Utils.errorExit("Could not parse JSON response")

        self.assertIn('block_num', resp1)
        self.assertIn('block_num', resp2)
        self.assertIn('block_num', resp3)
        self.assertEqual(resp1['block_num'], 1)
        self.assertEqual(resp2['block_num'], 2)
        self.assertEqual(resp3['block_num'], 3)

    @classmethod
    def setUpClass(self):
        self.cleanEnv(self)
        self.startEnv(self)

    @classmethod
    def tearDownClass(self):
        self.cleanEnv(self)

if __name__ == "__main__":
    unittest.main()
