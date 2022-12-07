#!/usr/bin/env python3

import re
import subprocess

def main():
    cmd="programs/nodeos/nodeos --help"
    result = subprocess.run(["programs/nodeos/nodeos", "--help"], capture_output=True, text=True)

    myStr = result.stdout
    myStr = myStr.rstrip("\n")
    myStr = re.sub(":\n\s+-",':@@@\n  -', string=myStr)
    myStr = re.sub("\n\n",'\n@@@', string=myStr)
    myStr = re.sub("Application Options:\n",'', string=myStr)
    pluginSections = re.split("(@@@.*?@@@\n)", string=myStr)


    sec=0
    for section in pluginSections:
        sec=sec+1

    def pairwise(iterable):
        "s -> (s0, s1), (s2, s3), (s4, s5), ..."
        a = iter(iterable)
        return zip(a, a)

    pluginOptsDict = {}
    for section, options in pairwise(pluginSections[1:]):
        myOpts = re.sub("\s+", " ", options)
        myOpts = re.sub("\n", " ", myOpts)
        myOpts = re.sub(" --", "\n--",string = myOpts)
        splitOpts=re.split("\n", myOpts)

        argDescDict = {}
        for opt in splitOpts[1:]:
            secondSplit = re.split("(--[\w\-]+)", opt)[1:]
            argument=secondSplit[0]
            argDefaultDesc=secondSplit[1].lstrip("\s")
            argDescDict[argument] = argDefaultDesc
            # print(f"argDefaultDesc: {argDefaultDesc}")
        section=re.sub("@@@", "", section)
        section=re.sub("\n", "", section)
        sectionSplit=re.split("::", section)
        configSection = section
        if len(sectionSplit) > 1:
            configSection=sectionSplit[1]

        if pluginOptsDict.get(configSection) is not None:
            pluginOptsDict[configSection].update(argDescDict)
        else:
            pluginOptsDict[configSection] = argDescDict

    newDict = {}
    for key, value in pluginOptsDict.items():
        newPlugin="".join([x.capitalize() for x in key.split('_')]).replace(":","")

        newArgs = {}
        for key, value in value.items():
            newKey="".join([x.capitalize() for x in key.split('-')]).replace('--','')
            newKey="".join([newKey[0].lower(), newKey[1:]])
            newArgs[newKey]=value
        newDict[newPlugin]=newArgs

    def writeDataclass(plugin:str, dataFieldDict:dict, pluginOptsDict:dict):
        newPlugin="".join([x.capitalize() for x in plugin.split('_')]).replace(":","")
        pluginArgsFile=f"../tests/performance_tests/{newPlugin}Args.py"
        with open(pluginArgsFile, 'w') as dataclassFile:
            chainPluginArgs = dataFieldDict[newPlugin]

            dataclassFile.write(f"#!/usr/bin/env python3\n\n")            
            dataclassFile.write(f"import dataclasses\n")
            dataclassFile.write(f"import re\n\n")            
            dataclassFile.write(f"from dataclasses import dataclass\n\n")            
            dataclassFile.write(f"@dataclass\nclass {newPlugin}Args:\n")
            dataclassFile.write(f"    _pluginNamespace: str=\"eosio\"\n")
            dataclassFile.write(f"    _pluginName: str=\"{plugin[:-1]}\"\n")

            for key, value in pluginOptsDict[plugin].items():
                newKey="".join([x.capitalize() for x in key.split('-')]).replace('--','')
                newKey="".join([newKey[0].lower(), newKey[1:]])
                value = chainPluginArgs[newKey]
                match = re.search("\(=.*?\)", value)
                if match is not None:
                    value = match.group(0)[2:-1]
                    try:
                        numVal = int(value)
                        dataclassFile.write(f"    {newKey}: int=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosDefault: int={numVal}\n")
                        dataclassFile.write(f"    _{newKey}NodeosArg: str=\"{key}\"\n")
                    except ValueError:
                        strValue = str(value)
                        quote = "\'" if re.search("\"", strValue) else "\""
                        dataclassFile.write(f"    {newKey}: str=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosDefault: str={quote}{strValue}{quote}\n")
                        dataclassFile.write(f"    _{newKey}NodeosArg: str=\"{key}\"\n")
                else:
                    if re.search("deepmind", newKey, re.IGNORECASE) or \
                       re.search("tracehistory", newKey, re.IGNORECASE) or \
                       re.search("tracenoabis", newKey, re.IGNORECASE) or \
                       re.search("chainstatehistory", newKey, re.IGNORECASE) or \
                       re.search("console", newKey, re.IGNORECASE) or \
                       re.search("print", newKey, re.IGNORECASE) or \
                       re.search("verbose", newKey, re.IGNORECASE) or \
                       re.search("debug", newKey, re.IGNORECASE) or \
                       re.search("force", newKey, re.IGNORECASE) or \
                       re.search("onthreshold", newKey, re.IGNORECASE) or \
                       re.search("allowcredentials", newKey, re.IGNORECASE) or \
                       re.search("delete", newKey, re.IGNORECASE) or \
                       re.search("replay", newKey, re.IGNORECASE) or \
                       re.search("onstart", newKey, re.IGNORECASE) or \
                       re.search("onstop", newKey, re.IGNORECASE) or \
                       re.search("enable", newKey, re.IGNORECASE) or \
                       re.search("disable", newKey, re.IGNORECASE):
                        dataclassFile.write(f"    {newKey}: bool=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosDefault: bool=False\n")
                        dataclassFile.write(f"    _{newKey}NodeosArg: str=\"{key}\"\n")
                    elif re.search("sizegb", newKey, re.IGNORECASE) or \
                         re.search("maxage", newKey, re.IGNORECASE) or \
                         re.search("retainblocks", newKey, re.IGNORECASE):
                        dataclassFile.write(f"    {newKey}: int=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosDefault: int=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosArg: str=\"{key}\"\n")
                    else:
                        dataclassFile.write(f"    {newKey}: str=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosDefault: str=None\n")
                        dataclassFile.write(f"    _{newKey}NodeosArg: str=\"{key}\"\n")

            def writeThreadSetter(pluginName: str) -> str:
                if (re.search("chain", pluginName, re.IGNORECASE) or re.search("net", pluginName, re.IGNORECASE) or re.search("producer", pluginName, re.IGNORECASE)):
                    attrName = re.sub("PluginArgs", "", pluginName).lower()
                    return f"""\
    def threads(self, threads: int):
        self.{attrName}Threads=threads\n\n"""
                else:
                    return ""

            def writeSupportedNodeosArgs() -> str:
                return f"""\
    def supportedNodeosArgs(self) -> list:\n\
        args = []\n\
        for field in dataclasses.fields(self):\n\
            match = re.search("\w*NodeosArg", field.name)\n\
            if match is not None:\n\
                args.append(getattr(self, field.name))\n\
        return args\n\n"""

            def writeStrFxn() -> str:
                return f"""\
    def __str__(self) -> str:\n\
        args = [] \n\
        for field in dataclasses.fields(self):\n\
            match = re.search("[^_]", field.name[0])\n\
            if match is not None:\n\
                default = getattr(self, f"_{{field.name}}NodeosDefault")\n\
                current = getattr(self, field.name)\n\
                if current is not None and current != default:\n\
                    if type(current) is bool:
                        args.append(f"{{getattr(self, f'_{{field.name}}NodeosArg')}}")
                    else:
                        args.append(f"{{getattr(self, f'_{{field.name}}NodeosArg')}} {{getattr(self, field.name)}}")

        return "--plugin " + self._pluginNamespace + "::" + self._pluginName + " " + " ".join(args) if len(args) > 0 else ""\n\n"""

            def writeMainFxn(pluginName: str) -> str:
                return f"""\
def main():\n\
    pluginArgs = {pluginName}()\n\
    print(pluginArgs.supportedNodeosArgs())\n\
    exit(0)\n\n\
if __name__ == '__main__':\n\
    main()\n"""

            def writeHelpers(pluginName: str) -> str:
                return "\n" + writeThreadSetter(pluginName) + writeSupportedNodeosArgs() + writeStrFxn() + writeMainFxn(pluginName)

            dataclassFile.write(writeHelpers(f"{newPlugin}Args"))
    
    writeDataclass(plugin="chain_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="http_client_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="http_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="net_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="producer_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="resource_monitor_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="signature_provider_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="state_history_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
    writeDataclass(plugin="trace_api_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)

    exit(0)

if __name__ == '__main__':
    main()
