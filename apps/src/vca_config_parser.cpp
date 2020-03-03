/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2020 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel VCA User Space Tools.
 */

#include <string.h>
#include <sys/file.h>
#include <grp.h>

#include <boost/foreach.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/detail/xml_parser_write.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/detail/xml_parser_error.hpp>

#include "vca_config_parser.h"
#include "helper_funcs.h"
#include "vca_common.h"

using namespace Printer;
using namespace vca_config_parser;

/* to avoid Static Initialization Order Fiasco we need to wrap it into a function */
const data_field & global_fields(global_fields::_enum field) {
	static data_field **global = NULL;
	if (!global) {
		global = new data_field*[global_fields::ENUM_SIZE];
		assert(global);
		global[global_fields::auto_boot] = new data_field(data_field::number, "auto-boot", "1");
		global[global_fields::debug_enabled] = new data_field(data_field::number, "debug-enabled", "0");
		global[global_fields::link_up_timeout_ms] = new data_field(data_field::number, "link-up-timeout-ms", "2000");
		global[global_fields::handshake_irq_timeout_ms] = new data_field(data_field::number, "handshake-irq-timeout-ms", "30000");
		global[global_fields::alloc_timeout_ms] = new data_field(data_field::number, "alloc-timeout-ms", "100");
		global[global_fields::cmd_timeout_ms] = new data_field(data_field::number, "cmd-timeout-ms", "1000");
		global[global_fields::mac_write_timeout_ms] = new data_field(data_field::number, "mac-write-timeout-ms", "1000");
		global[global_fields::default_daemon_script] = new data_field(data_field::string, "default-daemon-script", "");
		global[global_fields::wait_cmd_timeout_s] = new data_field(data_field::number, "wait-cmd-timeout-s", "200");
		global[global_fields::wait_bios_cmd_timeout_s] = new data_field(data_field::number, "wait-bios-cmd-timeout-s", "300");
		global[global_fields::wait_bios_cmd_flashing_s] = new data_field(data_field::number, "wait-bios-cmd-flashing-s", "1200");
		global[global_fields::ICMP_ping_interval_s] = new data_field(data_field::number, "ICMP-ping-inverval-s", "1");
		global[global_fields::ICMP_response_timeout_s] = new data_field(data_field::number, "ICMP-response-timeout-s", "10");
		global[global_fields::va_min_free_memory_enabled] = new data_field(data_field::number, "va-min-free-memory-enabled", "1");
	}
	return *global[field];
}

const data_field & cpu_fields(cpu_fields::_enum field) {
	static data_field** cpu = NULL;
	if (!cpu) {
		cpu = new data_field*[cpu_fields::ENUM_SIZE];
		assert(cpu);
		cpu[cpu_fields::img_path] = new data_field(data_field::string, "os-image", "");
		cpu[cpu_fields::last_img_path] = new data_field(data_field::string, "last-os-image", "");
		cpu[cpu_fields::script_path] = new data_field(data_field::string, "script", "");
		cpu[cpu_fields::daemon_script] = new data_field(data_field::string, "daemon-script", "");
		cpu[cpu_fields::ip] = new data_field(data_field::string, "ip", "");
		cpu[cpu_fields::mask] = new data_field(data_field::string, "mask", "");
		cpu[cpu_fields::gateway] = new data_field(data_field::string, "gateway", "");
		cpu[cpu_fields::host_ip] = new data_field(data_field::string, "host-ip", "");
		cpu[cpu_fields::host_mask] = new data_field(data_field::string, "host-mask", "");
		cpu[cpu_fields::cpu_max_freq_non_turbo] = new data_field(data_field::number, "cpu-max-freq-non-turbo", "0");
		cpu[cpu_fields::bridge_interface] = new data_field(data_field::string, "bridge-interface", "");
		cpu[cpu_fields::node_name] = new data_field(data_field::string, "node-name", "");
		cpu[cpu_fields::nfs_server] = new data_field(data_field::string, "nfs-server", "");
		cpu[cpu_fields::nfs_path] = new data_field(data_field::string, "nfs-path", "");
		cpu[cpu_fields::block_devs] = new data_field(data_field::string, "block-devs", "");
		cpu[cpu_fields::va_free_mem] = new data_field(data_field::string, "va-min-free-memory-enabled-node", "1");
	}
	return *cpu[field];
}

