#!/usr/bin/env python3
import os
import subprocess
import sys

class CompileError(Exception):
    pass

def main(root, in_file, out_file):
    cwd = os.getcwd()
    os.chdir(root)

    intermediate_file = 'intermediate.wasm'
    res = subprocess.run(
        ['cdt-cpp', '-O0', '-c', in_file, '-o', intermediate_file],
        capture_output=True
    )
    if res.returncode > 0:
        print(res.args)
        raise CompileError(res.stderr)

    res = subprocess.run(
        ['cdt-ld', intermediate_file, '-o', out_file],
        capture_output=True
    )
    if res.returncode > 0:
        print(res.args)
        raise CompileError(res.stderr)

    os.chdir(cwd)

if __name__ == "__main__":
    root_path = sys.argv[1]
    in_file_path = sys.argv[2] if sys.argv[2] else ""
    out_file_path = sys.argv[3] if sys.argv[3] else ""
    main(root_path, in_file_path, out_file_path)
