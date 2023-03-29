#!/usr/bin/env python3

import argparse
import datetime
from dataclasses import InitVar, dataclass, field, is_dataclass, asdict
from enum import Enum
import errno
import glob
import json
from pathlib import Path
import os
import math
import platform
import shlex
import shutil
import select
import signal
import string
import subprocess
import sys
import time
from typing import ClassVar, Dict, List

from TestHarness import Cluster
from TestHarness import Utils
from TestHarness import fc_log_level

block_dir = 'blocks'

class EnhancedEncoder(json.JSONEncoder):
    def default(self, o):
        if is_dataclass(o):
            return asdict(o)
        elif isinstance(o, Path):
            return str(o)
        elif isinstance(o, Enum):
            return str(o)
        return super().default(o)

@dataclass
class KeyStrings(object):
    pubkey: str
    privkey: str

@dataclass
class nodeDefinition:
    index: int
    name: str
    node_cfg_name: InitVar[str]
    base_dir: InitVar[str]
    cfg_name: InitVar[str]
    data_name: InitVar[str]
    keys: List[str] = field(default_factory=list)
    peers: List[str] = field(default_factory=list)
    producers: List[str] = field(default_factory=list)
    dont_start: bool = field(init=False, default=False)
    config_dir_name: Path = field(init=False)
    data_dir_name: str = field(init=False)
    p2p_port: int = 0
    http_port: int = 0
    base_p2p_port: ClassVar[int] = 9876
    base_http_port: ClassVar[int] = 8888
    host_name: str = 'localhost'
    public_name: str = 'localhost'
    listen_addr: str = '0.0.0.0'
    p2p_count: ClassVar[int] = 0
    http_count: ClassVar[int] = 0
    p2p_port_generator = None
    http_port_generator = None
    _dot_label: str = ''

    def __post_init__(self, node_cfg_name, base_dir, cfg_name, data_name):
        self.config_dir_name = Path(base_dir) / cfg_name / node_cfg_name
        self.data_dir_name = Path(base_dir) / data_name / node_cfg_name
        if self.p2p_port_generator is None:
            type(self).p2p_port_generator = self.create_p2p_port_generator()
        if self.http_port_generator is None:
            type(self).http_port_generator = self.create_http_port_generator()
    
    def set_host(self, is_bios=False):
        self.p2p_port = self.p2p_bios_port() if is_bios else next(self.p2p_port_generator)
        self.http_port = self.http_bios_port() if is_bios else next(self.http_port_generator)

    @classmethod
    def p2p_bios_port(cls):
        return cls.base_p2p_port - 100

    @classmethod
    def http_bios_port(cls):
        return cls.base_http_port - 100

    @classmethod
    def create_p2p_port_generator(cls):
        while True:
            yield cls.base_p2p_port + cls.p2p_count
            cls.p2p_count += 1

    @classmethod
    def create_http_port_generator(cls):
        while True:
            yield cls.base_http_port + cls.http_count
            cls.http_count += 1

    @property
    def p2p_endpoint(self):
        return self.public_name + ':' + str(self.p2p_port)

    @property
    def dot_label(self):
        if not self._dot_label:
            self._dot_label = self.mk_dot_label()
        return self._dot_label
    
    def mk_dot_label(self):
        node_name = self.name + '\nprod='
        if len(self.producers) > 0:
            node_name += '\n'.join(self.producers)
        else:
            node_name += '<none>'
        return self.mk_host_dot_label() + '\n' + node_name

    def mk_host_dot_label(self):
        if not self.public_name:
            host_label = self.p2p_endpoint
        elif self.public_name == self.host_name:
            host_label = self.p2p_endpoint
        else:
            host_label = self.public_name + '/' + self.host_name
        return host_label

@dataclass
class testnetDefinition:
    name: str
    nodes: Dict[str, nodeDefinition] = field(init=False, default_factory=dict)

