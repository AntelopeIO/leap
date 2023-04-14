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
from TestHarness import testnetDefinition, nodeDefinition
from TestHarness.launcher import cluster_generator

class launcher(cluster_generator):
    def __init__(self, args):
        super().__init__(args)
        self.define_network()
        self.generate()

    def launch(self, instance: nodeDefinition):
        eosdcmd = self.construct_command_line(instance)

        if not instance.dont_start:
            dd = Path(instance.data_dir_name)
            out = dd / 'stdout.txt'
            err_sl = dd / 'stderr.txt'
            err = dd / Path(f'stderr.{self.launch_time}.txt')
            pidf = dd / Path(f'{Utils.EosServerName}.pid')

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

    def kill(self, signum: int):
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
        errorCode = l.kill(int(l.args.kill))
    elif l.args.launch == 'all' or l.args.launch == 'local':
        l.start_all()
    for f in glob.glob(Utils.DataPath):
        shutil.rmtree(f)
    sys.exit(errorCode)
