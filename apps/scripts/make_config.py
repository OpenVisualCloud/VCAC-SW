#!/bin/python
#
# Intel VCA Software Stack (VCASS)
#
# Copyright(c) 2017 Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2, as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Intel VCA Scripts.
#

import sys
import subprocess as proc
import xml.etree.ElementTree as XmlTree
import copy
import signal

UPDATE_ACTIONS = {
    "no_action"     : 0, #option saved without change
    "apply_user"    : 1, #option set to user value
    "wrn_apply_user": 2, #option set to user value, prints warning
    "new_default"   : 3, #option set to new default value
    "delete"        : 4, #option is deleted
    "error"         : 5, #error
    "new_usr"       : 6, #ask user to specify new value
    "unknown"       : 7  #unhandled situation, triggers script exit
}

#Will run only if 'debug' command line argument will be passed
def decode_update_actions():
    decoded = {}
    for action in UPDATE_ACTIONS:
        decoded[UPDATE_ACTIONS[action]] = action
    return decoded

decoded_actions = {}

#
# Changes in configuration are encoded using vector of 3 elements
# Vector indexes corespond to config as follow:
# [0] - old default config
# [1] - user config
# [2] - new default config
#
# Option state is encoded as follow:
# d   - default
# m   - modified, also describes added value (ex. new blockIO device)
# del - deleted
# nd  - new default value, state possible only for new config
#
# Example:
#    ["d", "m", "del"]
# means the given option  in:
#   OLD default config have DEFAULT value
#   USER config value is diffrent form one in OLD config
#   NEW default config does not have this option, it was DELETED in new revision
#

#
# Exception classes
#
class BadXMLConfiguration(Exception):
    def __init__(self, msg):
        super(BadXMLConfiguration, self).__init__(msg)

class UnknownActionTaken(Exception):
    def __init__(self, msg):
        super(UnknownActionTaken, self).__init__(msg)

class VcactlInvalidParameterName(Exception):
    def __init__(self):
        super(UnknownActionTaken, self).__init__("Invalid parameter passed to vcactl!")

class VcactlError(Exception):
    def __init__(self):
        super(UnknownActionTaken, self).__init__("vcactl error!")

#
#wrn_up - will be set true if any waring will be printed. If this will be set to true,
#         additional information will be shown to user
#
class warning_holder:
    def __init__(self):
        self.__wrn_up=False
    def set_wrn_up(self):
        self.__wrn_up=True
    def check(self):
        return self.__wrn_up

WRN_UP=warning_holder()


#
# Base class for update strategies
#
import abc
class AbstractUpdate:
    __metaclass__ = abc.ABCMeta
    __option_state_dict = {
        "d"     : "in default state",
        "m"     : "modified by user",
        "del"   : "deleted",
        "nd"    : "in NEW default state"
    }

    def __init__(self):
        self.__print_debug = False

    def set_debug(self):
        print "[DEBUG] Debug mode is on, script will not run vcactl, script will only print command to run"
        self.__print_debug = True

    def _AbstractUpdate__decode_and_print_changes(self, changes):
        check_none = lambda val: str(val) if val is not None else "<not set>"
        print "\tOLD DEFAULT configuration: "   + self.__option_state_dict[changes[0]] + "[" + check_none(self.__real_values[0]) + "]"
        print "\tUSER configuration:        "   + self.__option_state_dict[changes[1]] + "[" + check_none(self.__real_values[1]) + "]"
        print "\tNEW DEFAULT configuration: "   + self.__option_state_dict[changes[2]] + "[" + check_none(self.__real_values[2]) + "]"

    @abc.abstractmethod
    def _AbstractUpdate__decide_action(self, changes, option, cardId, nodeId, block_dev_name):
        return UPDATE_ACTIONS["unknown"]

    def __print_message(self, action, option, changes):
        if self.__print_debug:
            print "[DEBUG] " + decoded_actions[action] + "[" + str(action) + "] on option: " + option + " changes vector: " + str(changes)

        if action == UPDATE_ACTIONS['unknown']:
            raise UnknownActionTaken("[ERROR] Can not decide what update action take on option: " + option + " changes vector: " + str(changes))

        if action == UPDATE_ACTIONS['error']:
            raise BadXMLConfiguration("[ERROR] USER configuration is corrupted. Missing option: " + option)

        if action == UPDATE_ACTIONS["wrn_apply_user"]:
            WRN_UP.set_wrn_up()
            check_none = lambda val: str(val) if val is not None else "<not set>"
            print "[WARNING] Option: " + option + " have new default value [" + check_none(self.__real_values[2]) + "]"

    def run(self, changes, option, real_values, cardId, nodeId, block_dev_name):
        self.__real_values = real_values
        action = self._AbstractUpdate__decide_action(changes, option, cardId, nodeId, block_dev_name)
        self.__print_message(action, option, changes)
        return action