def producer_name(producer_number: int, shared_producer: bool = False):
    '''For first 26 return "defproducera" ... "defproducerz".
       After 26 return "defpraaaaaab", "defpraaaaaac"...'''
    if producer_number > len(string.ascii_lowercase) - 1:
        def alpha_str_base(number: int, base: str):
            '''Convert number to base represented as string of "digits"'''
            d,m = divmod(number, len(base))
            if d > 0:
                return alpha_str_base(d, base) + base[m]
            return base[m]
        return ('shr' if shared_producer else 'def') + 'pr' + alpha_str_base(producer_number - len(string.ascii_lowercase) + 1, string.ascii_lowercase).rjust(7, string.ascii_lowercase[0])
    else:
        return ('shr' if shared_producer else 'def') + 'producer' + string.ascii_lowercase[producer_number]

class launcher(object):
    def __init__(self, args):
        self.args = self.parseArgs(args)
        self.network = testnetDefinition(self.args.network_name)
        self.aliases: List[str] = []
        self.next_node = 0

        self.launch_time = datetime.datetime.now().strftime('%Y_%m_%d_%H_%M_%S')

        self.define_network()
        self.generate()

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
        parser.add_argument('-o', '--output', help='save a copy of the generated topology in this file and exit without launching', dest='topology_filename')
        parser.add_argument('-k', '--kill', type=int, help='kill a network as specified in arguments with given signal')
        parser.add_argument('--down', type=comma_separated, help='comma-separated list of node numbers that will be shut down', default=[])
        parser.add_argument('--bounce', type=comma_separated, help='comma-separated list of node numbers that will be restarted', default=[])
        parser.add_argument('--roll', type=comma_separated, help='comma-separated list of host names where the nodes will be rolled to a new version')
        parser.add_argument('-b', '--base_dir', type=Path, help='base directory where configuration and data files will be written', default=Path('.'))
        parser.add_argument('--config-dir', type=Path, help='directory containing configuration files such as config.ini', default=Path('etc') / 'eosio')
        parser.add_argument('--data-dir', type=Path, help='name of subdirectory under base-dir where node data will be written', default=Path('var') / 'lib')
        parser.add_argument('-c', '--config', type=Path, help='configuration file name relative to config-dir', default='config.ini')
        parser.add_argument('-v', '--version', action='version', version='%(prog)s 1.0')
        
        cfg = parser.add_argument_group(title='optional and config file arguments')
        cfg.add_argument('-f', '--force', action='store_true', help='force overwrite of existing configuration files and erase blockchain', default=False)
        cfg.add_argument('-n', '--nodes', dest='total_nodes', type=int, help='total number of nodes to configure and launch', default=1)
        cfg.add_argument('--unstarted-nodes', type=int, help='total number of nodes to configure, but not launch', default=0)
        cfg.add_argument('-p', '--pnodes', type=int, help='number of nodes that contain one or more producers', default=1)
        cfg.add_argument('--producers', type=int, help='total number of non-bios and non-shared producer instances in this network', default=21)
        cfg.add_argument('--shared-producers', type=int, help='total number of shared producers on each non-bios node', default=0)
        cfg.add_argument('-m', '--mode', choices=['any', 'producers', 'specified', 'none'], help='connection mode', default='any')
        cfg.add_argument('-s', '--shape', help='network topology, use "star", "mesh", "ring", "line" or give a filename for custom', default='star')
        cfg.add_argument('-g', '--genesis', type=Path, help='set the path to genesis.json', default='./genesis.json')
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
        cfg.add_argument('--template', help='the startup script template', default='testnet.template')
        cfg.add_argument('--max-block-cpu-usage', type=int, help='the "max-block-cpu-usage" value to use in the genesis.json file', default=200000)
        cfg.add_argument('--max-transaction-cpu-usage', type=int, help='the "max-transaction-cpu-usage" value to use in the genesis.json file', default=150000)
        cfg.add_argument('--nodeos-log-path', type=Path, help='path to nodeos log directory')
        cfg.add_argument('--logging-level', type=fc_log_level, help='Provide the "level" value to use in the logging.json file')
        cfg.add_argument('--logging-level-map', type=json.loads, help='JSON string of a logging level dictionary to use in the logging.json file for specific nodes, matching based on node number. Ex: {"bios":"off","00":"info"}')
        cfg.add_argument('--is-nodeos-v2', action='store_true', help='Toggles old nodeos compatibility', default=False)
        r = parser.parse_args(args)
        if r.launch != 'none' and r.topology_filename:
            Utils.Print('Output file specified--overriding launch to "none"')
            r.launch = 'none'
        if r.shape not in ['star', 'mesh', 'ring', 'line'] and not Path(r.shape).is_file():
            parser.error('-s, --shape must be one of "star", "mesh", "ring", "line", or a file')
        if len(r.specific_nums) != len(getattr(r, f'specific_{Utils.EosServerName}es')):
            parser.error(f'Count of uses of --specific-num and --specific-{Utils.EosServerName} must match')
        if len(r.spcfc_inst_nums) != len(getattr(r, f'spcfc_inst_{Utils.EosServerName}es')):
            parser.error(f'Count of uses of --spcfc-inst-num and --spcfc-inst-{Utils.EosServerName} must match')
        r.pnodes += 1 # add one for the bios node
        r.total_nodes += 1
        if r.pnodes > r.producers + 1:
            r.pnodes = r.producers
        if r.pnodes > r.total_nodes:
            r.total_nodes = r.pnodes + r.unstarted_nodes
        elif r.total_nodes < r.pnodes + r.unstarted_nodes:
            Utils.errorExit('if provided, "--nodes" must be equal to or greater than the number of nodes indicated by "--pnodes" and "--unstarted-nodes".')
        return r

    def assign_name(self, is_bios):
        if is_bios:
            return -1, 'bios', 'node_bios'
        else:
            index = self.next_node
            indexStr = str(self.next_node)
            self.next_node += 1
            return index, self.network.name + indexStr.zfill(2), f'node_{indexStr.zfill(2)}'

    def define_network(self):
        if self.args.per_host == 0:
            for i in range(self.args.total_nodes):
                index, node_name, cfg_name = self.assign_name(i == 0)
                node = nodeDefinition(index, node_name, cfg_name, self.args.base_dir, self.args.config_dir, self.args.nodeos_log_path)
                node.set_host(i == 0)
                self.aliases.append(node.name)
                self.network.nodes[node.name] = node
        else:
            ph_count = 0
            host_ndx = 0
            num_prod_addr = 1
            num_nonprod_addr = 1
            for i in range(self.args.total_nodes, 0, -1):
                do_bios = False
                index, node_name, cfg_name = self.assign_name(i == 0)
                lhost = nodeDefinition(index, node_name, cfg_name, self.args.base_dir, self.args.config_dir, self.args.nodeos_log_path)
                lhost.set_host(i == 0)
                if ph_count == 0:
                    if host_ndx < num_prod_addr:
                        do_bios = True # servers.producer[host_ndx].has_bios
                        lhost.host_name = '127.0.0.1' # servers.producer[host_ndx].ipaddr
                        lhost.public_name = 'localhost' # servers.producer[host_ndx].name
                        ph_count = 1 # servers.producer[host_ndx].instances
                    elif host_ndx - num_prod_addr < num_nonprod_addr:
                        ondx = host_ndx - num_prod_addr
                        do_bios = True # servers.nonprod[ondx].has_bios
                        lhost.host_name = '127.0.0.1' # servers.nonprod[ondx].ipaddr
                        lhost.public_name = 'localhost' # servers.nonprod[ondx].name
                        ph_count = 1 # servers.nonprod[ondx].instances
                    else:
                        lhost.host_name = f'pseudo_{host_ndx.zfil(2)}'
                        lhost.public_name = lhost.host_name
                        ph_count = 1
                    host_ndx += 1
                lhost.name = self.assign_name(do_bios)
                self.aliases.append(lhost.name)
                do_bios = False
                ph_count -= 1

    def bind_nodes(self):
        if self.args.pnodes < 2:
            raise RuntimeError(f'Unable to allocate producers due to insufficient pnodes = {self.args.pnodes}')
        non_bios = self.args.pnodes - 1
        per_node = int(self.args.producers / non_bios)
        extra = self.args.producers % non_bios
        i = 0
        producer_number = 0
        to_not_start_node = self.args.total_nodes - self.args.unstarted_nodes - 1
        accounts = Cluster.createAccountKeys(len(self.network.nodes.values()))
        for account, node in zip(accounts, self.network.nodes.values()):
            is_bios = node.name == 'bios'
            if is_bios:
                node.keys.append(KeyStrings('EOS6MRyAjQq8ud7hVNYcfnVPJqcVpscN5So8BhtHuGYqET5GDW5CV',
                                            '5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3'))
                node.producers.append('eosio')
            else:
                node.keys.append(KeyStrings(account.ownerPublicKey, account.ownerPrivateKey))
                if i < non_bios:
                    count = per_node
                    if extra:
                        count += 1
                        extra -= 1
                    while count > 0:
                        prodname = producer_name(producer_number)
                        node.producers.append(prodname)
                        producer_number += 1
                        count -= 1
                    for j in range(0, self.args.shared_producers):
                        prodname = producer_name(j, True)
                        node.producers.append(prodname)
                node.dont_start = i >= to_not_start_node
            if not is_bios:
                i += 1


    def generate(self):
        {
            'ring': self.make_line,
            'line': lambda: self.make_line(False),
            'star': self.make_star,
            'mesh': self.make_mesh,
        }.get(self.args.shape, self.make_custom)()

        if not self.args.nogen:
            genesis = self.init_genesis()
            for node_name, node in self.network.nodes.items():
                node.config_dir_name.mkdir(parents=True, exist_ok=True)
                self.write_config_file(node)
                self.write_logging_config_file(node)
                self.write_genesis_file(node, genesis)
                node.data_dir_name.mkdir(parents=True, exist_ok=True)
        
        self.write_dot_file()

        if self.args.topology_filename:
            with open(self.args.topology_filename, 'w') as topo:
                json.dump(self.network, topo, cls=EnhancedEncoder, indent=2, separators=[', ', ': '])

    def write_config_file(self, node):
        with open(node.config_dir_name / 'config.ini', 'w') as cfg:
            is_bios = node.name == 'bios'
            peers = '\n'.join([f'p2p-peer-address = {self.network.nodes[p].p2p_endpoint}' for p in node.peers])
            if len(node.producers) > 0:
                producer_keys = f'signature-provider = {node.keys[0].pubkey}=KEY:{node.keys[0].privkey}\n'
                producer_names = '\n'.join([f'producer-name = {p}' for p in node.producers])
                producer_plugin = 'plugin = eosio::producer_plugin\n'
            else:
                producer_keys = ''
                producer_names = ''
                producer_plugin = ''
            config = \