const data_field & blk_dev_fields(blk_dev_fields::_enum field) {
	static data_field** blk_dev = NULL;
	if (!blk_dev) {
		blk_dev = new data_field*[blk_dev_fields::ENUM_SIZE];
		assert(blk_dev);
		blk_dev[blk_dev_fields::mode] = new data_field(data_field::string, "mode", "");
		blk_dev[blk_dev_fields::path] = new data_field(data_field::string, "path", "");
		blk_dev[blk_dev_fields::ramdisk_size_mb] = new data_field(data_field::number, "ramdisk-size-mb", "");
		blk_dev[blk_dev_fields::enabled] = new data_field(data_field::number, "enabled", "");
	}
	return *blk_dev[field];
}

vca_config::vca_config(std::string _filename) : filename(_filename)
{
	for(int i = 0; i < global_fields::ENUM_SIZE; i++)
		fields[i] = ::global_fields((global_fields::_enum)i);

	for (int i = 0; i < MAX_CARDS; i++) {
		for (int j = 0; j < MAX_CPU; j++) {
			for (int k = 0; k < cpu_fields::ENUM_SIZE; k++)
				card[i].cpu[j].fields[k] = ::cpu_fields((cpu_fields::_enum)k);

			for (int l = 0; l < MAX_BLK_DEVS; l++) {
				for (int m = 0; m < blk_dev_fields::ENUM_SIZE; m++)
					card[i].cpu[j].blk[l].fields[m] = ::blk_dev_fields((blk_dev_fields::_enum)m);
			}
		}
	}
}

bool vca_config::try_set_data_field(std::string val, data_field & data)
{
	if (!val.empty()) {
		data.set_value(val.c_str());
		if (data.get_type() == data_field::number) {
			if (!is_unsigned_number(val.c_str())) {
				LOG_ERROR("field %s expects number, not: %s\n", data.get_name(), val.c_str());
			}
		}
	}
	return true;
}

bool vca_config::contains_field(const char *field_name) const
{
	return (is_field_global(field_name) || is_field_cpu(field_name) || is_field_blk(field_name));
}

bool vca_config::is_field_global(const char *field_name) const
{
	for(size_t i = 0; i < global_fields::ENUM_SIZE; i++)
		if(strcmp(field_name, ::global_fields((global_fields::_enum)i).get_name()) == 0)
			return true;
	return false;
}

bool vca_config::is_field_cpu(const char *field_name) const
{
	for(size_t i = 0; i < cpu_fields::ENUM_SIZE; i++)
		if(strcmp(field_name, ::cpu_fields((cpu_fields::_enum)i).get_name()) == 0)
			return true;
	return false;
}

bool vca_config::is_field_blk(const char *field_name) const
{
	for (size_t i = 0; i < blk_dev_fields::ENUM_SIZE; i++)
		if (strcmp(field_name, ::blk_dev_fields((blk_dev_fields::_enum)i).get_name()) == 0)
			return true;
	return false;
}