#
# Manual mode - always ask user what to do
#
class ManualUpdate(AbstractUpdate):
    name = "Manual mode"

    def __ask_user(self, changes, option, cardId, nodeId, block_dev_name):
        if cardId is None and nodeId is None:
            option_info = "(global): "
        else:
            if block_dev_name is not None:
                option_info = "for card " + str(cardId) + ", cpu " + str(nodeId) + ", " + block_dev_name + ": "
            else:
                option_info = "for card " + str(cardId) + ", cpu " + str(nodeId) + ": "

        print "Option " + option_info + option + " is in: "
        super(ManualUpdate, self)._AbstractUpdate__decode_and_print_changes(changes)
        print "Please choose what to do from given options:"
        print "\t1. Apply USER value."
        print "\t2. Use NEW default value."
        print "\t3. Choose new value."

        try:
            user_choice = input("Action[2]:")
        except (SyntaxError, NameError):
            user_choice = 2

        if user_choice == 1:
            return UPDATE_ACTIONS["apply_user"]
        elif user_choice == 3:
            return UPDATE_ACTIONS["new_usr"]
        else:
            return UPDATE_ACTIONS["new_default"]

    def _AbstractUpdate__decide_action(self, changes, option, cardId, nodeId, block_dev_name):
        if changes[1] == 'del' and (changes[0] == 'd' and changes[2] == 'd'):
            return UPDATE_ACTIONS["error"]
        else:
            return self.__ask_user(changes, option, cardId, nodeId, block_dev_name)

#
# Semi automatic mode - Will favour user set values, but in contested situations will ask user what to do
#
class SemiAutoUpdate(AbstractUpdate):
    name = "Semi-auto mode"

    def __ask_user(self, changes, option, cardId, nodeId, block_dev_name):
        if cardId is None and nodeId is None:
            option_info = "(global): "
        else:
            if block_dev_name is not None:
                option_info = "for card " + str(cardId) + ", cpu " + str(nodeId) + ", " + block_dev_name + ": "
            else:
                option_info = "for card " + str(cardId) + ", cpu " + str(nodeId) + ": "

        print "Option " + option_info + option + " is in: "
        super(SemiAutoUpdate, self)._AbstractUpdate__decode_and_print_changes(changes)
        print "Please choose what to do from given options:"
        print "\t1. Apply USER value."
        print "\t2. Use NEW default value."
        print "\t3. Choose new value."

        try:
            user_choice = input("Action[2]:")
        except (SyntaxError, NameError):
            user_choice = 2

        if user_choice == 1:
            return UPDATE_ACTIONS["apply_user"]
        elif user_choice == 3:
            return UPDATE_ACTIONS["new_usr"]
        else:
            return UPDATE_ACTIONS["new_default"]

    def _AbstractUpdate__decide_action(self, changes, option, cardId, nodeId, block_dev_name):
        if changes[0] == 'd':
            if changes[1] == 'd':
                if changes[2] == 'd':
                    return UPDATE_ACTIONS['no_action']
                elif changes[2] == 'nd':
                    return UPDATE_ACTIONS['new_default']
                elif changes[2] == 'del':
                    return UPDATE_ACTIONS['delete']
            elif changes[1] == 'm':
                if changes[2] == 'd':
                    return UPDATE_ACTIONS['apply_user']
                elif changes[2] == 'nd' or 'del':
                    return self.__ask_user(changes, option, cardId, nodeId, block_dev_name)
            elif changes[1] == 'del':
                return UPDATE_ACTIONS["error"]

        if changes[0] == 'del':
            if changes[1] == 'm':
                if changes[2] == 'del':
                    return UPDATE_ACTIONS['apply_user']
                elif changes[2] == 'nd':
                    return UPDATE_ACTIONS['new_default']
                else:
                    self.__ask_user(changes, option, cardId, nodeId, block_dev_name)

        return UPDATE_ACTIONS["unknown"] #Unknown case