f'''blocks-dir = {block_dir}
http-server-address = {node.host_name}:{node.http_port}
http-validate-host = false
p2p-listen-endpoint = {node.listen_addr}:{node.p2p_port}
p2p-server-address = {node.p2p_endpoint}
{"enable-stale-production = true" if is_bios else ""}
{"p2p-peer-address = %s" % self.network.nodes['bios'].p2p_endpoint if not is_bios else ""}
{peers}
{producer_keys}
{producer_names}
{producer_plugin}
plugin = eosio::net_plugin
plugin = eosio::chain_api_plugin
'''
            cfg.write(config)

    def write_logging_config_file(self, node):
        ll = fc_log_level.debug
        if self.args.logging_level:
            ll = self.args.logging_level
        dex = str(node.index).zfill(2)
        if dex in self.args.logging_level_map:
            ll = self.args.logging_level_map[dex]
        with open(Path(__file__).resolve().parents[0] / 'TestHarness' / 'logging-template.json', 'r') as default:
            cfg = json.load(default)
        for logger in cfg['loggers']:
            logger['level'] = ll
        with open(node.config_dir_name / 'logging.json', 'w') as out:
            json.dump(cfg, out, cls=EnhancedEncoder, indent=2)

    def init_genesis(self):
        genesis_path = self.args.genesis if self.args.genesis.is_absolute() else Path.cwd() / self.args.genesis
        genesis = None
        if not genesis_path.is_file():
            genesis = { 'initial_timestamp': datetime.datetime.now().isoformat(),
                        'initial_key': self.network.nodes['bios'].keys[0].pubkey,
                        'initial_configuration': {
                            'max_block_net_usage': 1048576,
                            'target_block_net_usage_pct': 1000,
                            'max_transaction_net_usage': 524288,
                            'base_per_transaction_net_usage': 12,
                            'net_usage_leeway': 500,
                            'context_free_discount_net_usage_num': 20,
                            'context_free_discount_net_usage_den': 100,
                            'max_block_cpu_usage': self.args.max_block_cpu_usage,
                            'target_block_cpu_usage_pct': 1000,
                            'max_transaction_cpu_usage': self.args.max_transaction_cpu_usage,
                            'min_transaction_cpu_usage': 100,
                            'max_transaction_lifetime': 3600,
                            'deferred_trx_expiration_window': 600,
                            'max_transaction_delay': 3888000,
                            'max_inline_action_size': 524288,
                            'max_inline_action_depth': 4,
                            'max_authority_depth': 6
                        }
                    }
        else:
            with open(genesis_path, 'r') as f:
                genesis = json.load(f)
        genesis['initial_key'] = self.network.nodes['bios'].keys[0].pubkey
        genesis['max_block_cpu_usage'] = self.args.max_block_cpu_usage
        genesis['max_transaction_cpu_usage'] = self.args.max_transaction_cpu_usage
        return genesis

    def write_genesis_file(self, node, genesis):
        with open(node.config_dir_name / 'genesis.json', 'w') as f:
            f.write(json.dumps(genesis, indent=2))

    def is_bios_ndx(self, ndx):
        return self.aliases[ndx] == 'bios'

    def start_ndx(self):
        return 1 if self.is_bios_ndx(0) else 0

    def next_ndx(self, ndx):
        ndx += 1
        loop = ndx == self.args.total_nodes
        if loop:
            ndx = self.start_ndx()
        else:
            if self.is_bios_ndx(ndx):
                ndx, loop = self.next_ndx(ndx)
        return ndx, loop

    def skip_ndx(self, begin, offset):
        ndx = (begin + offset) % self.args.total_nodes
        if self.args.total_nodes > 2:
            attempts = self.args.total_nodes - 1
            while attempts and (self.is_bios_ndx(ndx) or ndx == begin):
                ndx, _ = self.next_ndx(ndx)
                attempts -= 1
        return ndx

    def make_line(self, make_ring: bool = True):
        if Utils.Debug: Utils.Print(f"making {'ring' if make_ring else 'line'}")
        self.bind_nodes()
        nl = list(self.network.nodes.values())
        for node, nextNode in zip(nl, nl[1:]):
            node.peers.append(nextNode.name)
        if make_ring:
            nl[-1].peers.append(nl[1].name)

    def make_star(self):
        if Utils.Debug: Utils.Print('making star')
        non_bios = self.args.total_nodes - 1
        if non_bios < 4:
            self.make_line()
            return
        self.bind_nodes()

        links = 3
        if non_bios > 12:
            links = int(math.sqrt(non_bios)) + 2
        gap = 3 if non_bios > 6 else (non_bios - links)/2 + 1
        while non_bios % gap == 0:
            gap += 1

        loop = False
        i = self.start_ndx()
        while not loop:
            current = self.network.nodes[self.aliases[i]]
            ndx = i
            for l in range(1, links+1):
                ndx = self.skip_ndx(ndx, l * gap)
                peer = self.aliases[ndx]
                found = True
                while found:
                    found = False
                    for p in current.peers:
                        if p == peer:
                            ndx, _ = self.next_ndx(ndx)
                            if ndx == i:
                                ndx, _ = self.next_ndx(ndx)
                            peer = self.aliases[ndx]
                            found = True
                            break
                if peer != current.name:
                    current.peers.append(peer)
            i, loop = self.next_ndx(i)

    def make_mesh(self):
        if Utils.Debug: Utils.Print('making mesh')
        non_bios = self.args.total_nodes - 1
        self.bind_nodes()
        loop = False
        i = self.start_ndx()
        while not loop:
            current = self.network.nodes[self.aliases[i]]
            for j in range(1, non_bios):
                ndx = self.skip_ndx(i,j)
                peer = self.aliases[ndx]
                if peer not in current.peers:
                    current.peers.append(peer)
            i, loop = self.next_ndx(i)

    def make_custom(self):
        if Utils.Debug: Utils.Print('making custom')
        with open(self.args.shape, 'r') as source:
            topo = json.load(source)
            self.network.name = topo['name']
            for nodeName in topo['nodes']:
                node = topo['nodes'][nodeName]
                self.network.nodes[nodeName].dont_start = node['dont_start']
                for keyObj in node['keys']:
                    self.network.nodes[nodeName].keys.append(KeyStrings(keyObj['pubkey'], keyObj['privkey']))
                for peer in node['peers']:
                    self.network.nodes[nodeName].peers.append(peer)
                for producer in node['producers']:
                    self.network.nodes[nodeName].producers.append(producer)

    def launch(self, instance: nodeDefinition):
        dd = Path(instance.data_dir_name)
        out = dd / 'stdout.txt'
        err_sl = dd / 'stderr.txt'
        err = dd / Path(f'stderr.{self.launch_time}.txt')
        pidf = dd / Path(f'{Utils.EosServerName}.pid')

        if instance.index in self.args.spcfc_inst_nums:
            eosdcmd = [f"{getattr(self.args, f'spcfc_inst_{Utils.EosServerName}es')[self.args.spcfc_inst_nums.index(instance.index)]}"]
        else:
            eosdcmd = [Utils.EosServerPath]
        if self.args.skip_signature:
            eosdcmd.append('--skip-transaction-signatures')
        if getattr(self.args, Utils.EosServerName):
            eosdcmd.extend(shlex.split(getattr(self.args, Utils.EosServerName)))
        if instance.index in self.args.specific_nums:
            i = self.args.specific_nums.index(instance.index)
            specifics = getattr(self.args, f'specific_{Utils.EosServerName}es')[i]
            if specifics[0] == "'" and specifics[-1] == "'":
                eosdcmd.extend(shlex.split(specifics[1:-1]))
            else:
                eosdcmd.extend(shlex.split(specifics))
        eosdcmd.append('--config-dir')
        eosdcmd.append(str(instance.config_dir_name))
        eosdcmd.append('--data-dir')
        eosdcmd.append(str(instance.data_dir_name))
        eosdcmd.append('--genesis-json')
        eosdcmd.append(f'{instance.config_dir_name}/genesis.json')
        if self.args.timestamp:
            eosdcmd.append('--genesis-timestamp')
            eosdcmd.append(self.args.timestamp)

        # Always enable a history query plugin on the bios node
        if instance.name == 'bios':
            if self.args.is_nodeos_v2:
                eosdcmd.append('--plugin')
                eosdcmd.append('eosio::history_api_plugin')
                eosdcmd.append('--filter-on')
                eosdcmd.append('"*"')
            else:
                eosdcmd.append('--plugin')
                eosdcmd.append('eosio::trace_api_plugin')

        if 'eosio::history_api_plugin' in eosdcmd and 'eosio::trace_api_plugin' in eosdcmd:
            eosdcmd.remove('--trace-no-abis')
            eosdcmd.remove('--trace-rpc-abi')
            i = eosdcmd.index('eosio::trace_api_plugin')
            eosdcmd.pop(i)
            i -= 1
            eosdcmd.pop(i)

        if not instance.dont_start:
            Utils.Print(f'spawning child: {" ".join(eosdcmd)}')

            dd.mkdir(parents=True, exist_ok=True)

            stdout = open(out, 'w')
            stderr = open(err, 'w')
            c = subprocess.Popen(eosdcmd, stdout=stdout, stderr=stderr)
            with pidf.open('w') as pidout:
                pidout.write(str(c.pid))
            try:
                err_sl.unlink()
            except FileNotFoundError:
                pass
            err_sl.symlink_to(err.name)
        else:
            Utils.Print(f'unstarted node command: {" ".join(eosdcmd)}')

            with open(instance.data_dir_name / 'start.cmd', 'w') as f:
                f.write(' '.join(eosdcmd))

    def bounce(self, nodeNumbers):
        self.down(nodeNumbers, True)

    def down(self, nodeNumbers, relaunch=False):
        for num in nodeNumbers:
            for node in self.network.nodes.values():
                if self.network.name + num == node.name:
                    Utils.Print(f'{"Restarting" if relaunch else "Shutting down"} node {node.name}')
                    with open(node.data_dir_name / f'{Utils.EosServerName}.pid', 'r') as f:
                        pid = int(f.readline())
                        self.terminate_wait_pid(pid, raise_if_missing=not relaunch)
                    if relaunch:
                        self.launch(node)

    def kill(self, signum):
        errorCode = 0
        for node in self.network.nodes.values():
            try:
                with open(node.data_dir_name / f'{Utils.EosServerName}.pid', 'r') as f:
                    pid = int(f.readline())
                    self.terminate_wait_pid(pid, signum, raise_if_missing=False)
            except FileNotFoundError as err:
                errorCode = 1
        return errorCode

    def start_all(self):
        if self.args.launch.lower() != 'none':
            for instance in self.network.nodes.values():
                self.launch(instance)
                time.sleep(self.args.delay)

    def write_dot_file(self):
        with open('testnet.dot', 'w') as f:
            f.write('digraph G\n{\nlayout="circo";\n')
            for node in self.network.nodes.values():
                for p in node.peers:
                    pname = self.network.nodes[p].dot_label
                    f.write(f'"{node.dot_label}"->"{pname}" [dir="forward"];\n')
            f.write('}')

    def terminate_wait_pid(self, pid, signum = signal.SIGTERM, raise_if_missing=True):
        '''Terminate a non-child process with given signal number or with SIGTERM if not
           provided and wait for it to exit.'''
        if sys.version_info >= (3, 9) and platform.system() == 'Linux': # on our supported platforms, Python 3.9 accompanies a kernel > 5.3
            try:
                fd = os.pidfd_open(pid)
            except:
                if raise_if_missing:
                    raise
            else:
                po = select.poll()
                po.register(fd, select.POLLIN)
                try:
                    os.kill(pid, signum)
                except ProcessLookupError:
                    if raise_if_missing:
                        raise
                po.poll(None)
        else:
            if platform.system() in {'Linux', 'Darwin'}:
                def pid_exists(pid):
                    try:
                        os.kill(pid, 0)
                    except OSError as err:
                        if err.errno == errno.ESRCH:
                            return False
                        elif err.errno == errno.EPERM:
                            return True
                        else:
                            raise err
                    return True
                def backoff_timer(delay):
                    time.sleep(delay)
                    return min(delay * 2, 0.04)
                delay = 0.0001
                try:
                    os.kill(pid, signum)
                except ProcessLookupError:
                    if raise_if_missing:
                        raise
                    else:
                        return
                while True:
                    if pid_exists(pid):
                        delay = backoff_timer(delay)
                    else:
                        return

if __name__ == '__main__':
    errorCode = 0
    l = launcher(sys.argv[1:])
    if len(l.args.down):
        l.down(l.args.down)
    elif len(l.args.bounce):
        l.bounce(l.args.bounce)
    elif l.args.kill:
        errorCode = l.kill(l.args.kill)
    elif l.args.launch == 'all' or l.args.launch == 'local':
        l.start_all()
    for f in glob.glob(Utils.DataPath):
        shutil.rmtree(f)
    sys.exit(errorCode)
