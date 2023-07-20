#!/usr/bin/env python3

import signal

from TestHarness import Cluster, TestHelper, Utils, WalletMgr

###############################################################
# p2p_multiple_listen_test
#
# Test nodeos ability to listen on multiple ports for p2p
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args=TestHelper.parse_args({"-p","-n","-d","--keep-logs"
                            ,"--dump-error-details","-v"
                            ,"--leave-running","--unshared"})
pnodes=args.p
delay=args.d
debug=args.v
total_nodes=4
dumpErrorDetails=args.dump_error_details

Utils.Debug=debug
testSuccessful=False

cluster=Cluster(unshared=args.unshared, keepRunning=args.leave_running, keepLogs=args.keep_logs)
walletMgr=WalletMgr(True)

try:
    TestHelper.printSystemInfo("BEGIN")

    cluster.setWalletMgr(walletMgr)

    Print(f'producing nodes: {pnodes}, delay between nodes launch: {delay} second{"s" if delay != 1 else ""}')

    Print("Stand up cluster")
    specificArgs = {
        '0': '--agent-name node-00 --p2p-listen-endpoint 0.0.0.0:9779 --p2p-server-address localhost:9779 --plugin eosio::net_api_plugin',
        '2': '--agent-name node-02 --p2p-peer-address localhost:9779 --plugin eosio::net_api_plugin',
    }
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo='ring', delay=delay, 
                      specificExtraNodeosArgs=specificArgs) is False:
        errorExit("Failed to stand up eos cluster.")
    
    cluster.waitOnClusterSync(blockAdvancing=5)
    cluster.biosNode.kill(signal.SIGTERM)
    cluster.getNode(1).kill(signal.SIGTERM)
    cluster.getNode(3).kill(signal.SIGTERM)
    cluster.waitOnClusterSync(blockAdvancing=5)
    connections = cluster.nodes[0].processUrllibRequest('net', 'connections')
    open_socket_count = 0
    for conn in connections['payload']:
        if conn['is_socket_open']:
            open_socket_count += 1
            assert conn['last_handshake']['agent'] == 'node-02', f'Connected node identifed as "{conn["last_handshake"]["agent"]}" instead of node-02'
            assert conn['last_handshake']['p2p_address'][:14] == 'localhost:9878', 'Connected node is not listening on port 9878'
    assert open_socket_count == 1, 'Node 0 is expected to have only one open socket'
    connections = cluster.nodes[2].processUrllibRequest('net', 'connections')
    open_socket_count = 0
    for conn in connections['payload']:
        if conn['is_socket_open']:
            open_socket_count += 1
            assert conn['last_handshake']['agent'] == 'node-00', f'Connected node identifed as "{conn["last_handshake"]["agent"]}" instead of node-00'
            assert conn['last_handshake']['p2p_address'][:14] == 'localhost:9779', 'Connected node is not listening on port 9779'
    assert open_socket_count == 1, 'Node 2 is expected to have only one open socket'

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
