import random
import re
import string
import subprocess

from .testUtils import Utils

# Class for generating distinct names for many accounts
class NamedAccounts:

    def __init__(self, cluster, numAccounts):
        Utils.Print("NamedAccounts %d" % (numAccounts))
        self.numAccounts=numAccounts
        self.accounts=createAccountKeys(numAccounts)
        if self.accounts is None:
            Utils.errorExit("FAILURE - create keys")
        accountNum = 0
        for account in self.accounts:
            Utils.Print("NamedAccounts Name for %d" % (accountNum))
            account.name=self.setName(accountNum)
            accountNum+=1

    def setName(self, num):
        retStr="test"
        digits=[]
        maxDigitVal=5
        maxDigits=8
        temp=num
        while len(digits) < maxDigits:
            digit=(num % maxDigitVal)+1
            num=int(num/maxDigitVal)
            digits.append(digit)

        digits.reverse()
        retStr += "".join(map(str, digits))

        Utils.Print("NamedAccounts Name for %d is %s" % (temp, retStr))
        return retStr

###########################################################################################
class Account(object):
    # pylint: disable=too-few-public-methods

    def __init__(self, name):
        self.name=name

        self.ownerPrivateKey=None
        self.ownerPublicKey=None
        self.activePrivateKey=None
        self.activePublicKey=None


    def __str__(self):
        return "Name: %s" % (self.name)

    def __repr__(self):
        return "Name: %s" % (self.name)

def createAccountKeys(count: int) -> list[Account]:
    accounts=[]
    p = re.compile('Private key: (.+)\nPublic key: (.+)\n', re.MULTILINE)
    for _ in range(0, count):
        try:
            cmd="%s create key --to-console" % (Utils.EosClientPath)
            if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
            keyStr=Utils.checkOutput(cmd.split())
            m=p.search(keyStr)
            if m is None:
                Utils.Print("ERROR: Owner key creation regex mismatch")
                break

            ownerPrivate=m.group(1)
            ownerPublic=m.group(2)

            cmd="%s create key --to-console" % (Utils.EosClientPath)
            if Utils.Debug: Utils.Print("cmd: %s" % (cmd))
            keyStr=Utils.checkOutput(cmd.split())
            m=p.match(keyStr)
            if m is None:
                Utils.Print("ERROR: Active key creation regex mismatch")
                break

            activePrivate=m.group(1)
            activePublic=m.group(2)

            name=''.join(random.choice(string.ascii_lowercase) for _ in range(12))
            account=Account(name)
            account.ownerPrivateKey=ownerPrivate
            account.ownerPublicKey=ownerPublic
            account.activePrivateKey=activePrivate
            account.activePublicKey=activePublic
            accounts.append(account)
            if Utils.Debug: Utils.Print("name: %s, key(owner): ['%s', '%s], key(active): ['%s', '%s']" % (name, ownerPublic, ownerPrivate, activePublic, activePrivate))

        except subprocess.CalledProcessError as ex:
            msg=ex.stderr.decode("utf-8")
            Utils.Print("ERROR: Exception during key creation. %s" % (msg))
            break

    if count != len(accounts):
        Utils.Print("Account keys creation failed. Expected %d, actual: %d" % (count, len(accounts)))
        return None

    return accounts
