#!/usr/bin/env python3

import argparse
import os
import pathlib
import sys

from testUtils import Utils

class launcher(object):
    def parseArgs(self, args):
        '''Configure argument parser and use it on the passed in list of strings.
        
        arguments:
        args -- list of arguments (may be sys.argv[1:] or synthetic list)
        
        returns -- argparse.Namespace object with parsed results
        '''
        def comma_separated(string):
            return string.split(',')
        parser = argparse.ArgumentParser(description='launcher command line options',
                                         formatter_class=argparse.ArgumentDefaultsHelpFormatter)
        parser.add_argument('-i', '--timestamp', help='set the timestamp for the first block.  Use "now" to indicate the current time')
        parser.add_argument('-l', '--launch', choices=['all', 'none', 'local'], help='select a subset of nodes to launch.  If not set, the default is to launch all unless an output file is named, in which case none are started.', default='all')
        parser.add_argument('-o', '--output', help='save a copy of the generated topology in this file', dest='topology_filename')
        parser.add_argument('-k', '--kill', help='retrieve the list of previously started process ids and issue a kill to each')
        parser.add_argument('--down', type=comma_separated, help='comma-separated list of node numbers that will be shut down')
        parser.add_argument('--roll', type=comma_separated, help='comma-separated list of host names where the nodes will be rolled to a new version')
        parser.add_argument('--config-dir', help='directory containing configuration files such as config.ini', default=os.getcwd())
        parser.add_argument('-c', '--config', type=pathlib.Path, help='configuration file name relative to config-dir', default='config.ini')
        parser.add_argument('-v', '--version', action='version', version='%(prog)s 1.0')
        
        cfg = parser.add_argument_group(title='optional and config file arguments')
        cfg.add_argument('-f', '--force', action='store_true', help='force overwrite of existing configuration files and erase blockchain', default=False)
        cfg.add_argument('-n', '--nodes', type=int, help='total number of nodes to configure and launch', default=1)
        cfg.add_argument('--unstarted-nodes', type=int, help='total number of nodes to configure, but not launch', default=0)
        cfg.add_argument('-p', '--pnodes', type=int, help='number of nodes that contain one or more producers', default=1)
        cfg.add_argument('--producers', type=int, help='total number of non-bios and non-shared producer instances in this network', default=21)
        cfg.add_argument('--shared-producers', type=int, help='total number of shared producers on each non-bios node', default=0)
        cfg.add_argument('-m', '--mode', choices=['any', 'producers', 'specified', 'none'], help='connection mode', default='any')
        cfg.add_argument('-s', '--shape', help='network topology, use "star", "mesh", "ring", "line" or give a filename for custom', default='star')
        cfg.add_argument('-g', '--genesis', help='set the path to genesis.json', default='./genesis.json')
        cfg.add_argument('--skip-signature', action='store_true', help='do not require transaction signatures', default=False)
        cfg.add_argument('--' + Utils.EosServerName, help=f'forward {Utils.EosServerName} command line argument(s) to each instance of {Utils.EosServerName}; enclose all arg(s) in quotes')
        cfg.add_argument('--specific-num', type=int, action='append', dest='specific_nums', default=[], help=f'forward {Utils.EosServerName} command line argument(s) (using "--specific-{Utils.EosServerName}" flag to this specific instance of {Utils.EosServerName}.  This parameter can be entered multiple times and requires a paired "--specific-{Utils.EosServerName}" flag each time it is used.')
        cfg.add_argument('--specific-' + Utils.EosServerName, action='append', dest=f'specific_{Utils.EosServerName}es', default=[], help=f'forward {Utils.EosServerName} command line argument(s) to its paired specific instance of {Utils.EosServerName} (using "--specific-num"); enclose arg(s) in quotes')
        cfg.add_argument('--spcfc-inst-num', type=int, action='append', dest='spcfc_inst_nums', default=[], help=f'specify a path to a binary (using "--spcfc-inst-{Utils.EosServerName}" flag) for launching this numbered instance of {Utils.EosServerName}.  This parameter can be entered multiple times and requires a paired "--spcfc-inst-{Utils.EosServerName}" flag each time it is used.')
        cfg.add_argument('--spcfc-inst-' + Utils.EosServerName, action='append', dest=f'spcfc_inst_{Utils.EosServerName}es', default=[], help=f'path to a binary to launch for the {Utils.EosServerName} instance number specified by the corresponding "--spcfc-inst-num" flag')
        cfg.add_argument('-d', '--delay', type=int, help='seconds delay before starting each node after the first', default=0)
        cfg.add_argument('--boot', action='store_true', help='after deploying the nodes, boot the network', default=False)
        cfg.add_argument('--nogen', action='store_true', help='launch nodes without writing new config files', default=False)
        cfg.add_argument('--host-map', help='name of file containing mapping specific nodes to hosts, used to enhance the custom shape argument')
        cfg.add_argument('--servers', help='name of a file containing ip addresses and names of individual servers to deploy as producers or non-producers')
        cfg.add_argument('--per-host', type=int, help='specifies how many instances will run on a single host.  Use 0 to indicate all on one', default=0)
        cfg.add_argument('--network-name', help='network name prefix used in GELF logging source', default='testnet_')
        cfg.add_argument('--enable-gelf-logging', action='store_true', help='enable gelf logging appender in logging configuration file', default=False)
        cfg.add_argument('--gelf-endpoint', help='hostname:port or ip:port of GELF endpoint', default='128.0.0.1:12201')
        cfg.add_argument('--template', help='the startup script template')
        cfg.add_argument('--script', help='the optionally generated startup script name', default='bios_boot.sh')
        cfg.add_argument('--max-block-cpu-usage', type=int, help='the "max-block-cpu-usage" value to use in the genesis.json file')
        cfg.add_argument('--max-transaction-cpu-usage', type=int, help='the "max-transaction-cpu-usage" value to use in the genesis.json file')
        r = parser.parse_args(args)
        if r.shape not in ['star', 'mesh', 'ring', 'line'] and not pathlib.Path(r.shape).is_file():
            parser.error('-s, --shape must be one of "star", "mesh", "ring", "line", or a file')
        if len(r.specific_nums) != len(getattr(r, f'specific_{Utils.EosServerName}es')):
            parser.error(f'Count of uses of --specific-num and --specific-{Utils.EosServerName} must match')
        if len(r.spcfc_inst_nums) != len(getattr(r, f'spcfc_inst_{Utils.EosServerName}es')):
            parser.error(f'Count of uses of --spcfc-inst-num and --spcfc-inst-{Utils.EosServerName} must match')
        return r

if __name__ == '__main__':
    l = launcher()
    args = l.parseArgs(sys.argv[1:])
    print(args)
