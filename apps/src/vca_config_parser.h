/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2015-2017 Intel Corporation.
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

#ifndef _VCA_CONFIG_PARSER_H_
#define _VCA_CONFIG_PARSER_H_

#ifdef __cplusplus

#include <string>
#include <assert.h>

#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "vca_defs.h"
#include "vca_blockio_ctl.h"

#define VCA_CONFIG_HEADER	"VCA_CONFIGURATION"
#define VCA_CONFIG_LOCK_TIMEOUT 3
#define VCA_CONFIG_LOCK_FILE VCA_CONFIG_DIR "/config.lock"

#define VCA_CONFIG_VER_MAJOR	1
#define VCA_CONFIG_VER_MINOR	5
#define VCA_CONFIG_VER_NUMBER	2
#define VCA_CONFIG_VER_STRING (int_to_string(VCA_CONFIG_VER_MAJOR) + "." + \
			       int_to_string(VCA_CONFIG_VER_MINOR) + "." + \
			       int_to_string(VCA_CONFIG_VER_NUMBER))

namespace vca_config_parser {

struct global_fields {
	enum _enum {
		auto_boot = 0,
		debug_enabled,
		link_up_timeout_ms,
		handshake_irq_timeout_ms,
		alloc_timeout_ms,
		cmd_timeout_ms,
		mac_write_timeout_ms,
		default_daemon_script,
		wait_cmd_timeout_s,
		wait_bios_cmd_timeout_s,
		wait_bios_cmd_flashing_s,
		ICMP_ping_interval_s,
		ICMP_response_timeout_s,
		va_min_free_memory_enabled,
		ENUM_SIZE
	};
};

struct cpu_fields {
	enum _enum {
		img_path,
		last_img_path,
		script_path,
		daemon_script,
		ip,
		mask,
		gateway,
		host_ip,
		host_mask,
		cpu_max_freq_non_turbo,
		bridge_interface,
		node_name,
		nfs_server,
		nfs_path,
		block_devs,
		va_free_mem,
		ENUM_SIZE
	};
};

struct blk_dev_fields {
	enum _enum {
		mode,
		path,
		ramdisk_size_mb,
		enabled,
		ENUM_SIZE
	};
};

struct data_field {
	enum _type{
		number = 0,
		string
	};
private:
	_type type;
	const char* name;
	std::string data;
public:
	data_field(): type(string), name("unknown"), data(""){}
	data_field(_type type, const char * name, const char* value): type(type), name(name), data(value) {}
	unsigned int get_number() const { assert(type == number); return atoi(data.c_str()); }
	const char* get_cstring() const { return data.c_str(); }
	std::string get_string() const { return data; }
	void set_value(const char * value) { data = value; }
	const char* get_name() const { return name; }
	data_field::_type get_type() const { return type; }
};

struct vca_config {
private:
	std::string filename;
	boost::property_tree::ptree root;
	bool load_vca_xml(boost::property_tree::ptree &root);
	bool save_vca_xml(boost::property_tree::ptree &root);
	bool parse_global(boost::property_tree::ptree root);
	bool parse_card(boost::property_tree::ptree root);
	bool parse_cpu(int card_id, boost::property_tree::ptree node);
	bool parse_blk_devs(int card_id, int cpu_id, boost::property_tree::ptree node);
	bool try_set_data_field(std::string val, data_field & data);

	struct blk_data {
		data_field fields[blk_dev_fields::ENUM_SIZE];
		bool exist;
	};

	struct cpu_data {
		data_field fields[cpu_fields::ENUM_SIZE];
		blk_data blk[MAX_BLK_DEVS];
	};

	struct card_data {
		cpu_data cpu[MAX_CPU];
	};

	data_field fields[global_fields::ENUM_SIZE];
	card_data card[MAX_CARDS];
public:
	bool get_vca_config_from_file();
	bool save_default_config();
	bool save_global_field(const char *field_name, const char *field_value);
	bool save_cpu_field(unsigned int card_id, unsigned int cpu_id, const char *field_name, const char *field_value);
	bool save_blk_field(unsigned int card_id, unsigned int cpu_id, unsigned int blk_dev_id,
		const char *field_name, const char *field_value);
	bool add_blk_dev_fields(unsigned int card_id, unsigned int cpu_id, int blk_dev_id);
	bool contains_field(const char *field_name) const;
	bool is_field_global(const char *field_name) const;
	bool is_field_cpu(const char *field_name) const;
	bool is_field_blk(const char *field_name) const;
	bool blk_dev_exist(unsigned int card_id, unsigned int cpu_id, unsigned int blk_dev_id) const;

	vca_config(std::string _filename);
public:
	const data_field & get_global_field(global_fields::_enum field) const {
		return fields[field];
	}
	const data_field *get_global_field(const char * field_name) const {
		for(int i = 0; i < global_fields::ENUM_SIZE; i++)
			if(!strcmp(fields[i].get_name(), field_name))
				return &fields[i];
		return NULL;
	}
	const data_field & get_cpu_field(int card_id, int cpu_id, cpu_fields::_enum field) const {
		assert(card_id < MAX_CARDS);
		assert(cpu_id < MAX_CPU);
		return card[card_id].cpu[cpu_id].fields[field];
	}
	const data_field *get_cpu_field(int card_id, int cpu_id, const char * field_name) const {
		assert(card_id < MAX_CARDS);
		assert(cpu_id < MAX_CPU);
		for(int i = 0; i < cpu_fields::ENUM_SIZE; i++)
			if(!strcmp(card[card_id].cpu[cpu_id].fields[i].get_name(), field_name))
				return &card[card_id].cpu[cpu_id].fields[i];
		return NULL;
	}
	const data_field & get_blk_field(int card_id, int cpu_id, int blk_dev_id, blk_dev_fields::_enum field) const {
		assert(card_id < MAX_CARDS);
		assert(cpu_id < MAX_CPU);
		assert(blk_dev_id < MAX_BLK_DEVS);
		return card[card_id].cpu[cpu_id].blk[blk_dev_id].fields[field];
	}
	const data_field *get_blk_field(int card_id, int cpu_id, int blk_dev_id, const char * field_name) const {
		assert(card_id < MAX_CARDS);
		assert(cpu_id < MAX_CPU);
		assert(blk_dev_id < MAX_BLK_DEVS);
		for (int i = 0; i < blk_dev_fields::ENUM_SIZE; i++)
			if (!strcmp(card[card_id].cpu[cpu_id].blk[blk_dev_id].fields[i].get_name(), field_name))
				return &card[card_id].cpu[cpu_id].blk[blk_dev_id].fields[i];
		return NULL;
	}
};

} /* end of namespace vca_config_parser */

extern "C" {
	using namespace vca_config_parser;
#else
struct vca_config {};
#endif

struct vca_config * new_vca_config(const char *filename);
void delete_vca_config(struct vca_config * _vca_config);
bool vca_config_get_config_from_file(struct vca_config * config);
const char * vca_config_get_host_ip(struct vca_config * config, int card_id, int cpu_id);
const char * vca_config_get_host_mask(struct vca_config * config, int card_id, int cpu_id);
bool vca_config_is_auto_boot(struct vca_config * config);
bool vca_config_is_va_min_free_memory_enabled(struct vca_config *config);
#ifdef __cplusplus
}
#endif
#endif