#
# Full automatic mode - Apply old USER value when possible. May trigger ERROR or show WARNING
#
class FullAutoUpdate(AbstractUpdate):
    name = "Full automatic mode"
    def _AbstractUpdate__decide_action(self, changes, option, cardId, nodeId, block_dev_name):
        if changes[0] == 'd' and changes[1] == 'm' and changes[2] == 'd':
            return UPDATE_ACTIONS["apply_user"]
        if changes[0] == 'del' or changes[0] == 'd':
            if changes[1] == 'm':
                if changes[2] == 'del':
                    return UPDATE_ACTIONS["apply_user"]
                elif changes[2] == 'd' or changes[2] == 'nd':
                    return UPDATE_ACTIONS["wrn_apply_user"]

        if changes[1] == 'del' and (changes[0] == 'd' and changes[2] == 'd'):
            return UPDATE_ACTIONS["error"]

        if changes[2] == 'del':
            return UPDATE_ACTIONS["delete"]

        if changes[0] == 'd':
            if changes[1] == 'd' and (changes[2] == 'd' or changes[2] == 'nd'):
                if changes[2] == 'd':
                    return UPDATE_ACTIONS["no_action"]
                else:
                    return UPDATE_ACTIONS["new_default"]

        return UPDATE_ACTIONS["unknown"] #Unknown case

