#!/usr/bin/env python3

from testUtils import Utils
from TestHelper import TestHelper

import subprocess
import signal
import time
import os
import shutil

###############################################################
# block-log-retain-blocks test
#
# A basic test for --block-log-retain-blocks option. It validates no blocks.log
# is generated when the option is set to 0 and blocks.log is generated when the
# option is set to greater than 0.
#
###############################################################

Print=Utils.Print
errorExit=Utils.errorExit

args = TestHelper.parse_args({"--keep-logs", "--dump-error-details", "-v", "--leave-running", "--clean-run"})
debug=args.v
keepLogs=args.keep_logs
dumpErrorDetails=args.dump_error_details
killAll=args.clean_run
killEosInstances= not args.leave_running

Utils.Debug=debug

def is_in_file(string, filename):
   with open(filename) as myfile:
      if string in myfile.read():
         return True
      else:
         return False

def start_and_stop_nodeos(retain_blocks=0, produced_block=10):
   cmd=f'programs/nodeos/nodeos -e -p eosio --plugin eosio::producer_api_plugin --plugin eosio::producer_plugin --plugin eosio::chain_api_plugin --plugin eosio::chain_plugin --data-dir {data_dir} --block-log-retain-blocks {retain_blocks}'
   Print(f'cmd to launch nodeos: {cmd}')

   stdout_filename=data_dir + "stdout.out"
   stderr_filename=data_dir + "stderr.out"
   stdout_file=open(stdout_filename, "w")
   stderr_file=open(stderr_filename, "w")
   proc=subprocess.Popen(cmd.split(), stdout=stdout_file, stderr=stderr_file)

   for i in range(1,20):
      if is_in_file(f'lib: {produced_block}', stderr_filename):
         Print(f'nodeos launched and lib: {produced_block} produced')
         break;
      elif i == 19:
         os.system('killall nodeos')
         errorExit("nodeos failed to start after 20 seconds")
      time.sleep(1)
   
   proc.send_signal(signal.SIGINT)
   proc.wait()
   stdout_file.close()
   stderr_file.close()
   Print(f'nodeos stopped')

def expect_no_block_log_file():
   if os.path.exists(blocklog_file):
      errorExit(f'{blocklog_file} not expected to exist. Test failed')

def expect_block_log_file():
   if not os.path.exists(blocklog_file):
      errorExit(f'{blocklog_file} expected to exist. Test failed')

TestHelper.printSystemInfo("BEGIN")

if killAll:
   os.system('killall nodeos')
data_dir="var/lib/node/" # OK to be a global
blocklog_file=data_dir + "blocks/blocks.log"
shutil.rmtree(data_dir, ignore_errors=True)
os.makedirs(data_dir, exist_ok=True)

Print("Start nodeos with retain_blocks as 0 the first time")
start_and_stop_nodeos(retain_blocks=0, produced_block=10)
expect_no_block_log_file()

Print("Start nodeos with retain_blocks as 0 the second time")
start_and_stop_nodeos(retain_blocks=0, produced_block=20)
expect_no_block_log_file()

shutil.rmtree(data_dir, ignore_errors=True)
os.makedirs(data_dir, exist_ok=True)

Print("Start nodeos with retain_blocks as 10")
start_and_stop_nodeos(retain_blocks=5, produced_block=10)
expect_block_log_file()

Print("Test succeeded")
if not keepLogs:
   shutil.rmtree(data_dir, ignore_errors=True)

exit(0)
