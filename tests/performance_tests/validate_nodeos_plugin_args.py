#!/usr/bin/env python3

import re
import subprocess

from NodeosPluginArgs import ChainPluginArgs, HttpPluginArgs, NetPluginArgs, ProducerPluginArgs, ResourceMonitorPluginArgs, SignatureProviderPluginArgs, StateHistoryPluginArgs, TraceApiPluginArgs

testSuccessful = False

regenSuggestion = "Try updating *PluginArgs classes to nodeos's current config options by running the script: generate_nodeos_plugin_args_class_files.py. \
    Updates to generation script may be required if a plugin was added/removed or in some default parameter cases."

def parseNodeosConfigOptions() -> dict:
    result = subprocess.run(["programs/nodeos/nodeos", "--help"], capture_output=True, text=True)

    myStr = result.stdout
    myStr = myStr.rstrip("\n")
    myStr = re.sub(":\n\s+-",':@@@\n  -', string=myStr)
    myStr = re.sub("\n\n",'\n@@@', string=myStr)
    myStr = re.sub("Application Options:\n",'', string=myStr)
    pluginSections = re.split("(@@@.*?@@@\n)", string=myStr)

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

        argDefaultsDict = {}
        for opt in splitOpts[1:]:
            secondSplit = re.split("(--[\w\-]+)", opt)[1:]
            argument=secondSplit[0]
            argDefaultDesc=secondSplit[1].lstrip("\s")
            default = None
            match = re.search("\(=.*?\)", argDefaultDesc)
            if match is not None:
                value = match.group(0)[2:-1]
                try:
                    default = int(value)
                except ValueError:
                    default = str(value)
            argDefaultsDict[argument] = default

        section=re.sub("@@@", "", section)
        section=re.sub("\n", "", section)
        sectionSplit=re.split("::", section)
        configSection = section
        if len(sectionSplit) > 1:
            configSection=sectionSplit[1]
        
        if configSection[-1] == ":":
            configSection = configSection[:-1]

        if pluginOptsDict.get(configSection) is not None:
            pluginOptsDict[configSection].update(argDefaultsDict)
        else:
            pluginOptsDict[configSection] = argDefaultsDict
    return pluginOptsDict

nodeosPluginOptsDict = parseNodeosConfigOptions()

curListOfSupportedPlugins = [ChainPluginArgs(), HttpPluginArgs(), NetPluginArgs(), ProducerPluginArgs(),
                             ResourceMonitorPluginArgs(), SignatureProviderPluginArgs(), StateHistoryPluginArgs(), TraceApiPluginArgs()]

curListOfUnsupportedOptionGroups = ["txn_test_gen_plugin", "Application Config Options", "Application Command Line Options"]

#Check whether nodeos has added any plugin configuration sections
for confSection in nodeosPluginOptsDict.keys():
    assert confSection in [paClass._pluginName for paClass in curListOfSupportedPlugins] or confSection in curListOfUnsupportedOptionGroups, f"ERROR: New config section \"{confSection}\" added to nodeos which may require updates. {regenSuggestion}"

def argStrToAttrName(argStr: str) -> str:
    attrName="".join([x.capitalize() for x in argStr.split('-')]).replace('--','')
    attrName="".join([attrName[0].lower(), attrName[1:]])
    return attrName

for supportedPlugin in curListOfSupportedPlugins:
    #Check whether nodeos has removed any plugin configuration sections
    assert supportedPlugin._pluginName in nodeosPluginOptsDict, f"ERROR: Supported config section \"{supportedPlugin._pluginName}\" no longer supported by nodeos. {regenSuggestion}"

    for opt in supportedPlugin.supportedNodeosArgs():
        #Check whether nodeos has removed any arguments in a plugin
        assert opt in nodeosPluginOptsDict[supportedPlugin._pluginName].keys(), f"ERROR: nodeos no longer supports \"{opt}\" in \"{supportedPlugin._pluginName}\". {regenSuggestion}"


        ourDefault = getattr(supportedPlugin, f"_{argStrToAttrName(opt)}NodeosDefault")
        nodeosCurDefault = nodeosPluginOptsDict[supportedPlugin._pluginName][opt]
        if type(ourDefault) == bool and nodeosCurDefault is None:
            nodeosCurDefault=False
        #Check whether our defaults no longer match nodeos's
        assert ourDefault == nodeosCurDefault, f"ERROR: {type(supportedPlugin)}'s default for \"{opt}\" is {ourDefault} and no longer matches nodeos's default {nodeosCurDefault} in \"{supportedPlugin._pluginName}\". {regenSuggestion}"

    #Check whether nodeos has added/updated any argument defaults
    for nodeosOpt, defaultValue in nodeosPluginOptsDict[supportedPlugin._pluginName].items():
        assert nodeosOpt in supportedPlugin.supportedNodeosArgs(), f"ERROR: New nodeos option \"{nodeosOpt}\". Support for this option needs to be added to {type(supportedPlugin)}. {regenSuggestion}"
        
        ourDefault = getattr(supportedPlugin, f"_{argStrToAttrName(nodeosOpt)}NodeosDefault")
        if type(ourDefault) == bool and defaultValue is None:
            defaultValue=False
        assert defaultValue == ourDefault, f"ERROR: nodeos's default for \"{nodeosOpt}\" is {nodeosCurDefault} and no longer matches {type(supportedPlugin)}'s default: {ourDefault} in \"{supportedPlugin._pluginName}\". {regenSuggestion}"

testSuccessful = True

exitCode = 0 if testSuccessful else 1
exit(exitCode)
