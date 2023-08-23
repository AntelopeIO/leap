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
total_nodes=3
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
        '0': '--agent-name node-00 --p2p-listen-endpoint 0.0.0.0:9876 --plugin eosio::net_api_plugin',
        '1': '--agent-name node-01 --p2p-listen-endpoint 0.0.0.0:9877 --p2p-peer-address localhost:9876:peer --plugin eosio::net_api_plugin',
        '2': '--agent-name node-02 --p2p-listen-endpoint 0.0.0.0:9878 --p2p-peer-address localhost:9876:peer --plugin eosio::net_api_plugin',
    }
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo='line', delay=delay, 
                      specificExtraNodeosArgs=specificArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    cluster.waitOnClusterSync(blockAdvancing=5)
    # Shut down bios node, which is connected to all other nodes in all topologies
    cluster.biosNode.kill(signal.SIGTERM)

    connections = cluster.nodes[2].processUrllibRequest('net', 'connections')
    open_socket_count = 0
    for conn in connections['payload']:
        if conn['is_socket_open']:
            open_socket_count += 1
            if conn['last_handshake']['agent'] == 'node-01':
                assert conn['last_handshake']['p2p_address'].split()[0] == 'localhost:9877', f"Connected node is listening on '{conn['last_handshake']['p2p_address'].split()[0]}' instead of port 9877"
            elif conn['last_handshake']['agent'] == 'node-00':
                assert conn['last_handshake']['p2p_address'].split()[0] == 'localhost:9876:peer', f"Connected node is listening on '{conn['last_handshake']['p2p_address'].split()[0]}' instead of port 9876"
    # Node 0 will send Node 1 peer info to Node 2
    assert open_socket_count == 2, 'Node 2 is expected to connect Node 0 & 1'

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