bool vca_config::load_vca_xml(boost::property_tree::ptree &root)
{
	using namespace boost::property_tree;
	try {
		read_xml(filename, root, xml_parser::trim_whitespace);
	}
	catch (xml_parser_error &ex) {
		LOG_ERROR("Cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	try {
		root = root.get_child(VCA_CONFIG_HEADER);
	}
	catch (ptree_bad_path &ex) {
		LOG_ERROR("%s is not a valid VCA CONFIGURATION FILE!\n", filename.c_str());
		return false;
	}

	if (root.empty()) {
		LOG_ERROR("%s is empty!\n", filename.c_str());
		return false;
	}

	return true;
}

bool vca_config::save_vca_xml(boost::property_tree::ptree &pt)
{
	using namespace boost::property_tree;
	std::string filename_tmp = std::string(VCA_CONFIG_PATH) + ".tmp";
	ptree tree;
	tree.put_child(VCA_CONFIG_HEADER, pt);

	#if defined(WIN32) || (__GNUC__ >= 5) || (BOOST_VERSION >= 105600)
		xml_parser::xml_writer_settings<std::string> w(' ', 2);
	#else
		xml_parser::xml_writer_settings<char> w(' ', 2);
	#endif

	try {
		xml_parser::write_xml(filename_tmp, tree, std::locale(), w);
	}
	catch (xml_parser_error &ex) {
		LOG_ERROR("Cannot write file %s as XML file: %s!\n", filename_tmp.c_str(), ex.what());
		return false;
	}

	auto grp = getgrnam("vcausers");
	if (FAIL == rename(filename_tmp.c_str(), filename.c_str()) ||
	    FAIL == chown(filename.c_str(), (uid_t)-1, grp->gr_gid ) ||
	    FAIL == chmod(filename.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) ) {
		LOG_ERROR("Cannot rename files or change their ownership/permissions - old: %s, new: %s: %s\n",
			filename_tmp.c_str(), filename.c_str(), strerror(errno));
		LOG_WARN("VCA configuration file has not been changed since the last operation.\n");
		return false;
	}

	return true;
}

bool vca_config::save_global_field(const char *field_name, const char *field_value)
{
	using namespace boost::property_tree;

	close_on_exit fd(open(VCA_CONFIG_LOCK_FILE, O_RDWR));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > VCA_CONFIG_LOCK_TIMEOUT) {
			LOG_WARN("Cannot access config.xml - different process locks the file\n");
			return false;
		}

	if (!load_vca_xml(root))
		return false;
	try {
		BOOST_FOREACH(ptree::value_type & v, root.get_child("global")) {
				if (v.first == field_name) {
					v.second.data().clear();
					v.second.data().append(field_value);
					break;
				}
		}
	}
	catch (ptree_bad_path &ex) {
		LOG_ERROR("save_global_field: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	if (!save_vca_xml(root))
		return false;

	return true;
}

bool vca_config::save_cpu_field(unsigned int card_id, unsigned int cpu_id, const char *field_name, const char* field_value)
{
	using namespace boost::property_tree;

	close_on_exit fd(open(VCA_CONFIG_LOCK_FILE, O_RDWR));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > VCA_CONFIG_LOCK_TIMEOUT) {
			LOG_WARN("Cannot access config.xml - different process locks the file\n");
			return false;
		}

	if (!load_vca_xml(root))
		return false;
	try {
		BOOST_FOREACH(ptree::value_type & v, root.get_child("")) {
			if (v.first == "card" && v.second.get<unsigned int>("<xmlattr>.id") == card_id) {
				BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
					if (v.first == "cpu" && v.second.get<unsigned int>("<xmlattr>.id") == cpu_id) {
						BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
							if (v.first == field_name) {
								v.second.data().clear();
								v.second.data().append(field_value);
								break;
							}
						}
					}
				}
			}
		}
	}
	catch (ptree_bad_path &ex) {
		LOG_ERROR("save_cpu_field: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	if (!save_vca_xml(root))
		return false;

	return true;
}

bool vca_config::save_blk_field(unsigned int card_id, unsigned int cpu_id, unsigned int blk_dev_id, const char *field_name, const char* field_value)
{
	using namespace boost::property_tree;


	close_on_exit fd(open(VCA_CONFIG_LOCK_FILE, O_RDWR));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > VCA_CONFIG_LOCK_TIMEOUT) {
			LOG_WARN("Cannot access config.xml - different process locks the file\n");
			return false;
		}

	if (!load_vca_xml(root))
		return false;
	try {
		BOOST_FOREACH(ptree::value_type & v, root.get_child("")) {
			if (v.first == "card" && v.second.get<unsigned int>("<xmlattr>.id") == card_id) {
				BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
					if (v.first == "cpu" && v.second.get<unsigned int>("<xmlattr>.id") == cpu_id) {
						BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
							if (v.first == "block-devs") {
								BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
									if (v.first == get_block_dev_name_from_id(blk_dev_id)) {
										BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
											if (v.first == field_name) {
												v.second.data().clear();
												v.second.data().append(field_value);
												break;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
	catch (ptree_bad_path &ex) {
		LOG_ERROR("save_blk_field: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	if (!save_vca_xml(root))
		return false;

	return true;
}

bool vca_config::add_blk_dev_fields(unsigned int card_id, unsigned int cpu_id, int blk_dev_id)
{
	using namespace boost::property_tree;

	close_on_exit fd(open(VCA_CONFIG_LOCK_FILE, O_RDWR));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > VCA_CONFIG_LOCK_TIMEOUT) {
			LOG_WARN("Cannot access config.xml - different process locks the file\n");
			return false;
		}

	if (!load_vca_xml(root))
		return false;
	try {
		BOOST_FOREACH(ptree::value_type & v, root.get_child("")) {
			if (v.first == "card" && v.second.get<unsigned int>("<xmlattr>.id") == card_id) {
				BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
					if (v.first == "cpu" && v.second.get<unsigned int>("<xmlattr>.id") == cpu_id) {
						ptree &node = v.second;
						BOOST_FOREACH(ptree::value_type & v, v.second.get_child("")) {
							if (v.first == "block-devs") {
								ptree &blk_devs = v.second;
								ptree new_blk_devs;
								bool blk_dev_added = false;
								BOOST_FOREACH(ptree::value_type & v, blk_devs.get_child("")) {
									int id_from_config;
									if (!extract_block_dev_id(v.first.c_str(), id_from_config)) {
										LOG_ERROR("block device name (%s) not valid\n", v.first.c_str());
										return false;
									}
									if (v.first == get_block_dev_name_from_id(blk_dev_id)) {
										LOG_DEBUG("%s already exist for card %d cpu %d\n",
											get_block_dev_name_from_id(blk_dev_id).c_str(), card_id, cpu_id);
										return true;
									}
									else if (id_from_config < blk_dev_id) {
										new_blk_devs.add_child(v.first, v.second);
									}
									else if (id_from_config > blk_dev_id) {
										if (!blk_dev_added) {
											ptree new_blk_dev;
											for (int l = 0; l < blk_dev_fields::ENUM_SIZE; l++) {
												const data_field & field = ::blk_dev_fields((blk_dev_fields::_enum)l);
												new_blk_dev.put(field.get_name(), "");
											}
											new_blk_devs.add_child(get_block_dev_name_from_id(blk_dev_id), new_blk_dev);
											if (parse_blk_devs(card_id, cpu_id, node)) {
												blk_dev_added = true;
											}
											else {
												LOG_ERROR("parsing blk devs failed!\n");
												return false;
											}
										}
										new_blk_devs.add_child(v.first, v.second);
									}
								}
								// in case when only one blk_dev exist
								if (!blk_dev_added) {
									ptree new_blk_dev;
									for (int l = 0; l < blk_dev_fields::ENUM_SIZE; l++) {
										const data_field & field = ::blk_dev_fields((blk_dev_fields::_enum)l);
										new_blk_dev.put(field.get_name(), "");
									}
									new_blk_devs.add_child(get_block_dev_name_from_id(blk_dev_id), new_blk_dev);
									if (!parse_blk_devs(card_id, cpu_id, node)) {
										LOG_ERROR("parsing blk devs failed\n");
										return false;
									}
								}
								blk_devs = new_blk_devs;
							}
						}
					}
				}
			}
		}
	}
	catch (ptree_bad_path &ex) {
		LOG_ERROR("add_blk_dev_fields: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	if (!save_vca_xml(root))
		return false;

	return true;
}

bool vca_config::parse_global(boost::property_tree::ptree node)
{
	try {
		BOOST_FOREACH(boost::property_tree::ptree::value_type const& v, node.get_child("global")) {
			for (int i = 0; i < global_fields::ENUM_SIZE; i++) {
				if (v.first == fields[i].get_name()) {
					if (!try_set_data_field(v.second.data(), fields[i]))
						return false;
					break;
				}
			}
		}
	}
	catch (boost::property_tree::ptree_bad_path &ex) {
		LOG_ERROR("parse_global: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	return true;
}

#define XML_PARSING_ERROR_MSG "invalid entry in configuration file"
enum vca_card_type get_card_type(int card_id);

bool vca_config::parse_card(boost::property_tree::ptree node)
{
	int faked_card_type = VCA_UNKNOWN;
	try {
		BOOST_FOREACH(boost::property_tree::ptree::value_type const& v, node.get_child("")) {
			if (v.first == "card") {
				std::string id = v.second.get<std::string>("<xmlattr>.id");
				if (!is_unsigned_number(id.c_str())) {
					LOG_WARN(XML_PARSING_ERROR_MSG ": incorrect card id: %s, expected unsigned number.\n", id.c_str());
					continue;
				}
				unsigned int card_id = atoi(id.c_str());
				if (card_id >= MAX_CARDS) {
					LOG_WARN(XML_PARSING_ERROR_MSG ": incorrect card id: %d, max value accepted: %d.\n", card_id, MAX_CARDS - 1);
					continue;
				}
				vca_card_type type=get_card_type(card_id);

				if (type == VCA_VCAA) {
					faked_card_type = VCA_FPGA;
				}
				else {
					faked_card_type = type;
				}
				if (v.second.get<std::string>("<xmlattr>.type") != int_to_string(faked_card_type)) {
					continue;
				}
				parse_cpu(card_id, v.second);
			}
		}
	}
	catch (boost::property_tree::ptree_bad_path &ex) {
		LOG_ERROR("parse_card: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}
	return true;
}

bool vca_config::parse_cpu(int card_id, boost::property_tree::ptree node)
{
	using namespace boost::property_tree;
	unsigned int cpu_id;

	try {
		BOOST_FOREACH(ptree::value_type const& v, node.get_child("")) {
			if (v.first == "cpu") {
				std::string id = v.second.get<std::string>("<xmlattr>.id");
				if (!is_unsigned_number(id.c_str())) {
					LOG_WARN("Card: %d - " XML_PARSING_ERROR_MSG ": incorrect cpu id: %s, expected unsigned number.\n", card_id, id.c_str());
					continue;
				}
				cpu_id = atoi(id.c_str());
				if (cpu_id >= MAX_CPU) {
					LOG_WARN("Card: %d - " XML_PARSING_ERROR_MSG ": incorrect cpu id: %d, max value accepted: %d.\n", card_id, cpu_id, MAX_CPU - 1);
					continue;
				}

				cpu_data &cpu = this->card[card_id].cpu[cpu_id];
				ptree subtree = v.second;

				BOOST_FOREACH(ptree::value_type const& v, subtree.get_child("")) {
					for (int i = 0; i < cpu_fields::ENUM_SIZE; i++) {
						if (v.first == get_cpu_field(card_id, cpu_id, cpu_fields::block_devs).get_name()) {
							parse_blk_devs(card_id, cpu_id, subtree);
							break;
						}
						if (v.first == cpu.fields[i].get_name()) {
							if (!try_set_data_field(v.second.data(), cpu.fields[i]))
								return false;
							break;
						}
					}
				}
			}
		}
	}
	catch (boost::property_tree::ptree_bad_path &ex) {
		LOG_ERROR("parse_cpu: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	return true;
}

bool vca_config::parse_blk_devs(int card_id, int cpu_id, boost::property_tree::ptree node)
{
	using namespace boost::property_tree;

	try {
		BOOST_FOREACH(ptree::value_type const& v, node.get_child("block-devs")) {
			int blk_dev_id;
			if (extract_block_dev_id(v.first.c_str(), blk_dev_id)) {

				blk_data &blk_dev = this->card[card_id].cpu[cpu_id].blk[blk_dev_id];
				blk_dev.exist = true;
				ptree subtree = v.second;

				BOOST_FOREACH(ptree::value_type const& v, subtree.get_child("")) {
					for (int i = 0; i < blk_dev_fields::ENUM_SIZE; i++) {
						if (v.first == blk_dev.fields[i].get_name()) {
							try_set_data_field(v.second.data(), blk_dev.fields[i]);
							break;
						}
					}
				}
			} else {
				LOG_WARN("Card: %d Cpu: %d - " XML_PARSING_ERROR_MSG ": incorrect blockio device id: %s.\n", card_id, cpu_id, v.first.c_str());
			}
		}
	}
	catch (boost::property_tree::ptree_bad_path &ex) {
		LOG_ERROR("parse_blk_devs: cannot parse xml file %s: %s!\n", filename.c_str(), ex.what());
		return false;
	}

	return true;
}

bool vca_config::blk_dev_exist(unsigned int card_id, unsigned int cpu_id, unsigned int blk_dev_id) const
{
	// TODO: remove is_block_dev_id_valid() func after removing this one
	return is_block_dev_id_valid(blk_dev_id) &&
	       this->card[card_id].cpu[cpu_id].blk[blk_dev_id].exist;
}

bool vca_config::get_vca_config_from_file()
{
	close_on_exit fd(open(VCA_CONFIG_LOCK_FILE, O_RDWR));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > VCA_CONFIG_LOCK_TIMEOUT) {
			LOG_WARN("Cannot access config.xml - different process locks the file\n");
			return false;
		}

	if (!load_vca_xml(root))
		return false;

	if (!parse_global(root))
		return false;

	if (!parse_card(root))
		return false;

	return true;
}

std::string get_default_script()
{
	return "";
}

static std::string ip_prefix(vca_card_type type) {
	switch( type) {
	case VCA_FPGA_FAB1:
	case VCA_VCAA_FAB1:
		return "172.32.";
	case VCA_VCGA_FAB1:
		return "172.33.";
	default: return "172.31.";
	}
}

static unsigned nodes_on_card(vca_card_type type) {
	switch( type) {
	case VCA_FPGA_FAB1:
	case VCA_VCAA_FAB1:
		return 1;
	case VCA_VCGA_FAB1:
		return 2;
	default: return MAX_CPU;
	}
}

static std::string get_default_cpu_ip(int card_id, int cpu_id, vca_card_type type)
{
	return ip_prefix( type)+ int_to_string(1 + card_id * nodes_on_card(type) + cpu_id) + ".1";
}

std::string get_default_host_mask()
{
	return std::string("24");
}

std::string get_default_mask()
{
	return std::string("24");
}

static std::string get_default_host_ip(int card_id, int cpu_id, vca_card_type type)
{
	return ip_prefix( type)+ int_to_string(1+ card_id * nodes_on_card( type) + cpu_id) + ".254";
}

std::string get_default_bridge_interface()
{
	return std::string("");
}

static std::string get_default_node_name(int card_id, int cpu_id, vca_card_type type)
{
	switch( type){
	case VCA_FPGA_FAB1:
	case VCA_VCAA_FAB1:
		return std::string("vca_node_")+ int_to_string( card_id);
	default: return std::string("vca_node_")+ int_to_string( card_id)+ int_to_string( cpu_id);
	}
}

static std::string get_default_nfs_server(int card_id, int cpu_id, vca_card_type type)
{
	return get_default_host_ip(card_id, cpu_id, type);
}

std::string get_default_nfs_path()
{
	return std::string("/mnt/%s");
}

bool vca_config::save_default_config()
{
	using namespace boost::property_tree;

	ptree root;

	ptree &global = root.add("global", "");
	for (int i = 0; i < global_fields::ENUM_SIZE; i++) {
		const data_field & field = ::global_fields((global_fields::_enum)i);
		global.put(field.get_name(), field.get_cstring());
	}

	static vca_card_type const card_types[]={ VCA_FPGA, VCA_MV_FAB1, VCA_VCAA, VCA_VCGA};
	for(vca_card_type const& card_type:card_types) {
		for (int i = 0; i < MAX_CARDS; i++) {
			ptree card;
			card.put("<xmlattr>.type", card_type);
			for (int j = 0, e= nodes_on_card( card_type); j < e; j++) {
				ptree cpu;
				cpu.put("<xmlattr>.id", j);
				for (int k = 0; k < cpu_fields::ENUM_SIZE; k++) {
					const data_field & field = ::cpu_fields((cpu_fields::_enum)k);
					std::string value;
					if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::script_path).get_name()) == 0)
						value = get_default_script();
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::ip).get_name()) == 0)
						value = get_default_cpu_ip(i, j, card_type);
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::mask).get_name()) == 0)
						value = get_default_mask();
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::gateway).get_name()) == 0)
						value = get_default_host_ip(i, j, card_type);
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::host_ip).get_name()) == 0)
						value = get_default_host_ip(i, j, card_type);
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::host_mask).get_name()) == 0)
						value = get_default_host_mask();
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::bridge_interface).get_name()) == 0)
						value = get_default_bridge_interface();
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::node_name).get_name()) == 0)
						value = get_default_node_name(i, j, card_type);
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::nfs_server).get_name()) == 0)
						value = get_default_nfs_server(i, j, card_type);
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::nfs_path).get_name()) == 0)
						value = get_default_nfs_path();
					else if (strcmp(field.get_name(), ::cpu_fields(cpu_fields::block_devs).get_name()) == 0) {
						ptree &block_devs = cpu.add("block-devs", "");
						ptree blk_dev;

						for (int l = 0; l < blk_dev_fields::ENUM_SIZE; l++) {
							const data_field & field = ::blk_dev_fields((blk_dev_fields::_enum)l);
							blk_dev.put(field.get_name(), value.c_str());
						}
						block_devs.add_child(get_block_dev_name_from_id(0), blk_dev);
						break;
					}
					else
						value = field.get_cstring();

					cpu.put(field.get_name(), value.c_str());
				}
				card.add_child("cpu", cpu);
			}
			card.put("<xmlattr>.id", i);
			root.add_child("card", card);
		}
	}
	root.put("<xmlattr>.version", VCA_CONFIG_VER_STRING);

	close_on_exit fd(open(VCA_CONFIG_LOCK_FILE, O_RDWR));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > VCA_CONFIG_LOCK_TIMEOUT) {
			LOG_WARN("Cannot access config.xml - different process locks the file\n");
			return false;
		}

	if (!save_vca_xml(root))
		return false;

	return true;
}

struct vca_config *new_vca_config(const char *filename)
{
	return new vca_config(std::string(filename));
}

void delete_vca_config(struct vca_config * _vca_config)
{
	delete _vca_config;
}

bool vca_config_get_config_from_file(struct vca_config *config)
{
	return config->get_vca_config_from_file();
}

const char *vca_config_get_host_ip(struct vca_config *config, int card_id, int cpu_id)
{
	return config->get_cpu_field(card_id, cpu_id, cpu_fields::host_ip).get_cstring();
}

const char *vca_config_get_host_mask(struct vca_config *config, int card_id, int cpu_id)
{
	return config->get_cpu_field(card_id, cpu_id, cpu_fields::host_mask).get_cstring();
}

bool vca_config_is_auto_boot(struct vca_config *config)
{
	return config->get_global_field(global_fields::auto_boot).get_number() == 1 ? true : false;
}

bool vca_config_is_va_min_free_memory_enabled(struct vca_config *config)
{
	return config->get_global_field(global_fields::va_min_free_memory_enabled).get_number() == 1 ? true : false;
}
