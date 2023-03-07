#!/usr/bin/env python3

import re
import subprocess

"""
The purpose of this script is to attempt to generate *PluginArgs.py files, containing respective dataclass objects,
to encapsulate the configurations options available for each plugin as currently documented in nodeos's --help command.

It makes use of the compiled nodeos program and runs the --help command, capturing the output.
It then parses the output, breaking down the presented configuration options by plugin section (ignoring application and test plugin config options).
This provides a rudimentary list of plugins supported, config options for each plugin, and attempts to acertain default values and types.
The script then uses the parsed output to generate *PluginArgs.py scripts, placing them in the NodeosPluginArgs directory.

Currently it generates the following scripts:
- ChainPluginArgs.py
- HttpPluginArgs.py
- NetPluginArgs.py
- ProducerPluginArgs.py
- ResourceMonitorPluginArgs.py
- SignatureProviderPluginArgs.py
- StateHistoryPluginArgs.py
- TraceApiPluginArgs.py

Each *PluginArgs.py file contains one dataclass that captures the available configuration options for that plugin via nodeos command line.

Each config options is represented by 3 member variables, for example:
1) blocksDir: str=None
    --This is the field that will be populated when the dataclass is used by other scripts to configure nodeos
2) _blocksDirNodeosDefault: str='"blocks"'
    --This field captures the default value in the nodeos output.  This will be compared against the first field to see if the configuration
    option will be required on the command line to override the default value when running nodeos.
3) _blocksDirNodeosArg: str="--blocks-dir"
    --This field captures the command line config option for use when creating the command line string

The BasePluginArgs class provides implementations for 2 useful functions for each of these classes:
1) supportedNodeosArgs
    -- Provides a list of all the command line config options currently supported by the dataclass
2) __str__
    -- Provides the command line argument string for the current configuration to pass to nodeos
       (this only provides command line options where configured values differ from defaults)

Some current limitations:
- There are some hardcoded edge cases when trying to determine the types associated with certain default argument parameters.
  These may need to be updated to account for new/different options as they are added/removed/modified by nodeos

Note:
- To help with maintainability the validate_nodeos_plugin_args.py test script is provided which validates the current
  *PluginArgs dataclass configuration objects against the current nodeos --help output to notify developers when
  configuration options have changed and updates are required.
"""


def main():
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
        pluginArgsFile=f"../tests/performance_tests/NodeosPluginArgs/{newPlugin}Args.py"
        with open(pluginArgsFile, 'w') as dataclassFile:
            chainPluginArgs = dataFieldDict[newPlugin]

            dataclassFile.write(f"#!/usr/bin/env python3\n\n")
            dataclassFile.write(f"from dataclasses import dataclass\n")
            dataclassFile.write(f"from .BasePluginArgs import BasePluginArgs\n\n")
            dataclassFile.write(f"\"\"\"\n")
            dataclassFile.write(f"This file/class was generated by generate_nodeos_plugin_args_class_files.py\n")
            dataclassFile.write(f"\"\"\"\n\n")
            dataclassFile.write(f"@dataclass\nclass {newPlugin}Args(BasePluginArgs):\n")
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

            def writeMainFxn(pluginName: str) -> str:
                return f"""\
def main():\n\
    pluginArgs = {pluginName}()\n\
    print(pluginArgs.supportedNodeosArgs())\n\
    exit(0)\n\n\
if __name__ == '__main__':\n\
    main()\n"""

            def writeHelpers(pluginName: str) -> str:
                return "\n" + writeMainFxn(pluginName)

            dataclassFile.write(writeHelpers(f"{newPlugin}Args"))
    
    writeDataclass(plugin="chain_plugin:", dataFieldDict=newDict, pluginOptsDict=pluginOptsDict)
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
