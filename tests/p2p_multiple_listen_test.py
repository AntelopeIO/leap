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
                            ,"--activate-if","--dump-error-details","-v"
                            ,"--leave-running","--unshared"})
pnodes=args.p
delay=args.d
debug=args.v
total_nodes=5
activateIF=args.activate_if
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
        '0': '--agent-name node-00 --p2p-listen-endpoint 0.0.0.0:9876 --p2p-listen-endpoint 0.0.0.0:9779 --p2p-server-address ext-ip0:20000 --p2p-server-address ext-ip1:20001 --plugin eosio::net_api_plugin',
        '2': '--agent-name node-02 --p2p-peer-address localhost:9779 --plugin eosio::net_api_plugin',
        '4': '--agent-name node-04 --p2p-peer-address localhost:9876 --plugin eosio::net_api_plugin',
    }
    if cluster.launch(pnodes=pnodes, totalNodes=total_nodes, topo='line', delay=delay, activateIF=activateIF,
                      specificExtraNodeosArgs=specificArgs) is False:
        errorExit("Failed to stand up eos cluster.")

    # Be sure all nodes start out connected   (bios node omitted from diagram for brevity)
    #     node00              node01            node02            node03            node04
    #   localhost:9876 -> localhost:9877 -> localhost:9878 -> localhost:9879 -> localhost:9880
    # localhost:9779 ^                           |                                   |
    #       ^        +---------------------------+                                   |
    #       +------------------------------------------------------------------------+
    cluster.waitOnClusterSync(blockAdvancing=5)
    # Shut down bios node, which is connected to all other nodes in all topologies
    cluster.biosNode.kill(signal.SIGTERM)
    # Shut down second node, interrupting the default connections between it and nodes 00 and 02
    cluster.getNode(1).kill(signal.SIGTERM)
    # Shut down the fourth node, interrupting the default connections between it and nodes 02 and 04
    cluster.getNode(3).kill(signal.SIGTERM)
    # Be sure all remaining nodes continue to sync via the two listen ports on node 00
    #     node00            node01              node02            node03            node04
    #   localhost:9876     offline          localhost:9878       offline        localhost:9880
    # localhost:9779 ^                           |                                   |
    #       ^        +---------------------------+                                   |
    #       +------------------------------------------------------------------------+
    cluster.waitOnClusterSync(blockAdvancing=5)
    connections = cluster.nodes[0].processUrllibRequest('net', 'connections')
    open_socket_count = 0
    for conn in connections['payload']:
        if conn['is_socket_open']:
            open_socket_count += 1
            if conn['last_handshake']['agent'] == 'node-02':
                assert conn['last_handshake']['p2p_address'].split()[0] == 'localhost:9878', f"Connected node is listening on '{conn['last_handshake']['p2p_address'].split()[0]}' instead of port 9878"
            elif conn['last_handshake']['agent'] == 'node-04':
                assert conn['last_handshake']['p2p_address'].split()[0] == 'localhost:9880', f"Connected node is listening on '{conn['last_handshake']['p2p_address'].split()[0]}' instead of port 9880"
    assert open_socket_count == 2, 'Node 0 is expected to have exactly two open sockets'

    connections = cluster.nodes[2].processUrllibRequest('net', 'connections')
    open_socket_count = 0
    for conn in connections['payload']:
        if conn['is_socket_open']:
            open_socket_count += 1
            assert conn['last_handshake']['agent'] == 'node-00', f"Connected node identifed as '{conn['last_handshake']['agent']}' instead of node-00"
            assert conn['last_handshake']['p2p_address'].split()[0] == 'ext-ip0:20000', f"Connected node is advertising '{conn['last_handshake']['p2p_address'].split()[0]}' instead of ext-ip0:20000"
    assert open_socket_count == 1, 'Node 2 is expected to have exactly one open socket'

    connections = cluster.nodes[4].processUrllibRequest('net', 'connections')
    open_socket_count = 0
    for conn in connections['payload']:
        if conn['is_socket_open']:
            open_socket_count += 1
            assert conn['last_handshake']['agent'] == 'node-00', f"Connected node identifed as '{conn['last_handshake']['agent']}' instead of node-00"
            assert conn['last_handshake']['p2p_address'].split()[0] == 'ext-ip1:20001', f"Connected node is advertising '{conn['last_handshake']['p2p_address'].split()[0]} 'instead of ext-ip1:20001"
    assert open_socket_count == 1, 'Node 4 is expected to have exactly one open socket'

    testSuccessful=True
finally:
    TestHelper.shutdown(cluster, walletMgr, testSuccessful=testSuccessful, dumpErrorDetails=dumpErrorDetails)

exitCode = 0 if testSuccessful else 1
exit(exitCode)