#
# Objects of this class performs update, using vcactl, according to given strategy
#
class VcactlUpdater:
    def __init__(self, strategy, options, cardId = None, nodeId = None, block_dev_name=None):
        self.__cardId = cardId
        self.__nodeId = nodeId
        self.__block_dev_name = block_dev_name
        self.__strategy = strategy
        self.__options_in_xmls = options
        self.__debug = False
        if cardId is not None and nodeId is not None:
            self.__vcactl_command_base = [
                "vcactl",
                "config",
                str(self.__cardId),
                str(self.__nodeId)
            ]
            if block_dev_name is not None:
                self.__vcactl_command_base.append(block_dev_name)
        else:
            self.__vcactl_command_base = [
                "vcactl",
                "config",
            ]

    def set_debug(self):
        self.__debug = True
        self.__strategy.set_debug()

    def __get_command(self, option, value):
        tmp = list(self.__vcactl_command_base)
        tmp.append(option)
        if value is None:
            tmp.append("")
        else:
            tmp.append(value)
        return tmp

    def __run_vcactl(self, option, value):
        if self.__debug:
            print self.__get_command(option, value)
            return
        proc.check_call(self.__get_command(option, value))


    def __get_action(self, changes, option, values, cardId, nodeId, block_dev_name):
        return self.__strategy.run(changes, option, values, cardId, nodeId, block_dev_name)

    def __get_new_user_value(self, option_name):
            user_choice = None
            while user_choice is None:
                try:
                    user_choice = input("Please input new value for " + option_name + ": ")
                except SyntaxError:
                    user_choice = None
            return str(user_choice)


    def __choose_value(self, action, option_entry, option_name):
        if action == UPDATE_ACTIONS["new_usr"]:
            return self.__get_new_user_value(option_name)
        if action == UPDATE_ACTIONS["no_action"]:
            return option_entry[0]
        if action == UPDATE_ACTIONS["apply_user"] or action == UPDATE_ACTIONS["wrn_apply_user"]:
            return option_entry[1]
        if action == UPDATE_ACTIONS["new_default"]:
            return option_entry[2]
        if action == UPDATE_ACTIONS["delete"]:
            return ""

    def __encode_changes(self, option_entry):
        if option_entry[0] == option_entry[1]:
            #Not changed option
            if option_entry[1] == option_entry[2]:
                return ['d', 'd', 'd']
            #Not changed by user, but new default value in newest rev.
            elif (option_entry[1] != option_entry[2]) and option_entry[2] is not None:
                return ['d','d','nd']
            #Option removed form XML in new rev. and not altered by user
            else:
                return ['d', 'd', 'del']
        elif (option_entry[0] != option_entry[1]) and option_entry[1] is not None:
            #"Last used image case"
            if option_entry[2] == option_entry[0]:
                return ['d', 'm', 'd']
            #Option removed form XML in new rev. and altered by user
            elif option_entry[2] is None:
                return ['d', 'm', 'del']
            #Option have new default value in new rev. but it was altered by user
            else:
                return ['d', 'm', 'nd']
        #Option added by user
        elif option_entry[0] is None and option_entry[1] is not None and option_entry[2] is None:
            return ['del', 'm', 'del']
        #Option removed by user
        elif (option_entry[0] == option_entry[2]) and option_entry[1] is None:
            return ['d', 'del', 'd']
        #Option added by user, have new default value in new rev.
        elif option_entry[0] is None and option_entry[1] is not None and option_entry[2] is not None:
            return ['del', 'm', 'd']
        #New option
        elif option_entry[0] is None and option_entry[1] is None and option_entry[2] is not None:
            return ['del', 'del', 'nd']
        elif option_entry[0] is None and option_entry[1] is None and option_entry[2] is None:
            return ['del', 'del', 'del']
        #error
        else:
            return None

    #options_in_xml - dictionary which contains:
    #                       as KEY name of the option
    #                       as VALUE, 3 element vector: [<old_default_config_value>, <user_config_value>, <new_default_config_value>]
    #                 If update will go trough block devices before structure described above, name of block device is stored, ex vcablk0
    def execute(self):
       for option_name in self.__options_in_xmls:
           changes = self.__encode_changes(self.__options_in_xmls[option_name])
           action = self.__get_action(changes, option_name, self.__options_in_xmls[option_name],
                                      self.__cardId, self.__nodeId, self.__block_dev_name)
           value = self.__choose_value(action, self.__options_in_xmls[option_name], option_name)
           self.__run_vcactl(option_name, value)

class XMLContainer:
    def __init__(self, orginalDef, user, newDef):
        self.oldDef = XmlTree.parse(orginalDef)
        self.user = XmlTree.parse(user)
        self.newDef = XmlTree.parse(newDef)
        self.xml = {
            "o" : self.oldDef,
            "u" : self.user,
            "n" : self.newDef
        }

    def __get_root_of(self, xmlConfigFile):
        return self.xml[xmlConfigFile].getroot()

    def __get_all_roots(self):
        all_roots = {}
        for xml in self.xml:
            all_roots[xml] = self.__get_root_of(xml)
        return all_roots

    def get_global_attribs(self):
        roots = self.__get_all_roots()
        global_attr = {}
        for config in roots:
            globalSection = roots[config].findall('global')
            props = {}

            for item in globalSection[0]: #Only one global section per config
                if item.text == "None":
                    props[item.tag] = None
                else:
                    props[item.tag] = item.text

            global_attr[config] = props

        return global_attr

    def get_card_cpu_specific_attribs(self):
        roots = self.__get_all_roots()
        all_card = [None]*8

        attr = {}
        for config in roots:
            for card in roots[config].findall('card'):
                all_cpu = [None]*3
                for cpu in card.findall('cpu'):
                    prop = {}
                    for item in cpu:
                        if item.tag == "block-devs":
                            pass
                        prop[item.tag] = item.text
                        all_cpu[int(cpu.attrib["id"])] = prop
                all_card[int(card.attrib["id"])] = copy.deepcopy(all_cpu)
            attr[config] = copy.deepcopy(all_card)
        return attr

    def get_block_devs_attribs(self):
        roots = self.__get_all_roots()
        mapped = {}
        for config in roots:
            block_devs = [[None for x in range(3)]for x in range(8)]
            for card in roots[config].findall('card'):
                for cpu in card.findall('cpu'):
                    for block_dev in cpu.findall('block-devs'):
                        bDevs = []
                        for dev in block_dev:
                            bDevs.append(dev)
                    block_devs[int(card.attrib["id"])][int(cpu.attrib["id"])] = bDevs
            mapped[config] = block_devs
        return mapped

