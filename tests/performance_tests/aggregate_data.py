#!/usr/bin/env python3

import json
import argparse

parser = argparse.ArgumentParser(add_help=False)
parser.add_argument("outfile", type=str, help="Name to write combined data to")
parser.add_argument("infiles", type=str, help="List of files to combine data from", nargs="*")
args = parser.parse_args()
js = {}
for file in args.infiles:
    file_js = {}
    with open(file, 'rt') as rf: 
        file_js = json.load(rf)
        if "env" not in js.keys():
            js["env"] = file_js["env"]
        file_js.pop("env")
        if "nodeosVersion" not in js.keys():
            js["nodeosVersion"] = file_js["nodeosVersion"]
        
        file_js.pop("nodeosVersion")
        js[file]=file_js

with open(args.outfile, 'wt') as wf:
    wf.write(json.dumps(js, sort_keys=True, indent=2))
