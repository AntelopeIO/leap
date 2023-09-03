#!/usr/bin/env python3

import os
import re

from TestHarness import Utils

###############################################################
# leap_util_bls_test
#
#  Test leap-util's BLS commands.
#  - Create a key pair
#  - Create a POP (Proof of Possession)
#  - Error handlings
#
###############################################################

Print=Utils.Print
testSuccessful=False

def test_create_key_to_console():
    rslts = Utils.processLeapUtilCmd("bls create key --to-console", "create key to console", silentErrors=False)
    check_create_key_results(rslts)

def test_create_key_to_file():
    # a random tmp file, to be deleted after use
    tmp_file = "tmp_key_file_dlkdx1x56pjy"
    Utils.processLeapUtilCmd("bls create key --file {}".format(tmp_file), "create key to file", silentErrors=False)

    with open(tmp_file, 'r') as file:
        rslts = file.read()
        check_create_key_results(rslts)

    os.remove(tmp_file)

def test_create_pop_from_command_line():
    pass

def test_create_pop_from_file():
    pass

def test_create_key_error_handling():
    pass

def test_create_pop_error_handling():
    pass

def check_create_key_results(rslts): 
    results = get_results(rslts)
    
    # check each output has valid value
    assert "PVT_BLS_" in results["Private key"]
    assert "PUB_BLS_" in results["Public key"]
    assert "SIG_BLS_" in results["Proof of Possession"]

def get_results(rslts):
    # sample output looks like
    # Private key: PVT_BLS_kRhJJ2MsM+/CddO...
    # Public key: PUB_BLS_lbUE8922wUfX0Iy5...
    # Proof of Possession: SIG_BLS_olZfcFw...
    pattern = r'(\w+[^:]*): ([^\n]+)'
    matched= re.findall(pattern, rslts)
    
    results = {}
    for k, v in matched:
        results[k.strip()] = v.strip()

    return results

# tests start
try:
    # test create key to console 
    test_create_key_to_console()

    # test create key to file
    test_create_key_to_file()

    # test create pop from private key in command line
    test_create_pop_from_command_line()
    
    # test create pop from private key in file
    test_create_pop_from_file()

    # test error handling in create key
    test_create_key_error_handling()

    # test error handling in create pop
    test_create_key_error_handling()

    testSuccessful=True
except Exception as e:
    Print(e)
    Utils.errorExit("exception during processing")

exitCode = 0 if testSuccessful else 1
exit(exitCode)