XML_IDX = {
    'o' : 0,
    'u' : 1,
    'n' : 2
}

def combine_properties(configurations, _type = "global"):

    if _type == "global":
        resutl = {}
        for xml_configuration in configurations:
            for _property in configurations[xml_configuration]:
                resutl[_property] = [None]*3

        for xml_configuration in configurations:
            for _property in configurations[xml_configuration]:
                resutl[_property][XML_IDX[xml_configuration]] = configurations[xml_configuration][_property]
        return resutl
    else:
        result = [[{} for x in range(3)]for x in range(8)] #[[None]*3]*8

        for xml_configuration in configurations:
            for card_id in xrange(len(configurations[xml_configuration])):
                for cpu_id in xrange(len(configurations[xml_configuration][card_id])):
                    for item in configurations[xml_configuration][card_id][cpu_id]:
                        if _type == "block_dev":
                            block_dev_name = item.tag
                            result[card_id][cpu_id][block_dev_name] = {}
                            for prop in item:
                                result[card_id][cpu_id][block_dev_name][prop.tag] = [None]*3
                        else:
                            result[card_id][cpu_id][item] = [None]*3

        for xml_configuration in configurations:
            for card_id in xrange(len(configurations[xml_configuration])):
                for cpu_id in xrange(len(configurations[xml_configuration][card_id])):
                    for item in configurations[xml_configuration][card_id][cpu_id]:
                        if _type == "block_dev":
                            block_dev_name = item.tag
                            for prop in item:
                                result[card_id][cpu_id][block_dev_name][prop.tag][XML_IDX[xml_configuration]] = prop.text
                        else:
                            result[card_id][cpu_id][item][XML_IDX[xml_configuration]] = configurations[xml_configuration][card_id][cpu_id][item]
        return result


def print_help():
    print "VCA configuration update script. Script performs update of VCA configuration. Can work in given modes:"
    print "\tmanual    - Always ask what to do."
    print "\tsemi-auto - Will favour user set values, but in contested situations will ask user what to do."
    print "\tfull-auto - Apply USER value when possible. May trigger ERROR(execution will be terminated) or show WARNING. This is default mode."
    print "Example call:"
    print "\tvca_config_upgrade --mode manual"
    print "\tvca_config_upgrade -m full-auto"
    print "Calling with -h or --help will print this message."
#   print "Additionally debug work mode can be requested by providing 'debug' parameter."
#   print "In debug mode script will not perform update. Example call in debug mode:"
#   print "\t vca_config_upgrade full-auto debug"
    sys.exit()

import os.path as fPath
def arg_parse(argv):
    xml_files = []
    for i in range(1,4):
        if argv[1] == 'help':
            print_help()
        f_ext =fPath.splitext(argv[i])[1]
        if f_ext != '.xml':
            raise RuntimeError('[ERROR] Bad arguments passed! First 3 arguments have to be the XML configuration files or 1st argument have to be: "help"')
        xml_files.append(argv[i])

    other_opt= {}
    for i in range(4, len(argv)):
        if argv[i] == 'debug':
            other_opt["dbg"] = 1
        elif (argv[i] == "manual") or (argv[i] == "full-auto") or (argv[i] == "semi-auto") :
            other_opt["up_mode"] = argv[i]
    return {"xml": xml_files, "other": other_opt}

