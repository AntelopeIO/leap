#!/usr/bin/env python3
import atexit, os, signal, shlex, shutil, time
import socket, threading
import zlib, json, glob
from pathlib import Path
from TestHarness import Node, TestHelper, Utils

###############################################################################################
# This test starts nodeos process which is configured with GELF logging endpoint as localhost, 
# receives data from the GELF loggging UDP PORT, and checks if the received log entries match
# those in stderr log file.
###########################################################################################


GELF_PORT = 24081 

# We need debug level to get more information about nodeos process
logging="""{
  "includes": [],
  "appenders": [{
      "name": "stderr",
      "type": "console",
      "args": {
        "stream": "std_error",
        "level_colors": [{
            "level": "debug",
            "color": "green"
          },{
            "level": "warn",
            "color": "brown"
          },{
            "level": "error",
            "color": "red"
          }
        ],
         "flush": true
      },
      "enabled": true
    },{
      "name": "net",
      "type": "gelf",
      "args": {
        "endpoint": "localhost:GELF_PORT",
        "host": "localhost",
        "_network": "testnet"
      },
      "enabled": true
    }
  ],
  "loggers": [{
      "name": "default",
      "level": "debug",
      "enabled": true,
      "additivity": false,
      "appenders": [
        "stderr",
        "net"
      ]
    }
  ]
}"""

logging = logging.replace("GELF_PORT", str(GELF_PORT))

nodeos_run_time_in_sec = 5

node_id = 1
received_logs = []
BUFFER_SIZE = 1024

def gelfServer(stop):
  s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
  s.settimeout(1)
  s.bind((TestHelper.LOCAL_HOST, GELF_PORT))
  while not stop():
    try:
        data, _ = s.recvfrom(BUFFER_SIZE)
        message = zlib.decompress(data, zlib.MAX_WBITS|32)
        entry = json.loads(message.decode())
        global num_received_logs, last_received_log
        received_logs.append(entry["short_message"])
    except socket.timeout:
        pass
  s.close()


data_dir = Path(Utils.getNodeDataDir(node_id))
config_dir = Path(Utils.getNodeConfigDir(node_id))
start_nodeos_cmd = shlex.split(f"{Utils.EosServerPath} -e -p eosio --data-dir={data_dir} --config-dir={config_dir}")
if os.path.exists(data_dir):
    shutil.rmtree(data_dir)
os.makedirs(data_dir)
if not os.path.exists(config_dir):
    os.makedirs(config_dir)
nodeos = Node(TestHelper.LOCAL_HOST, TestHelper.DEFAULT_PORT, node_id, config_dir, data_dir, start_nodeos_cmd, unstarted=True)

with open(config_dir / 'logging.json', 'w') as textFile:
    print(logging,file=textFile)

stop_threads = False
t1 = threading.Thread(target = gelfServer, args =(lambda : stop_threads, ))

try:
  @atexit.register
  def cleanup():
      nodeos.kill(signal.SIGINT)
      global stop_threads
      stop_threads = True
      t1.join()

  t1.start()

  nodeos.launchUnstarted()
  time.sleep(nodeos_run_time_in_sec)
finally:
   cleanup()

stderr_file = data_dir / 'stderr.txt'
with open(stderr_file[0], "r") as f:
    stderr_txt = f.read().rstrip()

assert len(received_logs) > 10, "Not enough gelf logs are received"
for received_log in received_logs:
  assert received_log in stderr_txt, "received GELF log entry does not match that of stderr"

if os.path.exists(Utils.DataPath):
    shutil.rmtree(Utils.DataPath)
