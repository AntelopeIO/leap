#!/usr/bin/env python3
import os
import sys
import re

harnessPath = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.append(harnessPath)

from testUtils import Account
from testUtils import Utils
from Cluster import Cluster
from WalletMgr import WalletMgr
from Node import Node
from Node import ReturnType
from TestHelper import TestHelper

Print = Utils.Print
errorExit = Utils.errorExit
cmdError = Utils.cmdError
relaunchTimeout = 30

args=TestHelper.parse_args({"-p","-n","-d","-s","--nodes-file"
                            ,"--dump-error-details","-v","--leave-running"
                            ,"--clean-run","--keep-logs"})

pnodes=args.p
topo=args.s
delay=args.d
total_nodes = pnodes if args.n < pnodes else args.n
Utils.Debug = args.v
killAll=args.clean_run
dumpErrorDetails=args.dump_error_details
dontKill=args.leave_running
killEosInstances = not dontKill
killWallet=not dontKill
keepLogs=True

# Setup cluster and its wallet manager
walletMgr=WalletMgr(True)
cluster=Cluster(walletd=True)
cluster.setWalletMgr(walletMgr)

testSuccessful = False

log_path = "var/lib/node_00/"
log_name = r"stderr\..*\.txt"
transaction_regex = r'trxs:\s+\d+'
int_regex = r'\d+'

for filename in os.listdir(log_path):
    if re.match(log_name, filename):
        # with open(os.path.join(log_path, filename), 'r') as f:
        print("\n\n\n\ntrying to remove: ")
        print(log_path + filename)
        os.remove(log_path + filename)
try:
    # Kill any existing instances and launch cluster
    TestHelper.printSystemInfo("BEGIN")
    cluster.killall(allInstances=killAll)
    cluster.cleanup()
    extraNodeosArgs=' --http-max-response-time-ms 990000 --disable-subjective-api-billing false --plugin eosio::trace_api_plugin --trace-no-abis '
    if cluster.launch(
       pnodes=pnodes,
       totalNodes=total_nodes,
       useBiosBootFile=False,
       topo=topo,
       extraNodeosArgs=extraNodeosArgs) == False:
        errorExit('Failed to stand up cluster.')

    wallet = walletMgr.create('default')
    cluster.populateWallet(2, wallet)
    cluster.createAccounts(cluster.eosioAccount, stakedDeposit=0)
    account1Name = cluster.accounts[0].name
    account2Name = cluster.accounts[1].name

    account1PrivKey = cluster.accounts[0].activePrivateKey
    account2PrivKey = cluster.accounts[1].activePrivateKey

    node0 = cluster.getNode()
    info = node0.getInfo()
    chainId = info['chain_id']
    lib_id = info['last_irreversible_block_id']

    transactions_to_send = 73

    # if Utils.Debug: Print(f'Running trx_generator: ./tests/trx_generator/trx_generator  --chain-id {chainId} --last-irreversible-block-id {lib_id} --handler-account {cluster.eosioAccount.name} --accounts {account1Name},{account2Name} --priv-keys {account1PrivKey},{account2PrivKey}')
    node0.waitForLibToAdvance(30)
    # Utils.runCmdReturnStr(f'./tests/trx_generator/trx_generator  --chain-id {chainId} --last-irreversible-block-id {lib_id} --handler-account {cluster.eosioAccount.name} --accounts {account1Name},{account2Name} --priv-keys {account1PrivKey},{account2PrivKey}')
    node0.waitForLibToAdvance(30)

    testSuccessful = True
finally:
    TestHelper.shutdown(
        cluster,
        walletMgr,
        testSuccessful,
        killEosInstances,
        killWallet,
        keepLogs,
        killAll,
        dumpErrorDetails
    )

for filename in os.listdir(log_name):
    if re.match(log_path, filename):
        with open(os.path.join(log_name, filename), 'r') as f:
            total = 0
            string_result = re.findall(transaction_regex, f.read())
            for value in string_result:
                int_result = re.findall(int_regex, value)
                total += int(int_result[0])
            f.close()
            if transactions_to_send != total:
                testSuccessful = False
                print("Error: Transactions received: %d did not match expected total: %d" % (total, transactions_to_send))

exitCode = 0 if testSuccessful else 1
exit(exitCode)