UPDATE_MODE = {
    "full-auto" : FullAutoUpdate,
    "semi-auto" : SemiAutoUpdate,
    "manual"    : ManualUpdate
}

def print_update_mode(update_mode):
    print "UPDATE MODE = " + update_mode.name

def sigint_handler(signal, frame):
    print('\nCatched SIGINT! Aborting..')
    sys.exit(1)

if __name__ == "__main__":
    signal.signal(signal.SIGINT, sigint_handler)

    arg = arg_parse(sys.argv)
    try:
        xmlCont = XMLContainer(arg["xml"][0], arg["xml"][1], arg["xml"][2],)
    except IOError as err:
        print "[ERROR] Cannot open file: " + err.filename
        sys.exit(3)

    try:
        update_mode = UPDATE_MODE[arg["other"]["up_mode"]]()
    except(KeyError):
        update_mode = UPDATE_MODE["full-auto"]()

    print_update_mode(update_mode)

    vca_updaters = []
    #Create update objects
    #global section update
    globalOptions = combine_properties(xmlCont.get_global_attribs())
    vca_updaters.append(VcactlUpdater(update_mode, globalOptions))

    #card-node update
    cardCpuOpt = xmlCont.get_card_cpu_specific_attribs()
    cardCpuOpt = combine_properties(cardCpuOpt, "cardCpu")

    block_dev_list = xmlCont.get_block_devs_attribs()
    block_dev_opt = combine_properties(block_dev_list, "block_dev")

    for card in xrange(8):
        for cpu in xrange(3):
            vca_updaters.append(VcactlUpdater(update_mode, cardCpuOpt[card][cpu],card, cpu))
            for block_device in block_dev_opt[card][cpu] :
                vca_updaters.append(VcactlUpdater(update_mode, block_dev_opt[card][cpu][block_device],card, cpu, block_dev_name=block_device))
    for update in vca_updaters:
        try:
            if arg["other"]["dbg"] == 1:
                decoded_actions = decode_update_actions()
                update.set_debug()
        except(KeyError):
            pass
        try:
            update.execute()
        except(BadXMLConfiguration, UnknownActionTaken, proc.CalledProcessError) as err:
            print err.message
            if type(err) is BadXMLConfiguration:
                print "\tBad User XML configuration was passed. You probably intentionally deleted option by directly editing file with configuration."
                print "\tOld configuration will be saved, but new default configuration will be used."
                print "\tIf you want you can change it using 'vcactl' or by running this script in manual or semi-auto update mode:"
                print "\t\tvca_config_upgrade --mode manual"
                print "\t\tvca_config_upgrade --mode semi-auto"
                sys.exit(3)
            if type(err) is UnknownActionTaken:
                print "\tMerge run into conflict that can not be resolved in automatic mode because of unknown reasons."
                print "\tOld configuration will be saved, but new default configuration will be used."
                print "\tIf you want, you can change it using 'vcactl' or by running this script in manual or semi-auto update mode:"
                print "\t\tvca_config_upgrade --mode manual"
                print "\t\tvca_config_upgrade --mode semi-auto"
                sys.exit(3)
            if type(err) is proc.CalledProcessError:
                print "\tVcactl error occured. Aborting."
                print "\tOld configuration will be saved, but new default configuration will be used."
                print "\t\tvca_config_upgrade --mode manual"
                print "\t\tvca_config_upgrade --mode semi-auto"
                sys.exit(err.returncode)

    if WRN_UP.check():
        print "\tAutomatic merge ended correctly, but some contested situations occurred. Values set by USER where selected, but you should check"
        print "\tmessages labeled with [WARNING], and consider setting new value for this parameters."
        print "\tYou can use 'vcactl' or run this script in manual or semi-auto update mode:"
        print "\t\tvca_config_upgrade --mode manual"
        print "\t\tvca_config_upgrade --mode semi-auto"

    print "Configuration update done!"
    sys.exit(0)
