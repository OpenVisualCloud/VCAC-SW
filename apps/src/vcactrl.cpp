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

#include "helper_funcs.h"

#include <sys/file.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#include <boost/thread/thread.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <vca_mgr_ioctl.h>
#include <vca_mgr_extd_ioctl.h>
#include <vca_csm_ioctl.h>

#include "vca_defs.h"
#include "vcactrl.h"
#include "vca_config_parser.h"
#include "vca_eeprom_update.h"
#include "vca_blockio_ctl.h"
#include "vca_devices.h"
#include "log_args.h"
#include "version.h"

#include <sys/mman.h>

#define VCASYSFSDIR						"/sys/class/vca"
#define LINK_DOWN_STATE					"link_down"
#define LINK_DOWN_STATE_TRIES			60
#define LINK_DOWN_RESET_TRIES			3
#define WAIT_CMD_WA_ATTEMPTS			10	// this is a WA for states which requires a little bit longer to return with error
#define POWER_BUTTON_TIMEOUT			10
#define NODE_POWERDOWN_TIMEOUT			5000
#define GOLD_BIOS_LOAD_TIMEOUT			200000
#define MIN_MEM_FREE_OF_CACHE_CARD_SIDE "524288"

#define HANDSHAKE_RESET_TRIES		3
#define AFTER_HANDSHAKE_RESET_TRIES	3

#define BOARD_ID_INVALID 0xff

#define LINK_DOWN_WORKAROUND

#define SN_MAX 30

using namespace Printer;

/* global error from vcactrl */
uint8_t command_err = 0;

static void print_help()
{
	execlp( "man", "man", "vcactl", NULL); // source help in vcactl.1 file
}

extern "C" void common_log(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf( stderr, format, args);
	va_end(args);
}

const char * get_vca_lbp_param_str(vca_lbp_param val)
{
	switch (val) {
	case VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS:
		return "VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS";
	case VCA_LBP_PARAM_i7_ALLOC_TIMEOUT_MS:
		return "VCA_LBP_PARAM_i7_ALLOC_TIMEOUT_MS";
	case VCA_LBP_PARAM_i7_CMD_TIMEOUT_MS:
		return "VCA_LBP_PARAM_i7_CMD_TIMEOUT_MS";
	case VCA_LBP_PARAM_i7_MAC_WRITE_TIMEOUT_MS:
		return "VCA_LBP_PARAM_i7_MAC_WRITE_TIMEOUT_MS";
	case VCA_LBP_PARAM_SIZE:
	default:
		LOG_ERROR("value not in vca_lbp_retval enum: %d\n", (int)val);
		return "NOT_VALID vca_lbp_param";
	}
}

const char *get_vca_lbp_retval_str(vca_lbp_retval val)
{
	switch (val) {
	case LBP_STATE_OK:
		return "LBP_STATE_OK";
	case LBP_SPAD_i7_WRONG_STATE:
		return "LBP_SPAD_i7_WRONG_STATE";
	case LBP_IRQ_TIMEOUT:
		return "LBP_IRQ_TIMEOUT";
	case LBP_ALLOC_TIMEOUT:
		return "LBP_ALLOC_TIMEOUT";
	case LBP_CMD_TIMEOUT:
		return "LBP_CMD_TIMEOUT";
	case LBP_BAD_PARAMETER_VALUE:
		return "LBP_BAD_PARAMETER_VALUE";
	case LBP_UNKNOWN_PARAMETER:
		return "LBP_UNKNOWN_PARAMETER";
	case LBP_INTERNAL_ERROR:
		return "LBP_INTERNAL_ERROR";
	case LBP_PROTOCOL_VERSION_MISMATCH:
		return "LBP_PROTOCOL_VERSION_MISMATCH";
	case LBP_WAIT_INTERRUPTED:
		return "LBP_WAIT_INTERRUPTED";
	case LBP_SIZE:
	default:
		LOG_ERROR("value not in vca_lbp_retval enum: %d\n", (int)val);
		return "NOT_VALID vca_lbp_retval";
	}
}

const char *get_plx_eep_retval_str(plx_eep_retval val)
{
	switch (val) {
	case PLX_EEP_STATUS_OK:
		return "PLX_EEP_STATUS_OK";
	case PLX_EEP_INTERNAL_ERROR:
		return "PLX_EEP_INTERNAL_ERROR";
	case PLX_EEP_TIMEOUT:
		return "PLX_EEP_TIMEOUT";
	case PLX_EEP_SIZE:
	default:
		LOG_ERROR("value not in plx_eep_retval enum: %d\n", (int)val);
		return "NOT_VALID plx_eep_retval";
	}
}

using namespace vca_config_parser;

class subcmds {
public:
	struct cmp_string {
	public:
		bool operator()(const char* lhs, const char* rhs) const {
			return strcmp(lhs, rhs) < 0;
		}
	};
	typedef std::map<const char*, const char*, cmp_string> subcmd;
	typedef std::map<const char*, const char*>::const_iterator iter;
public:
	subcmd _subcmds;

	subcmds() {}
	subcmds(subcmd subcmds) : _subcmds(subcmds) {}
	subcmds(const subcmds&);
	subcmds & operator=(const subcmds&);

	void add_subcmd(const char* name, const char* desc) {
		_subcmds.insert(std::pair<const char*, const char*>(name, desc));
	}
	~subcmds() {
		for (subcmd::iterator it = _subcmds.begin(); it != _subcmds.end();) {
			delete it->second;
			_subcmds.erase(it++);
		}
	}
};

class thread_manager {
	std::vector<boost::thread*> threads;
public:
	void create_thread(void *function(void*), void* data) {
		threads.push_back(new boost::thread(function, data));
	}
	~thread_manager() {
		for (size_t i = 0; i < threads.size(); i++)
			threads[i]->join();

		for(size_t i = 0; i < threads.size(); i++)
			delete threads[i];

		threads.clear();
	}
};

vca_config config(VCA_CONFIG_PATH);

bool load_vca_config()
{
	try {
		if (!config.get_vca_config_from_file()) {
			LOG_ERROR("could not parse vca configuration file!\n");
			return false;
		}
	}
	catch (std::exception &ex) {
		LOG_ERROR("Exception loading vca configuration file: %s", ex.what());
		return false;
	}

	return true;
}

class args_holder {
private:
	std::vector<const data_field*> _holder;
	const char *_cmd_name;
	std::map<std::string,bool> execution_flags;
public:
	args_holder() : _cmd_name("") {
		execution_flags["--force"] = false;
		execution_flags["--skip-modprobe-check"] = false;
		execution_flags["--skip-card-type-check"] = false;
	}
	bool is_force_cmd_enabled() { return execution_flags["--force"]; }
	bool is_modprobe_check_skipped() { return execution_flags["--skip-modprobe-check"]; }
	bool needs_card_type_check() { return !execution_flags["--skip-card-type-check"]; }

	void add_arg(const char *name, const char *value) {
		data_field *df = new data_field(data_field::string, name, value);
		_holder.push_back(df);
	}
	void add_arg(const char *name, unsigned int value) {
		data_field *df = new data_field(data_field::number, name, int_to_string(value).c_str());
		_holder.push_back(df);
	}
	std::string get_arg(const char *name) {
		for (auto &holder : _holder)
			if (strcmp(holder->get_name(), name) == 0){
				return holder->get_string();
			}
		return "";
	}
	std::vector<std::string> get_args(const char *name) {
		std::vector<std::string> results;
		for (auto &holder : _holder)
			if (strcmp(holder->get_name(), name) == 0) {
				results.push_back(holder->get_string());
			}
		return results;
	}
	void set_cmd_name(const char *cmd_name) {
		_cmd_name = cmd_name;
	}
	const char *get_cmd_name() {
		return _cmd_name;
	}
	bool process_execution_flag(const char *arg) {
		if (strcmp(arg, "-v") == 0) {
			verbose = DEBUG_INFO;
		} else if (strcmp(arg, "-vv") == 0) {
			verbose = FULL_INFO;
		} else if (strncmp(arg, "--", 2) == 0) {
			auto it = execution_flags.find(arg);
			if (it == execution_flags.end()) {
				LOG_ERROR("unknown flag: %s\n", arg);
				return false;
			}
			it->second = true;
		} else {
			return false;
		}
		return true;
	}
	int size() const {
		return _holder.size();
	}
	const data_field* operator()(const char* name) const {
		for (std::vector<const data_field*>::const_iterator it = _holder.begin(); it != _holder.end(); it++)
			if (strcmp((*it)->get_name(), name) == 0)
				return *it;
		return NULL;
	}
	~args_holder() {
		for (size_t i = 0; i < _holder.size(); i++)
			delete _holder[i];
	}
};

typedef int(*parse_arg_func)(const char * arg, args_holder & holder);

enum parsing_output {
	PARSED = 0,
	NOT_PARSED,
	PARSING_FAILED,
	PARSED_AND_CONTINUE
};

struct caller_data {
	const int card_id;
	int cpu_id;
	int caller_id;	/* to identify the number of call, usually only need
			to check if it is the first call( caller_id == 0 )*/
	args_holder &args;
	caller_data(int card_id, int cpu_id, args_holder &holder)
		:card_id(card_id), cpu_id(cpu_id), caller_id(0), args(holder){}
	caller_data(int card_id, args_holder &holder)
		:card_id(card_id), cpu_id(0), caller_id(0), args(holder) {}

	const char *get_cmd_name() {
		return args.get_cmd_name();
	}
	void LOG_CPU(e_verbose verbose_level, const char *format, ...) const {
		if(verbose_level <= verbose) {
			char buffer[BUFFER_SIZE];
			va_list _args;
			va_start(_args, format);
			vsnprintf(buffer, sizeof(buffer), format, _args);
			va_end(_args);
			LOG(verbose_level, "Card: %d Cpu: %d - %s", card_id, cpu_id, buffer);
		}
	}
	void LOG_CPU_WARN(const char * format, ...) const {
		char buffer[BUFFER_SIZE];
		va_list _args;
		va_start(_args, format);
		vsnprintf(buffer, sizeof(buffer), format, _args);
		va_end(_args);
		LOG_WARN("Card: %d Cpu: %d - %s", card_id, cpu_id, buffer);
	}
	void LOG_CPU_ERROR(const char * format, ...) const {
		char buffer[BUFFER_SIZE];
		va_list _args;
		va_start(_args, format);
		vsnprintf(buffer, sizeof(buffer), format, _args);
		va_end(_args);
		LOG_ERROR("Card: %d Cpu: %d - %s", card_id, cpu_id, buffer);
	}
};

typedef bool (*function)(caller_data data);

class function_caller {
protected:
	function f;
	unsigned int calls_count;
public:
	function_caller(function f): f(f), calls_count(0) {}
	virtual void call(caller_data data) = 0;
	virtual ~function_caller(){}
};

class sequential_caller: public function_caller {
public:
	sequential_caller(function f): function_caller(f) {}
	void call(caller_data data) {
		try {
			data.caller_id = calls_count++;
			bool success = f(data);
			if (!success)
				command_err = EAGAIN;
		}
		catch (boost::interprocess::interprocess_exception &ex) {
			data.LOG_CPU_ERROR("Exception (interprocess_exception) encountered while executing "
				"%s command: %s\n", data.get_cmd_name(), ex.what());
			command_err = EAGAIN;
		}
		catch (std::exception &ex) {
			data.LOG_CPU_ERROR("Exception encountered while executing "
				"%s command: %s\n", data.get_cmd_name(), ex.what());
			command_err = EAGAIN;
		}
		catch(...) {
			data.LOG_CPU_ERROR("Exception encountered while executing "
				"%s command\n", data.get_cmd_name());
			command_err = EAGAIN;
		}
	}
};

class threaded_caller: public function_caller {
private:
	struct thread_data {
		function f;
		caller_data data;
		thread_data(function f, caller_data data):
			f(f), data(data){}
	};
	static void *thread_func(void *arg) {
		thread_data *d = (thread_data*)arg;
		try {
			bool success = d->f(d->data);
			if (!success)
				command_err = EAGAIN;
			delete d;
		}
		catch (boost::interprocess::interprocess_exception &ex) {
			d->data.LOG_CPU_ERROR("Exception (interprocess_exception) encountered while executing "
				"%s command: %s\n", d->data.get_cmd_name(), ex.what());
			command_err = EAGAIN;
		}
		catch (std::exception &ex) {
			d->data.LOG_CPU_ERROR("Exception encountered while executing "
				"%s command: %s\n", d->data.get_cmd_name(), ex.what());
			command_err = EAGAIN;
		}
		return NULL;
	}
	static thread_manager * _thread_mgr;
public:
	static void set_thread_manager(thread_manager * tm) {
		_thread_mgr = tm;
	}
	threaded_caller(function f): function_caller(f) {}
	void call(caller_data data) {
		if (_thread_mgr) {
			data.caller_id = calls_count++;
			_thread_mgr->create_thread(thread_func, new thread_data(f, data));
		}

		else
			data.LOG_CPU_ERROR("Thread manager not set for threaded_caller!\n");
	}
};

thread_manager *threaded_caller::_thread_mgr = NULL;

static int get_last_cpu(unsigned card);

static bool validate_node_range(caller_data &d)
{
	if (0 <= d.card_id && d.card_id < MAX_CARDS)
		if (0<= d.cpu_id && d.cpu_id <=get_last_cpu(d.card_id))
			return true;
		else LOG_ERROR("Wrong cpu id! (got %d, expected from 0 to %d)\n", d.cpu_id, get_last_cpu(d.card_id));
	else LOG_ERROR("Wrong card id! (got %d, expected from 0 to %d)\n", d.card_id, MAX_CARDS - 1);
	return false;
}

static std::string get_node_lock_file_name(caller_data &d)
{
	return std::string(VCACTL_NODE_LOCK_PATH) + int_to_string(d.card_id) + int_to_string(d.cpu_id);
}


struct cmd_desc;
typedef parsing_output(*arg_parser)(const char *arg, args_holder &holder);
std::string get_card_gen(const unsigned card_id);

struct cmd_desc {
	const char * name;
	function_caller * caller;
	arg_parser args[6];
	subcmds *subcmd;
	size_t args_size;

	cmd_desc(const char* name, function_caller * caller,
		arg_parser arg0 = NULL,
		arg_parser arg1 = NULL,
		arg_parser arg2 = NULL,
		arg_parser arg3 = NULL,
		arg_parser arg4 = NULL,
		arg_parser arg5 = NULL) :
		name(name), caller(caller), subcmd(NULL), args_size(0) {
		if (arg0)
			args[args_size++] = arg0;
		else return;
		if (arg1)
			args[args_size++] = arg1;
		else return;
		if (arg2)
			args[args_size++] = arg2;
		else return;
		if (arg3)
			args[args_size++] = arg3;
		else return;
		if (arg4)
			args[args_size++] = arg4;
		else return;
		if (arg5)
			args[args_size++] = arg5;
	}
	cmd_desc(const char* name, function_caller * caller, subcmds *sub,
		arg_parser arg0 = NULL,
		arg_parser arg1 = NULL,
		arg_parser arg2 = NULL,
		arg_parser arg3 = NULL,
		arg_parser arg4 = NULL,
		arg_parser arg5 = NULL) :
		name(name), caller(caller), subcmd(sub), args_size(0) {
		if (arg0)
			args[args_size++] = arg0;
		else return;
		if (arg1)
			args[args_size++] = arg1;
		else return;
		if (arg2)
			args[args_size++] = arg2;
		else return;
		if (arg3)
			args[args_size++] = arg3;
		else return;
		if (arg4)
			args[args_size++] = arg4;
		else return;
		if (arg5)
			args[args_size++] = arg5;
	}
	bool parse_args(char *argv[], args_holder &holder) const {
		for (const arg_parser* parser = args, *const end = args + args_size; parser < end; ++parser)
			switch((*parser)(*argv, holder)) {
			case PARSING_FAILED:
				LOG_FULL("%s\tPARSING_FAILED by %p\n", (*argv)?:"", parser);
				return false;
			case PARSED:
				LOG_FULL("%s\tPARSED by %p\n", (*argv)?:"", parser);
				if (*argv) ++argv; // Stay in NULL, because probably can be next parser with optional values
				continue;
			case NOT_PARSED:
				LOG_FULL("%s\tNOT_PARSED by %p\n", (*argv)?:"", parser);
				continue;
			case PARSED_AND_CONTINUE:
				LOG_FULL("%s\tPARSED by %p (greedy)\n", (*argv)?:"", parser);
				if (*(argv+1))
					--parser; // Decrement parser, so it will be called again in next loop iteration
				++argv;
				continue;
			}
		if (!*argv) {
			LOG_FULL("Parsed\n");
			return true;
		}
		LOG_ERROR("Unrecognized parameter %s\n", *argv);
		LOG_INFO("use: vcactl help\n");
		return false;
	}
};

bool try_link_up(const caller_data & d, int n, bool need_reset);
std::string get_cpu_os(caller_data d);
std::string get_bios_version(caller_data d, close_on_exit& cpu_fd);
bool is_mv_bios_version_correct(caller_data d);
bool is_os_windows(std::string os);

std::string get_vca_sysfs_dir(const caller_data &d)
{
	return std::string(VCASYSFSDIR) + "/vca" + int_to_string(d.card_id) + int_to_string(d.cpu_id) + "/";
}


static void* read_cpu_sysfs(const caller_data &d, char const *entry, void*(proc)(const caller_data&,char const*,unsigned)) {
	std::string filename= get_vca_sysfs_dir( d) + entry;
	if( close_on_exit fd= open_path( filename.c_str(), O_RDONLY)) {
		char value[ PAGE_SIZE+ 2];
		int const r= read( fd, value,sizeof( value)- 1);
		assert( r< (int)(sizeof( value)- 1));
		if( 0<= r) {
			value[ r]= 0;
			return proc( d, value, r);
		}
		d.LOG_CPU_WARN( "Cannot read %s\n", filename.c_str());
	}
	return NULL;
}


std::string read_cpu_sysfs(const caller_data & d, const char *entry)
{
	std::string ret;
	std::string filename = get_vca_sysfs_dir(d) + entry;
	char value[PAGE_SIZE];

	close_on_exit fd(open_path(filename.c_str(), O_RDONLY));
	if (!fd)
		return ret;

	int len = read(fd, value, sizeof(value) -1);
	if (len < 0) {
		d.LOG_CPU_ERROR("Failed to read sysfs entry %s\n",
			filename.c_str());
		return ret;
	}
	if (len == 0)
		return ret;

	if (value[len - 1] == '\n')
		value[len - 1] = '\0';
	else
		value[len] = '\0';

	ret = value;
	return ret;
}

inline bool check_plx_eep_state_ok(plx_eep_retval state, unsigned long ioctl_cmd)
{
	if (state != PLX_EEP_STATUS_OK) {
		LOG_ERROR("EEP %s returned with %s\n",
			get_vca_ioctl_name(ioctl_cmd), get_plx_eep_retval_str(state));
		return false;
	}
	return true;
}

int vca_plx_eep_ioctl_with_bin(filehandle_t fd, char *img, size_t bin_size)
{
	int ret = SUCCESS;
	const unsigned long ioctl_cmd = VCA_UPDATE_EEPROM;
	vca_eeprom_desc *desc = (vca_eeprom_desc *)malloc(sizeof(*desc) + bin_size);
	if (!desc)
		return -ENOMEM;

	desc->ret = PLX_EEP_INTERNAL_ERROR;
	desc->buf_size = bin_size;
	memcpy(desc->buf, img, bin_size);

	if (!vca_ioctl(fd, ioctl_cmd, desc)) {
		ret = -EPERM;
	} else {
		if (!check_plx_eep_state_ok(desc->ret, ioctl_cmd))
			ret = FAIL;
	}

	free(desc);
	return ret;
}

int vca_plx_eep_ioctl_with_bin_mgr_extd(filehandle_t fd, char *img, size_t bin_size)
{
	int ret = SUCCESS;
	const unsigned long ioctl_cmd = VCA_UPDATE_SECONDARY_EEPROM;
	vca_secondary_eeprom_desc *desc =
			(vca_secondary_eeprom_desc *)malloc(sizeof(*desc) + bin_size);
	if (!desc)
		return -ENOMEM;

	desc->ret = PLX_EEP_INTERNAL_ERROR;
	desc->buf_size = bin_size;
	memcpy(desc->buf, img, bin_size);

	if (!vca_ioctl(fd, ioctl_cmd, desc)) {
		ret = -EPERM;
	} else {
		if (!check_plx_eep_state_ok(desc->ret, ioctl_cmd))
			ret = FAIL;
	}

	free(desc);
	return ret;
}

static int get_last_cpu(unsigned card)
{
	if(close_on_exit card_fd= open_card_fd( card)){
		unsigned cpu_num;
		if( vca_ioctl(card_fd, VCA_READ_CPU_NUM, &cpu_num))
			return ((cpu_num<MAX_CPU)?cpu_num:MAX_CPU)-1;
	}
	return -1;
}

std::string get_modules_version(int card_id)
{
	close_on_exit card_fd(open_card_fd(card_id));
	if (!card_fd)
		return "Build unknown (err: open)";
	vca_ioctl_buffer build_info;
	if (!vca_ioctl(card_fd, VCA_READ_MODULES_BUILD, build_info.buf))
		return "Build unknown (err: ioctl)";
	build_info.buf[sizeof(build_info.buf) - 1] = 0;
	return build_info.buf;
}

std::string get_kernel_version()
{
	char buf[256];
	run_cmd_with_output("uname -r", buf, sizeof(buf));
	return buf;
}

int get_board_id(int card_id)
{
	close_on_exit fd_extd(open_extd_card_fd(card_id));
	 if (!fd_extd) {
		LOG_ERROR("Could not open card_id: %d file descriptor!\n", card_id);
		return BOARD_ID_INVALID;
	}
	int boardId = 0;
	if (!vca_ioctl(fd_extd, VCA_READ_BOARD_ID, &boardId))
		return BOARD_ID_INVALID;
	return boardId;
}

const char* get_csm_ioctl_name(unsigned long ioctl_cmd)
{
	switch(ioctl_cmd) {
	case LBP_HANDSHAKE:
		return "LBP_HANDSHAKE";
	case LBP_BOOT_RAMDISK:
		return "LBP_BOOT_RAMDISK";
	case LBP_BOOT_FROM_USB:
		return "LBP_BOOT_FROM_USB";
	case LBP_FLASH_BIOS:
		return "LBP_FLASH_BIOS";
	case LBP_FLASH_FIRMWARE:
		return "LBP_FLASH_FIRMWARE";
	case LBP_SET_PARAMETER:
		return "LBP_SET_PARAMETER";
	case CSM_START:
		return "CSM_START";
	case CSM_STOP:
		return "CSM_STOP";
	case LBP_GET_MAC_ADDR:
		return "LBP_GET_MAC_ADDR";
	case LBP_UPDATE_MAC_ADDR:
		return "LBP_UPDATE_MAC_ADDR";
	case LBP_SET_SERIAL_NR:
		return "LBP_SET_SERIAL_NR";
	case LBP_SET_TIME:
		return "LBP_SET_TIME";
	case LBP_GET_BIOS_PARAM:
		return "LBP_GET_BIOS_PARAM";
	case LBP_SET_BIOS_PARAM:
		return "LBP_SET_BIOS_PARAM";
	case VCA_AGENT_COMMAND:
		return "VCA_AGENT_COMMAND";
	case LBP_CLEAR_SMB_EVENT_LOG:
		return "LBP_CLEAR_SMB_EVENT_LOG";
	case VCA_READ_EEPROM_CRC:
		return "VCA_READ_EEPROM_CRC";
	case LBP_BOOT_BLKDISK:
		return "LBP_BOOT_LBPDISK";
	case VCA_GET_MEM_INFO:
		return "VCA_GET_MEM_INFO";
	default:
		LOG_DEBUG("csm ioctl command name for %lx not found!\n", ioctl_cmd);
		return "";
	};
}

bool csm_ioctl(filehandle_t fd, unsigned long ioctl_cmd, void* arg)
{
	if(ioctl(fd, ioctl_cmd, arg)) {
		LOG_ERROR("%s failed!\n", get_csm_ioctl_name(ioctl_cmd));
		return false;
	}
	return true;
}

bool is_link_up(const caller_data & d)
{
	return read_cpu_sysfs(d, "link_state") == "up";
}


bool is_bios_up_or_ready(const caller_data & d)
{
	if (read_cpu_sysfs(d, "state") == "bios_up" ||
	    read_cpu_sysfs(d, "state") == "bios_ready")
		return true;
	else
		return false;
}

bool check_for_link_up(const caller_data & d, unsigned int timeout)
{
	boost::posix_time::ptime start = get_time();

	while(get_passed_time_ms(start) < timeout) {
		if (is_link_up(d))
			return true;
		sleep(1);
	}
	return false;
}

bool start_csm(filehandle_t cpu_fd, const caller_data & d)
{
	d.LOG_CPU(DEBUG_INFO, "Starting csm!\n");
	int ret;
	if(!csm_ioctl(cpu_fd, CSM_START, &ret))
		return false;
	if(ret) {
		if(ret == -EEXIST)
			return true;

		d.LOG_CPU_ERROR("Could not start CSM: %d\n", ret);
		return false;
	}
	return true;
}

bool stop_csm(filehandle_t cpu_fd, const caller_data & d)
{
	d.LOG_CPU(DEBUG_INFO, "Stopping CSM!\n");
	if (!csm_ioctl(cpu_fd, CSM_STOP, NULL))
		return false;
	return true;
}

bool get_mac_addr(filehandle_t cpu_fd, const caller_data & d, unsigned char mac_addr[6])
{
	d.LOG_CPU(DEBUG_INFO, "GETTING MAC ADDRESS!\n");
	vca_csm_ioctl_mac_addr_desc desc;
	if(!csm_ioctl(cpu_fd, LBP_GET_MAC_ADDR, &desc))
		return false;
	int mac_size = 6;
	memcpy((void*)mac_addr, (void*)desc.mac_addr, mac_size);
	return true;
}

inline bool check_lbp_state_ok(vca_lbp_retval state, unsigned long ioctl_cmd, const caller_data & d)
{
	if(state != LBP_STATE_OK) {
		if (state == LBP_PROTOCOL_VERSION_MISMATCH) {
			d.LOG_CPU_ERROR("\tToo old version of BIOS which does not support command ' %s BIOS '"
				". Update BIOS at node\n", d.args.get_cmd_name());
			command_err = EPERM;
			return false;
		}
		else if (state == LBP_BIOS_INFO_CACHE_EMPTY) {
			d.LOG_CPU_ERROR("\tCannot read BIOS info. Try again or if you will see this error"
				" repeatedly then try to update bios at node\n");
			command_err = EAGAIN;
			return false;
		}
		else {
			d.LOG_CPU_ERROR("%s returned with %s\n", get_csm_ioctl_name(ioctl_cmd), get_vca_lbp_retval_str(state));
			return false;
		}
	}
	return true;
}

bool set_time(filehandle_t cpu_fd, const caller_data & d)
{
	d.LOG_CPU(DEBUG_INFO, "SETTING TIME!!\n");
	vca_lbp_retval ret;
	if(!csm_ioctl(cpu_fd, LBP_SET_TIME, &ret))
		return false;

	return check_lbp_state_ok(ret, LBP_SET_TIME, d);
}

static std::string get_cpu_state(caller_data d)
{
	std::string const state = read_cpu_sysfs(d, "state");
	if (is_link_up(d))
		return state;
	return (state == VCA_POWER_DOWN_TEXT)? state : LINK_DOWN_STATE;
}

bool wait(caller_data d)
{
	unsigned int timeout = config.get_global_field(global_fields::wait_cmd_timeout_s).get_number() * 1000;
	boost::posix_time::ptime start = get_time();
	int wait_wa_counter = 0;
	std::string old_state;
	while(get_passed_time_ms(start) < timeout) {
		std::string const state= get_cpu_state(d);
		if(state == get_vca_state_string(VCA_NET_DEV_READY)) {
			d.LOG_CPU(VERBOSE_DEFAULT, "Net device ready!\n");
			return true;
		}
		else if ((state == get_vca_state_string(VCA_NET_DEV_DOWN) ||
				 state == get_vca_state_string(VCA_DHCP_ERROR)) &&
				 wait_wa_counter < WAIT_CMD_WA_ATTEMPTS) {
			wait_wa_counter++;
			// this is a WA for 'net_device_down' existance of after first Windows OS run (there is automatically reboot)
			// 'dhcp_error' state also need more time to change to 'net_device_ready'
		}
		else if (state == get_vca_state_string(VCA_DRV_PROBE_ERROR) ||
			 state == get_vca_state_string(VCA_DHCP_ERROR) ||
			 state == get_vca_state_string(VCA_NFS_MOUNT_ERROR) ||
			 state == get_vca_state_string(VCA_NET_DEV_DOWN)) {
			d.LOG_CPU_ERROR("error state detected: %s\n", state.c_str());
			if( state== VCA_NET_DEV_DOWN_TEXT)
				LOG_INFO( "Try `systemctl start vcactl`\n");
			return false;
		}
		if( old_state!= state) {
			d.LOG_CPU( DEBUG_INFO, "%s\n", state.c_str());
			old_state= state;
		}
		sleep(1);
	}
	d.LOG_CPU_ERROR("wait TIMEOUT!\n");
	return false;
}

bool devices_ready(args_holder &holder, int available_nodes)
{
	int card_id = NO_CARD, cpu_id = NO_CPU;

	const data_field *card_id_f = holder(CARD_ID_ARG);
	const data_field *cpu_id_f = holder(CPU_ID_ARG);
	if (card_id_f) {
		card_id = card_id_f->get_number();
	}
	if (cpu_id_f) {
		cpu_id = cpu_id_f->get_number();
	}

	if (card_id == NO_CARD) {
		LOG_FULL("Waiting for devices");
	}
	else if (cpu_id == NO_CPU) {
		LOG_FULL("Waiting for card %d", card_id);
	}
	else
		LOG_FULL("Waiting for card %d cpu %d", card_id, cpu_id);

	int last_ready_nodes = 0, ready_nodes = 0;
	boost::posix_time::ptime start = get_time();
	do {
		if (ready_nodes > last_ready_nodes)
			start = get_time();
		last_ready_nodes = ready_nodes;

		ready_nodes = count_ready_nodes();
		if (ready_nodes == FAIL) {
			LOG_ERROR("Count ready devices failed!\nProbably not all modules are loaded. Please execute script: vca_load_modules.sh with root privileges.\n");
			return false;
		}
		if (available_nodes == ready_nodes) {
			LOG_FULL(" DONE!\n");
			return true;
		}

		if (card_id != NO_CARD && cpu_id == NO_CPU) {
			bool ready=true;
			for (int i = get_last_cpu(card_id); 0<= i; --i) {
				if (!is_node_ready(card_id, i))
					ready=false;
			}
			if (ready) {
				LOG_FULL(" DONE!\n");
				return true;
			}
		}
		else if (card_id != NO_CARD) {
			if (is_node_ready(card_id, cpu_id)) {
				LOG_FULL(" DONE!\n");
				return true;
			}
		}
		LOG_FULL(".");
		sleep(1);
	} while (get_passed_time_ms(start) < MODPROBE_TIMEOUT_MS);

	if (verbose == VERBOSE_DEFAULT || verbose == DEBUG_INFO) {
		if (card_id == NO_CARD)
			LOG_ERROR("Command vcactl %s FAILED due to modprobe timeout for some of nodes!\n", holder.get_cmd_name());
		else if (cpu_id == NO_CPU)
			LOG_ERROR("Command vcactl %s FAILED due to modprobe timeout for card %d!\n", holder.get_cmd_name(), card_id);
		else
			LOG_ERROR("Command vcactl %s FAILED due to modprobe timeout for card %d cpu %d!\n", holder.get_cmd_name(), card_id, cpu_id);
		LOG_INFO("You can use --skip-modprobe-check option\n");
	}

	LOG_FULL(" FAILED: Timeout!\n");
	return false;
}

bool wait_bios(caller_data d)
{
	unsigned int normal_timeout = config.get_global_field(global_fields::wait_bios_cmd_timeout_s).get_number() * 1000;
	unsigned int flashing_timeout = config.get_global_field(global_fields::wait_bios_cmd_flashing_s).get_number() * 1000;
	unsigned int timeout = normal_timeout;
	unsigned int flashing_timeout_inc = 0;
	boost::posix_time::ptime start = get_time();
	if (flashing_timeout > normal_timeout)
		flashing_timeout_inc = flashing_timeout - normal_timeout;

	std::string old_state;
	int wait_wa_counter = 0;
	while(get_passed_time_ms(start) < timeout) {
		if (!try_link_up(d, LINK_DOWN_STATE_TRIES, false)) {
			d.LOG_CPU(DEBUG_INFO, "link_down occurs longer than %d sec!\n",
				(LINK_DOWN_STATE_TRIES * config.get_global_field(global_fields::link_up_timeout_ms).get_number()) / 1000);
			break;
		}
		std::string const state= get_cpu_state( d);
		if (state == get_vca_state_string(VCA_FLASHING)) {
			timeout += flashing_timeout_inc;
			flashing_timeout_inc = 0;
		}

		else if (state == get_vca_state_string(VCA_NET_DEV_DOWN)) {
			if (wait_wa_counter < WAIT_CMD_WA_ATTEMPTS)
				timeout += 1000;
			wait_wa_counter++;
			// this is a WA for longer existance of 'net_device_down' after powering down Windows OS
		}

		else if(state != get_vca_state_string(VCA_BIOS_DOWN) &&
			state != get_vca_state_string(VCA_FLASHING) &&
			state != get_vca_state_string(VCA_RESETTING) &&
			state != LINK_DOWN_STATE &&
			state != VCA_POWER_DOWN_TEXT &&
			state != VCA_POWERING_DOWN_TEXT) {
				d.LOG_CPU(VERBOSE_DEFAULT, "BIOS is up and running!\n");
			return true;
		}
		if( state!= old_state) {
			d.LOG_CPU( DEBUG_INFO, "%s\n", state.c_str());
			old_state= state;
		}
		sleep(1);
	}
	d.LOG_CPU_ERROR("wait-BIOS TIMEOUT!\n");
	return false;
}


static void* power_ok_warning(caller_data const& d,char const*value,unsigned len) {
	if( strncmp( "ok", value, len))
		d.LOG_CPU_WARN( "Power %s, use `vcactl pwrbtn-short %u %u`\n", value, d.card_id, d.cpu_id);
	return nullptr;
}


bool reset(caller_data d)
{
	d.LOG_CPU(DEBUG_INFO, "Resetting!\n");
	read_cpu_sysfs( d, "device/power_ok", power_ok_warning);
	struct vca_ioctl_desc desc;
	desc.cpu_id = d.cpu_id;
	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd)
		return false;
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;
	if( flock( cpu_fd, LOCK_EX|LOCK_NB)) { // unlocking by close file descriptor
		d.LOG_CPU_ERROR("Node is locked. Use `lslocks` to see what process holds the lock.\n");
		return false;
	}
	stop_csm(cpu_fd, d);
	return vca_ioctl(fd, VCA_RESET, &desc);
}

bool id_led(caller_data d)
{
	const data_field *subcmd_f = d.args(SUBCMD);
	std::string subcmd = subcmd_f->get_cstring();
	d.LOG_CPU(DEBUG_INFO, "LED turn %s!\n", subcmd.c_str());

	enum vca_card_type type = get_card_type(d.card_id);
	if (type != VCA_VCAA && type != VCA_VCGA) {
		d.LOG_CPU_ERROR("card type not supported\n");
		return false;
	} else {
		std::string id_led_path = get_vca_sysfs_dir(d) + VCA_ID_LED_PATH;
		close_on_exit fd(open_path(id_led_path.c_str(), O_WRONLY));
		if (!fd){
			d.LOG_CPU_ERROR("cannot open file %s\n", id_led_path.c_str());
			return false;
		}
		std::string command("id-led_" + subcmd);
		int command_size = command.size();
		if ( command_size != write(fd, command.c_str(), command_size)){
			d.LOG_CPU_ERROR("cannot write to file %s\n", id_led_path.c_str());
			return false;
		}
		return true;
	}
}

bool is_blk_disk_active(caller_data d, filehandle_t blk_dev_fd, unsigned int
			blk_dev_id)
{
	if (is_blk_disk_opened(blk_dev_fd, blk_dev_id) &&
		!is_bios_up_or_ready(d) && get_cpu_state(d) != get_vca_state_string(VCA_BIOS_DOWN)) {
		return true;
	}
	else
		return false;
}

bool check_if_blkdev_is_not_active(caller_data &d)
{
	close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
	if (!blk_dev_fd)
		return false;

	for (int i = 0; i < MAX_BLK_DEVS; i++) {
		if (is_blk_disk_rw(blk_dev_fd.fd, i) &&
			is_blk_disk_active(d, blk_dev_fd.fd, i)) {
			return false;
		}
	}
	return true;
}

bool check_if_vcablk0_is_not_active(caller_data &d)
{
	close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
	if (!blk_dev_fd)
		return false;

	if (is_blk_disk_rw(blk_dev_fd.fd, 0) &&
		is_blk_disk_active(d, blk_dev_fd.fd, 0))
		return false;
	return true;
}

bool _reset(caller_data d)
{
	if(d.args.is_force_cmd_enabled())
		return reset(d);

	if (check_if_blkdev_is_not_active(d))
		return reset(d);
	else {
		d.LOG_CPU_ERROR("VCA Card %d CPU %d has an active "\
			"blockio device in RW mode\n", d.card_id,
			d.cpu_id);
		d.LOG_CPU(VERBOSE_DEFAULT, "Resetting such a "\
			"device may lead to data corruption. Reset the "\
			"node in a graceful way (from OS level), or "\
			"invoke reset with --force flag\n");
		return false;
	}
}

int check_power_button_state(caller_data d)
{
	struct vca_ioctl_desc desc;
	desc.cpu_id = d.cpu_id;
	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd)
		return -ENOENT;


	if (!vca_ioctl(fd, VCA_CHECK_POWER_BUTTON, &desc))
		return -EAGAIN;

	return desc.ret;
}

bool wait_power_button_release(caller_data d)
{
	d.LOG_CPU(DEBUG_INFO, "Power button wait for end!\n");
	int time_max = POWER_BUTTON_TIMEOUT;
	while (check_power_button_state(d) > 0 && --time_max > 0)
		sleep(1);

	return (time_max != 0);
}

static bool press_power_button(caller_data d, bool hold)
{
	struct vca_ioctl_desc desc;
	desc.cpu_id = d.cpu_id;
	desc.hold = hold;
	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd)
		return false;

	if (!vca_ioctl(fd, VCA_POWER_BUTTON, &desc))
		return false;

	if (hold){
		close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
		csm_ioctl(cpu_fd, VCA_WRITE_SPAD_POWER_OFF, NULL);
	}

	if (!wait_power_button_release(d)) {
		d.LOG_CPU_ERROR("Failed when waiting for power button to be released.\n");
		return false;
	}

	if (hold) {
		close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
		if (!csm_ioctl(cpu_fd, VCA_WRITE_SPAD_POWER_OFF, NULL)) {
			d.LOG_CPU_ERROR("Cannot write 'power_off' state to BIOS SPADs.\n");
			return false;
		}
	}

	return true;
}

bool _toggle_power_button(caller_data d)
{
	bool pwr_up = false;
	std::string state = get_cpu_state(d);
	d.LOG_CPU(DEBUG_INFO, "Toggling power button!\n");

	if (state == get_vca_state_string(VCA_POWER_DOWN)) {
		pwr_up = true;
	}

	if(!press_power_button(d, false))
		return false;
	if (pwr_up) {
		close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
		if (!cpu_fd)
			return false;

		return csm_ioctl(cpu_fd, VCA_WRITE_SPAD_POWER_BUTTON, NULL);
	}
	return true;
}

bool check_if_power_button_supported(caller_data d) {
	enum vca_card_type card_type = get_card_type(d.card_id);
	if (card_type == VCA_UNKNOWN) {
		d.LOG_CPU_ERROR("Card generation unknown, command %s not supported\n", d.get_cmd_name());
		return false;
	}
	else if (card_type & VCA_VV ) {
		d.LOG_CPU_ERROR("Card GEN1, command %s not supported\n", d.get_cmd_name());
		return false;
	}
	else
		return true;
}

bool pwrbtn_boot(caller_data d);
bool toggle_power_button(caller_data d)
{
	if (!check_if_power_button_supported(d))
		return false;

	bool pwr_up = false;
	std::string state = get_cpu_state(d);

	if (state == get_vca_state_string(VCA_POWER_DOWN)) {
		sleep(TIME_TO_POWER_DOWN_NODE_S); // to make sure that very quick 'pwrbtn-short' after reaching 'power_off' state will work as design
		pwr_up = true;
	}

	if (!_toggle_power_button(d))
		return false;
	if (!pwr_up)
		return true;

	return pwrbtn_boot(d);
}

bool hold_power_button(caller_data d)
{
	if (!check_if_power_button_supported(d))
		return false;

	d.LOG_CPU(DEBUG_INFO, "Holding power button!\n");
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;
	if( flock( cpu_fd, LOCK_EX|LOCK_NB)) { // unlocking by close file descriptor
		d.LOG_CPU_ERROR("Node is locked. Use `lslocks` to see what process holds the lock.\n");
		return false;
	}
	stop_csm(cpu_fd, d);
	return press_power_button(d, true);
}

bool set_SMB_id(caller_data d)
{
	LOG_DEBUG("Card: %d - Setting SMB id!\n", d.card_id);
	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd)
		return false;

	const data_field *smb_id = d.args(SMB_ID_ARG);
	if (!smb_id)
		return false;

	unsigned int id = smb_id->get_number();

	return vca_ioctl(fd, VCA_SET_SMB_ID, &id);
}

bool ICMP_watchdog(caller_data d)
{
	const data_field *trigger_f = d.args(TRIGGER_ARG);
	if (!trigger_f)
		return false;

	unsigned int trigger = trigger_f->get_number();
	std::string ip;

	const data_field *ip_f = d.args(IP_ADDR_ARG);
	if (ip_f)
		ip = ip_f->get_cstring();

	std::string watchdog_cmd = ECHO " " VCA_PING_DAEMON_CMD " "
		+ int_to_string(trigger) + " "
		+ int_to_string(d.card_id) + " "
		+ int_to_string(d.cpu_id) + " "
		+ ip + " > "
		MSG_FIFO_FILE;

	if (run_cmd(watchdog_cmd.c_str()) == FAIL) {
		LOG_ERROR("Cannot execute: %s\n", watchdog_cmd.c_str());
		return false;
	}

	return true;
}

bool set_lbp_param(filehandle_t cpu_fd, vca_lbp_param param, unsigned int value, const caller_data & d)
{
	d.LOG_CPU(FULL_INFO, "SET LBP PARAM: %s with %d\n", get_vca_lbp_param_str(param), value);
	vca_csm_ioctl_param_desc desc;
	desc.param = param;
	desc.value = value;
	if(!csm_ioctl(cpu_fd, LBP_SET_PARAMETER, &desc))
		return false;

	return check_lbp_state_ok(desc.ret, LBP_SET_PARAMETER, d);
}


/**
Checks whether cpu is in "link_up" state or not.

@param d Data about parameters of excecuted command.
@param n Number of iteration of tries reading sysfs file (there is 2 sec of each try).
@param need_reset Determine if reset is needed after link state will be down
@return Returns true if link_state is equal to "up"
        or false if within n seconds link_state will be still "down".
*/
bool try_link_up(const caller_data & d, int n, bool need_reset)
{
	d.LOG_CPU(FULL_INFO, "Checking for link up!\n");
	for(int i = 0; i < n; i++) {
		if(check_for_link_up(d, config.get_global_field(global_fields::link_up_timeout_ms).get_number()))
			return true;

		if (need_reset)
			reset(d);
	}
	return false;
}

bool handshake(filehandle_t cpu_fd, const caller_data & d)
{
	vca_lbp_retval ret;
	if(!csm_ioctl(cpu_fd, LBP_HANDSHAKE, &ret))
		return false;

	return check_lbp_state_ok(ret, LBP_HANDSHAKE, d);
}

bool try_handshake(filehandle_t cpu_fd, const caller_data & d)
{
	d.LOG_CPU(DEBUG_INFO, "Trying handshake!\n");
	for(int i = 0; i < HANDSHAKE_RESET_TRIES; i++) {
		if(handshake(cpu_fd, d))
			return true;

		reset(d);

		if (!try_link_up(d, LINK_DOWN_RESET_TRIES, true))
			return false;
	}
	d.LOG_CPU_ERROR("Failed to handshake after %d tries.\n", HANDSHAKE_RESET_TRIES);

	return false;
}

bool csm_ioctl_with_img(filehandle_t cpu_fd, unsigned long ioctl_cmd, const caller_data & d, void *img, size_t img_size)
{
	d.LOG_CPU(DEBUG_INFO, "%s\n", get_csm_ioctl_name(ioctl_cmd));
	vca_csm_ioctl_mem_desc desc;
	desc.mem = img;
	desc.mem_info = img_size;
	if(!csm_ioctl(cpu_fd, ioctl_cmd, &desc))
		return false;

	return check_lbp_state_ok(desc.ret, ioctl_cmd, d);
}

bool try_ioctl_with_img(filehandle_t cpu_fd, unsigned long ioctl_cmd, const caller_data & d, void *img, size_t img_size)
{
	for(int i = 0; i < AFTER_HANDSHAKE_RESET_TRIES; i++) {
		if (csm_ioctl_with_img(cpu_fd, ioctl_cmd, d, img, img_size))
			return true;

		reset(d);

		if(!try_handshake(cpu_fd, d))
			return false;
	}
	d.LOG_CPU_ERROR("Failed to execute IOCTL (%s) after %d tries.\n",
		get_csm_ioctl_name(ioctl_cmd), AFTER_HANDSHAKE_RESET_TRIES);

	return false;
}

bool csm_ioctl_with_blk(filehandle_t cpu_fd, unsigned long ioctl_cmd, const caller_data & d)
{
	d.LOG_CPU(DEBUG_INFO, "%s\n", get_csm_ioctl_name(ioctl_cmd));
	enum vca_lbp_retval ret = LBP_STATE_OK;
	if(!csm_ioctl(cpu_fd, ioctl_cmd, &ret))
		return false;

	return check_lbp_state_ok(ret, ioctl_cmd, d);
}

bool try_ioctl_with_blk(filehandle_t cpu_fd, unsigned long ioctl_cmd, const caller_data & d)
{
	for(int i = 0; i < AFTER_HANDSHAKE_RESET_TRIES; i++) {
		if (csm_ioctl_with_blk(cpu_fd, ioctl_cmd, d))
			return true;

		reset(d);

		if(!try_handshake(cpu_fd, d))
			return false;
	}
	return true;
}

bool prepare_cpu(filehandle_t cpu_fd, const caller_data &d)
{
	d.LOG_CPU(DEBUG_INFO, "PREPARING CARD!\n");

	if (!set_lbp_param(cpu_fd, VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS,
		config.get_global_field(global_fields::handshake_irq_timeout_ms).get_number(), d))
			return false;

	if (!set_lbp_param(cpu_fd, VCA_LBP_PARAM_i7_ALLOC_TIMEOUT_MS,
		config.get_global_field(global_fields::alloc_timeout_ms).get_number(), d))
			return false;

	if (!set_lbp_param(cpu_fd, VCA_LBP_PARAM_i7_CMD_TIMEOUT_MS,
		config.get_global_field(global_fields::cmd_timeout_ms).get_number(), d))
			return false;

	if (!set_lbp_param(cpu_fd, VCA_LBP_PARAM_i7_MAC_WRITE_TIMEOUT_MS,
		config.get_global_field(global_fields::mac_write_timeout_ms).get_number(), d))
			return false;

	if (!try_link_up(d, LINK_DOWN_RESET_TRIES, true))
		return false;

	if (get_vca_state_string(VCA_BIOS_READY) != get_cpu_state(d)) {
		if (!try_handshake(cpu_fd, d))
			return false;
	}
	else
		LOG_DEBUG("Skipping handshake, because node is already in bios_ready state\n");

	d.LOG_CPU(DEBUG_INFO, "Preparing card SUCCESS!\n");
	return true;
}

inline const char * get_cpu_field_if_not_empty(const caller_data & d, cpu_fields::_enum field, bool silent=false)
{
	const data_field & data = config.get_cpu_field(d.card_id, d.cpu_id, field);
	const char * value = data.get_cstring();
	if(strcmp(value, "") == 0) {
		if(!silent)
			d.LOG_CPU_ERROR("%s in config file is EMPTY!\n", data.get_name());
		return NULL;
	}
	return value;
}

static std::string get_hostip(const caller_data & d)
{
	const char *ip = get_cpu_field_if_not_empty(d, cpu_fields::ip);
	char dev_buffer[BUFFER_SIZE];
	if (!ip)
		return "";
	/* In case of IP assigned directly to node, use host side IP of virtual
	 * interface */
	if (is_ip_address(ip)) {
		const char *hostip = get_cpu_field_if_not_empty(d, cpu_fields::host_ip);
		/* if hostip == NULL log message will be printed in function
		 * 'get_cpu_field_if_not_empty' */
		if (!hostip)
			return "";
		if (!is_ip_address(hostip)) {
			d.LOG_CPU_ERROR("Card's node has IP statically assigned,"
					" while host side of the interface doesn't"
					" have a proper address configured: %s\n", hostip);
			return "";
		}
		return hostip;
	}
	if (strncasecmp(DHCP_STRING, ip, strlen(DHCP_STRING))) {
		d.LOG_CPU_ERROR("Card doesn't have IP or dhcp assigned: %s\n", ip);
		return "";
	}
	/* In case of node's IP taken from dhcp, must check if node's virtual
	 * interface is assinged to bridge. If so, must use bridge's IP */
	const char *bridge = get_cpu_field_if_not_empty(d, cpu_fields::bridge_interface);
	if (bridge) {
		size_t brigde_len = strlen(bridge);
		if (brigde_len + 1 > sizeof(dev_buffer)) {
			d.LOG_CPU_ERROR("Bridge device name too long: %d, max %d\n",
					brigde_len, sizeof(dev_buffer));
			return "";
		}
		STRCPY_S(dev_buffer, bridge, sizeof(dev_buffer));
	} else {
		/* In case of lack of bridging, use address assigned to default
		 * host interface. To do so, we need name of that interface. */
		const char *cmd = "ip route | awk '/^default / { print $5 }'";
		if (run_cmd_with_output(cmd, dev_buffer, sizeof(dev_buffer)) == FAIL) {
			LOG_ERROR("Cannot execute: %s\n", cmd);
			return "";
		}

		if (!dev_buffer[0]) {
			d.LOG_CPU_ERROR("Cannot obtain default network interface\n");
			return "";
		}
	}

	char command[BUFFER_SIZE];
	const int ret = snprintf(command, sizeof(command),
			"ip addr show dev %s | awk -F'[ /]+' '$2 == \"inet\" { print $3 }'",
			dev_buffer);
	if (ret == FAIL || ret >= (int) sizeof(command)) {
		d.LOG_CPU_ERROR("Command string to obtain host IP on interface %s too long: %s\n",
				dev_buffer, command);
		return "";
	}

	if (run_cmd_with_output(command, dev_buffer, sizeof(dev_buffer)) == FAIL) {
		LOG_ERROR("Cannot execute: %s\n", command);
		return "";
	}
	if (!is_ip_address(dev_buffer)) {
		d.LOG_CPU_ERROR("Read address isn't proper IP: %s\n", dev_buffer);
		return "";
	}

	return dev_buffer;
}

static std::string get_nodename(const caller_data & d)
{
	return std::string("\%s");
}

static std::string get_nodeip(const caller_data & d)
{
	return std::string("\%i");
}

static void strip_quotes(std::string& phrase)
{
	replace_all(phrase, "\"", "");
	replace_all(phrase, "\'", "");
}

bool parse_nfs_path(const caller_data & d, std::string& path)
{

	if(path.empty())
		return true;

	if(path.find("%hostname") != std::string::npos)
	{
		char hostname[BUFFER_SIZE] = "";
		if (gethostname(hostname, BUFFER_SIZE-1) == FAIL) {
			if (errno == ENAMETOOLONG)
				LOG_ERROR("hostname does not fit into %d bytes\n", BUFFER_SIZE-1);
			LOG_ERROR("Could not get hostname\n");
			return false;
		}
		replace_all(path, "%hostname", hostname);
	}

	if(path.find("%hostip") != std::string::npos)
	{
		std::string hostip = get_hostip(d);
		if (hostip.empty())
			return false;
		replace_all(path, "%hostip", hostip);
	}

	if(path.find("%nodename") != std::string::npos)
	{
		std::string nodename = get_nodename(d);
		if (nodename.empty())
			return false;
		replace_all(path, "%nodename", nodename);
	}

	if(path.find("%nodeip") != std::string::npos)
	{
		std::string nodeip = get_nodeip(d);
		if (nodeip.empty())
			return false;
		replace_all(path, "%nodeip", nodeip);
	}

	strip_quotes(path);

	return true;
}

static bool make_net_config_windows(const caller_data& d,char const* mac,char const* ip,char const* mask,const char* gateway){
	std::string path= std::string(VCASYSFSDIR) + "/vca"
		+ int_to_string( d.card_id) + int_to_string( d.cpu_id) + "/net_config_windows";
	if (close_on_exit fd = open_path(path.c_str(), O_WRONLY|O_SYNC)) {
		std::string card_script;
		card_script+=std::string()
			+ "ip=" + ip +"\n"
			+ "mask=" + mask+ "\n"
			+ "gateway=" + gateway + "\n"
			+ "mtu=" MTU_VALUE "\n";
		if( write( fd, card_script.c_str(), card_script.length())==(int) card_script.length())
			return true; // SUCCESS
		d.LOG_CPU_ERROR( "Write to %s\n", path.c_str());
	}
	return false;
}

static bool make_net_config(const caller_data& d,char const* mac,char const* ip,char const* mask,const char* gateway){
	std::string net_cfg_path = std::string(VCASYSFSDIR) + "/vca"
		+ int_to_string(d.card_id) + int_to_string(d.cpu_id) + "/net_config";
	if (close_on_exit fd = open_path(net_cfg_path.c_str(), O_WRONLY|O_SYNC)) {
		std::string nfs_server= get_cpu_field_if_not_empty(d, cpu_fields::nfs_server) ?: "";
		strip_quotes(nfs_server);
		std::string nfs_path= get_cpu_field_if_not_empty(d, cpu_fields::nfs_path) ?: "";
		if( !parse_nfs_path(d, nfs_path)) {
			d.LOG_CPU_WARN( "Problems during processing NFS path %s\n", nfs_path.c_str());
		}
		std::string card_script= "#!/bin/bash\n"
				"trap 'echo -e \"Error ${BASH_COMMAND}\" >&2' ERR\n"
				"INTERFACE=`basename /sys/class/vca/vca/device/vop-dev0255/virtio0/net/*`\n";
		if( strncasecmp( DHCP_STRING, ip, strlen( DHCP_STRING)) == 0) {
			card_script+= std::string()+
				IP " link set ${INTERFACE} mtu " MTU_VALUE "\n"
				IP " link set dev ${INTERFACE} address " + mac + "\n"
				IP " link set dev ${INTERFACE} up\n"
				ECHO " \'" + nfs_server + "\' > /etc/nfs_server\n"
				ECHO " \'" + nfs_path + "\' > /etc/nfs_path\n"
				ECHO " " VCA_DHCP_IN_PROGRESS_TEXT " > /sys/class/vca/vca/state\n"
				"if [ ! -d /var/lib/dhclient ] ; then "
				MKDIR " -p /var/lib/dhclient ; fi\n"
				DHCLIENT " ${INTERFACE}\n"
				"if [ -s /var/lib/dhclient/dhclient.leases ]; then "
				ECHO " " VCA_DHCP_DONE_TEXT " > /sys/class/vca/vca/state; else "
				ECHO " " VCA_DHCP_ERROR_TEXT " > /sys/class/vca/vca/state; fi\n";
		}
		else if( is_ip_address( ip)) {
			int imask = atoi(mask);
			if (imask < 0 || imask > 32)
				return false;
			card_script+= std::string()+
				IP " link set ${INTERFACE} mtu " MTU_VALUE "\n"
				IP " link set dev ${INTERFACE} address " + mac + "\n"
				IP " addr add " + ip + "/" + int_to_string(imask) + " dev ${INTERFACE}\n"
				IP " link set dev ${INTERFACE} up\n"
				IP " route add " + gateway + " dev ${INTERFACE}\n"
				IP " route add default via " + gateway + "\n"
				ECHO " \'" + nfs_server + "\' > /etc/nfs_server\n"
				ECHO " \'" + nfs_path + "\' > /etc/nfs_path\n";
		}
		else {
			d.LOG_CPU_ERROR("Wrong ip. Use: `vcactl config %u %u ip`\n", d.card_id, d.cpu_id);
			return false;
		}
		if(const char *node_name= get_cpu_field_if_not_empty( d, cpu_fields::node_name)) {
			card_script.append( ECHO " \'");
			card_script.append(node_name);
			card_script.append("\' > /proc/sys/kernel/hostname \n");
		}
		if( write( fd, card_script.c_str(), card_script.length())==(int) card_script.length())
			return true; // SUCCESS
		d.LOG_CPU_ERROR( "Write to %s\n", net_cfg_path.c_str());
	}
	return false;
}

static bool make_sys_config(const caller_data& d){
	std::string path = std::string(VCASYSFSDIR) + "/vca"
		+ int_to_string(d.card_id) + int_to_string(d.cpu_id) + "/sys_config";
	if (close_on_exit fd = open_path(path.c_str(), O_WRONLY|O_SYNC)) {
		std::string sys_cfg_cmd= "#!/bin/bash\ntrap 'echo -e \"Error ${BASH_COMMAND}\" >&2' ERR\n";
		if(const char* script_path = get_cpu_field_if_not_empty(d, cpu_fields::script_path, true)) {
			if(close_on_exit fd= open_path( script_path, O_RDONLY)) {
				struct stat info;
				if (fstat(fd, &info)) {
					d.LOG_CPU_ERROR("fstat %s: %s\n", strerror(errno), path.c_str());
					return false;
				}
				const char *b;
				if ((b = (char*) mmap(0, info.st_size, PROT_READ, MAP_SHARED, fd, 0)) == MAP_FAILED) {
					d.LOG_CPU_ERROR("mmap: %s: %s\n", strerror(errno), path.c_str());
					return false;
				}
				sys_cfg_cmd.append(b, info.st_size);
				munmap((void*) b, info.st_size);
			} else {
				d.LOG_CPU_ERROR("open %s\n", path.c_str());
				return false;
			}
		}
		if(const char *va_free_mem_enable = get_cpu_field_if_not_empty(d, cpu_fields::va_free_mem))
			if( !strcmp( va_free_mem_enable, "1"))
				sys_cfg_cmd+= "sysctl -w vm.min_free_kbytes=" MIN_MEM_FREE_OF_CACHE_CARD_SIDE "\n";
		if( write( fd, sys_cfg_cmd.c_str(), sys_cfg_cmd.length())==(int) sys_cfg_cmd.length())
			return true; // SUCCESS
		d.LOG_CPU_ERROR("Write to %s\n", path.c_str());
	}
	return false;
}

static bool pass_script(const caller_data& d, char unsigned mac_addr[6])
{
	char mac[19];
	snprintf( mac, 18,"%02x:%02x:%02x:%02x:%02x:%02x",
		mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
	if(const char *ip = get_cpu_field_if_not_empty(d, cpu_fields::ip) )
		if(const char* mask = get_cpu_field_if_not_empty(d, cpu_fields::mask))
			if(const char* gateway = get_cpu_field_if_not_empty(d, cpu_fields::gateway))
				return make_sys_config( d)
					&& make_net_config( d, mac, ip, mask, gateway)
					&& make_net_config_windows( d, mac, ip, mask, gateway);
			else d.LOG_CPU_ERROR("Missing gateway. Use `vcactl config %u %u gateway`\n", d.card_id, d.cpu_id);
		else d.LOG_CPU_ERROR("Missing mask. Use `vcactl config %u %u mask`\n", d.card_id, d.cpu_id);
	else d.LOG_CPU_ERROR("Missing IP. Use `vcactl config  %u %u ip`\n", d.card_id, d.cpu_id);
	return false;
}

const char * try_get_path(const caller_data & d, const data_field & data)
{
	const data_field * file_path_f = d.args(FILE_PATH_ARG);
	if (file_path_f)
		return file_path_f->get_cstring();
	else {
		const char * default_path = data.get_cstring();
		if(strcmp(default_path, "") == 0) {
			d.LOG_CPU_ERROR("%s not set! Please run: vcactl config <card_id> <cpu_id> %s <path_to_file>\n",
				data.get_name(), data.get_name());
			return NULL;
		}

		if (!strcmp(default_path, BLOCKIO_BOOT_DEV_NAME)) {
			return default_path;
		} else {
			if(file_exists(default_path))
				return default_path;
			else {
				d.LOG_CPU_ERROR("%s Can't access file %s!\n", data.get_name(),
						default_path);
				return NULL;
			}
		}
	}
}

std::map<unsigned int, std::string> get_card_state(caller_data d)
{
	std::map<unsigned int, std::string> card;
	for (int i = get_last_cpu(d.card_id);0<= i; i--) {
		caller_data nodes = caller_data(d.card_id, (int)i, d.args);
		card[i] = get_cpu_state(nodes);
	}

	return card;
}

std::string get_cpu_os(caller_data d)
{
	if (is_link_up(d))
		return read_cpu_sysfs(d, "os_type");
	else
		return VCA_OS_TYPE_UNKNOWN_TEXT;
}

bool is_os_windows(std::string os)
{
	return (os == "windows");
}

void get_bios_flags_state(caller_data d, std::string &state)
{
	if (is_bios_up_or_ready(d))
		state = read_cpu_sysfs(d, "bios_flags");
}

void check_rcvy_jumper_state(caller_data d)
{
	std::string state;
	get_bios_flags_state(d, state);
	if (state == VCA_JUMPER_OPEN)
		d.LOG_CPU(VERBOSE_DEFAULT, "Warning: Jumper selects GOLD BIOS. Next reboot will boot it.\n");
}


static bool is_agent_ready(const caller_data &d) {
    const std::string state = get_cpu_state(d);
    return
           state == VCA_OS_READY_TEXT
        || state == VCA_NET_DEV_READY_TEXT
        || state == VCA_DHCP_ERROR_TEXT
        || state == VCA_NET_DEV_NO_IP_TEXT
        || state == VCA_DHCP_IN_PROGRESS_TEXT
        || state == VCA_DHCP_DONE_TEXT
        || state == VCA_NET_DEV_DOWN_TEXT;
}


static bool vca_agent_command(caller_data& d, std::string &output, vca_agent_cmd cmd)
{
    close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
    if (!cpu_fd)
        return false;
    if( flock( cpu_fd.fd, LOCK_EX|LOCK_NB)) { // unlocking by close file descriptor
        d.LOG_CPU_ERROR("Node is locked. Check `lslocks`.\n");
        return false;
    }
    if (!is_agent_ready(d)) {
        d.LOG_CPU_ERROR("Card needs to be in \"%s\", \"%s\", \"%s\", \"%s\", \"%s\" or \"%s\" state!\n",
                VCA_OS_READY_TEXT,
                VCA_NET_DEV_READY_TEXT,
                VCA_DHCP_ERROR_TEXT,
                VCA_NET_DEV_NO_IP_TEXT,
                VCA_DHCP_IN_PROGRESS_TEXT,
                VCA_DHCP_DONE_TEXT,
                VCA_NET_DEV_DOWN_TEXT);
        return false;
    }
    if(vca_csm_ioctl_agent_cmd*const data = (vca_csm_ioctl_agent_cmd *)calloc( 1, sizeof(*data) + PAGE_SIZE)) {
        do {
            *(char *)data->buf = cmd;
            data->buf_size = PAGE_SIZE;
            if(!csm_ioctl(cpu_fd, VCA_AGENT_COMMAND, data))
                break;
            if (!check_lbp_state_ok(data->ret, VCA_AGENT_COMMAND, d))
                break;
            if(data->buf_size >= PAGE_SIZE) {
                d.LOG_CPU_ERROR("IOCTL returned with corrupted values!\n");
                break;
            }
            if (data->buf_size) {
                if(data->buf[data->buf_size-1] == '\n')
                    data->buf[data->buf_size-1] = '\0';
            }
            // construct std::string from char* informing explicitely about length (KW issue)
            output.assign(data->buf, std::find(data->buf, data->buf + PAGE_SIZE - 1 , '\0'));
            free(data);
            return true; // success
        } while(false);
        free(data);
    }
    return false;

}


static const char* update_status_with_bios_flags(caller_data d)
{
	std::string state;
	get_bios_flags_state(d, state);
	if (state == VCA_JUMPER_OPEN)
		return ", gold_bios";
	return "";
}

static const char* check_dma_state(caller_data d)
{
	std::string const dma_hang_path = get_vca_sysfs_dir(d) + VCA_DMA_PATH;
	d.LOG_CPU(DEBUG_INFO, "Check path DMA HANG: %s\n", dma_hang_path.c_str());
	if (file_exists(dma_hang_path.c_str())) {
		std::string dma_state = read_cpu_sysfs(d, VCA_DMA_PATH);
		d.LOG_CPU(DEBUG_INFO, "Check dma_state: %s\n", dma_state.c_str());
		if (dma_state == VCA_DMA_HANG)
			return ", dma_hang";
	}
	return "";
}

static const char*  check_caterr(caller_data d, std::string const& state)
{
	std::string caterr_path = get_vca_sysfs_dir(d) + VCA_CATERR_PATH;
	if (state != VCA_POWER_DOWN_TEXT) {
		enum vca_card_type type = get_card_type(d.card_id);
		if (type == VCA_FPGA || type == VCA_VCAA || type == VCA_VCGA) {
			if (d.cpu_id == 0 || d.cpu_id == 1) {
				if (file_exists(caterr_path.c_str())) {
					std::string caterr_state = read_cpu_sysfs(d, VCA_CATERR_PATH);
					if (caterr_state == VCA_CATERR_YES) {
						return ", caterr";
					}
				}
			}
		}
	}
	return "";
}

static const char* update_power_button_status(caller_data d)
{
	d.LOG_CPU(DEBUG_INFO, "Check power button pressed\n");
	int state = check_power_button_state(d);
	if (state > 0) {
		return ", pwr_btn_press";
	} else if (state < 0) {
		return ", pwr_btn_unknown";
	}
	return "";
}

static bool status(caller_data d)
{
	std::string const state = get_cpu_state(d);
	if(state.empty()) {
		d.LOG_CPU_ERROR("Could not read state!\n");
		return false;
	}
	else if( state== LINK_DOWN_STATE) {
		read_cpu_sysfs( d, "device/power_ok", power_ok_warning);
	}
	else if( state== VCA_AFTER_REBOOT_TEXT || state== VCA_NET_DEV_DOWN_TEXT || state== VCA_NET_DEV_NO_IP_TEXT) {
		d.LOG_CPU_WARN("Check `systemctl status vcactl`\n");
	}
	printf("Card: %d\tCpu: %d\tSTATE: %s%s%s%s%s\n", d.card_id, d.cpu_id, state.c_str(),
			check_dma_state(d),
			update_status_with_bios_flags(d),
			update_power_button_status(d),
			check_caterr(d, state));
	return true;
}

static bool help(caller_data d)
{
	if (d.caller_id == 0) {
		print_help();
	}
	return true;
}

bool try_set_new_bios_params(filehandle_t cpu_fd, const caller_data & d)
{
	vca_csm_ioctl_bios_param_desc desc;
	desc.param = VCA_LBP_BIOS_PARAM_CPU_MAX_FREQ_NON_TURBO;
	unsigned int value = config.get_cpu_field(d.card_id, d.cpu_id,
		cpu_fields::cpu_max_freq_non_turbo).get_number();
	desc.value.value = value;

	d.LOG_CPU(DEBUG_INFO, "GETTING BIOS PARAM!\n");
	if(!csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &desc))
		return false;

	if(desc.ret == LBP_PROTOCOL_VERSION_MISMATCH)
		return false;

	if(!check_lbp_state_ok(desc.ret, LBP_GET_BIOS_PARAM, d))
		return false;

	d.LOG_CPU(DEBUG_INFO, "GOT BIOS PARAM OK!\n");

	if(value != desc.value.value) {
		d.LOG_CPU(DEBUG_INFO, "BIOS PARAM DIFFER FROM XML! GOT: %d XML: %d\n",
			desc.value, value);
		desc.value.value = value;
		d.LOG_CPU(DEBUG_INFO, "SETTING BIOS PARAM!\n");
		if(!csm_ioctl(cpu_fd, LBP_SET_BIOS_PARAM, &desc))
			return false;

		if(!check_lbp_state_ok(desc.ret, LBP_SET_BIOS_PARAM, d))
			return false;

		d.LOG_CPU(DEBUG_INFO, "BIOS PARAM SET OK!\n");
	} else
		return false;

	return true;
}

bool enable_blk_dev(caller_data d, unsigned int blk_dev_id)
{
	return config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_ENABLED_STR, "1");
}

bool disable_blk_dev(caller_data d, unsigned int blk_dev_id)
{
	return config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_ENABLED_STR, "0");
}

bool is_blk_enabled(int card_id, int cpu_id, unsigned int blk_dev_id)
{
	return !strcmp(config.get_blk_field(card_id, cpu_id, blk_dev_id, blk_dev_fields::enabled).get_cstring(), "1");
}

bool is_any_blkdev_enabled(caller_data d)
{
	config.get_vca_config_from_file();
	for (int i = 0; i < MAX_BLK_DEVS; i++) {
		if (is_blk_enabled(d.card_id, d.cpu_id, i)) {
			d.LOG_CPU_ERROR("VCA has an active blockio device in RW mode. You need to close it before config-default\n");
			return true;
		}
	}
	return false;
}

static bool is_blk_ramdisk_size_mb_ok(size_t size_mb)
{
	size_t max_size_t = std::numeric_limits<size_t>::max();
	size_t max_ramdisk_size_mb = B_TO_MB(max_size_t);
	return (size_mb > 0 && size_mb <= max_ramdisk_size_mb);
}

bool get_blk_config_field(caller_data d, unsigned int blk_dev_id, struct vca_blk_dev_info &blk_dev_info)
{
	std::string mode = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::mode).get_string();

	blk_dev_info.blk_dev_id = blk_dev_id;
	blk_dev_info.type = VCABLK_DISK_TYPE_FILE;
	blk_dev_info.size_mb = 0;
	blk_dev_info.mode = BLK_MODE_RW;
	blk_dev_info.path = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::path).get_string();

	if (mode == BLOCKIO_MODE_RAMDISK) {
		blk_dev_info.type = VCABLK_DISK_TYPE_MEMORY;
		blk_dev_info.size_mb = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::ramdisk_size_mb).get_number();

		if (!is_blk_ramdisk_size_mb_ok(blk_dev_info.size_mb)) {
			LOG_ERROR("Invalid block device ramdisk-size. Have to be more than 0.\n");
			return false;
		}
	}
	else if (mode == BLOCKIO_MODE_RO || mode == BLOCKIO_MODE_RW) {
		if (!file_exists(blk_dev_info.path.c_str())) {
			d.LOG_CPU_ERROR("File (%s) used from config to create block device %s cant' be accessed!\n",
				blk_dev_info.path.c_str(), get_block_dev_name_from_id(blk_dev_id).c_str());
			return false;
		}

		if (mode == BLOCKIO_MODE_RO) {
			blk_dev_info.mode = BLK_MODE_RO;
		}
	}
	else {
		d.LOG_CPU_ERROR("Incorrect block device mode (%s) in config file for block device %s!\n",
				mode.c_str(), get_block_dev_name_from_id(blk_dev_id).c_str());
		return false;
	}

	blk_dev_info.enabled = is_blk_enabled(d.card_id, d.cpu_id, blk_dev_id);

	return true;
}

bool check_blk_path(caller_data d, int cards_num);
bool open_enabled_blk_devs(caller_data d)
{
	close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
	if (!blk_dev_fd)
		return false;

	struct vca_blk_dev_info blk_dev_info;

	if (!check_blk_path(d, get_cards_num()))
		return false;

	for (int blk_dev_id = 0; blk_dev_id < MAX_BLK_DEVS; blk_dev_id++) {
		if (config.blk_dev_exist(d.card_id, d.cpu_id, blk_dev_id) && is_blk_enabled(d.card_id, d.cpu_id, blk_dev_id)) {
			if (check_blk_disk_exist(blk_dev_fd.fd, blk_dev_id)) {
				d.LOG_CPU(DEBUG_INFO, "Block device %s already exist.\n", get_block_dev_name_from_id(blk_dev_id).c_str());
			}
			else {
				if (!get_blk_config_field(d, blk_dev_id, blk_dev_info)) {
					d.LOG_CPU_ERROR("Cannot get blk_dev_info from config\n");
					return false;
				}

				if (open_blk_dev(blk_dev_fd.fd, blk_dev_info) != SUCCESS) {
					d.LOG_CPU_ERROR("Open block device %s failed!\n", get_block_dev_name_from_id(blk_dev_id).c_str());
					return false;
				}
			}
		}
	}
	return true;
}

static bool is_mv_bios_version_2_0(std::string current_bios_version, std::string bios_version_2_0)
{
	// based on BIOS versioning model since beginning of this project we are sure that comparing strings is enough
	return current_bios_version >= bios_version_2_0;
}

bool is_mv_bios_version_correct(caller_data d)
{
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;

	if (!is_mv_bios_version_2_0(get_bios_version(d, cpu_fd), BIOS_MV_2_0_VERSION))
		return false;

	return true;
}

bool get_bios_cfg_for(caller_data &d, filehandle_t fd, vca_lbp_bios_param param, unsigned long long &result)
{
	// TODO: refactor state checking to common function for both {get,set}_bios_cfg
	// TODO: use this function also for get_bios_cfg

	vca_csm_ioctl_bios_param_desc desc;
	desc.param = param;

	if (!csm_ioctl(fd, LBP_GET_BIOS_PARAM, &desc)) {
		return false;
	}
	if (!check_lbp_state_ok(desc.ret, LBP_GET_BIOS_PARAM, d)) {
		return false;
	}
	result = desc.value.value;
	return true;
}

bool boot(caller_data d)
{
	using namespace boost::interprocess;

	d.LOG_CPU(DEBUG_INFO, "Running \"boot\" function!\n");

	if (!validate_node_range(d)) {
		d.LOG_CPU_ERROR("Invalid caller data\n");
		return false;
	}

	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;
	if( flock( cpu_fd, LOCK_EX|LOCK_NB)) { // unlocking by close file descriptor
		d.LOG_CPU_ERROR("Node is locked. Use `lslocks` to see what process holds the lock.\n");
		return false;
	}
	#ifdef SGX
	unsigned long long current_state;
	std::string fix_command;
	vca_lbp_bios_param check_params[] = {VCA_LBP_PARAM_SGX, VCA_LBP_PARAM_HT, VCA_LBP_PARAM_GPU};
	for (auto &param: check_params){
		if(!get_bios_cfg_for(d, cpu_fd, param, current_state)){
			d.LOG_CPU_ERROR("Prerequisite checking failed. Aborting.\n");
			return false;
		}
		if(param == VCA_LBP_PARAM_SGX && !current_state) {
			fix_command += std::string(" ") + BIOS_CFG_SGX + " enable";
			fix_command += std::string(" ") + BIOS_CFG_EPOCH + " factory-default";
			fix_command += std::string(" ") + BIOS_CFG_PRM + " AUTO";
		}
		if(param == VCA_LBP_PARAM_HT && current_state)
			fix_command += std::string(" ") + BIOS_CFG_HT + " disable";
		if(param == VCA_LBP_PARAM_GPU && current_state)
			fix_command += std::string(" ") + BIOS_CFG_GPU + " disable";
	}
	if(!fix_command.empty()){
		d.LOG_CPU_ERROR("Configuration is not valid for boot. Consider running command: vcactl set-BIOS-cfg%s\n", fix_command.c_str());
		return false;
	}
	#endif

	std::string node_lock_file_name = get_node_lock_file_name(d);

	if (!file_create(node_lock_file_name.c_str(), O_WRONLY))
		return false;

	file_lock f_lock(node_lock_file_name.c_str());

	// this scoped_lock ensures that unlock() method will be executed
	// on every exit point from this function
	scoped_lock<file_lock> e_lock(f_lock, try_to_lock_type());

	if (!f_lock.try_lock()) {
		d.LOG_CPU_ERROR("Boot command already running! "
			"Cannot execute it twice at the same time.\n");
		return false;
	}

	const char *relative_path = NULL;

	const data_field * param_name = d.args(FORCE_GET_LAST_OS_IMAGE);

	if (param_name){
		relative_path = config.get_cpu_field(d.card_id,
			d.cpu_id, cpu_fields::last_img_path).get_cstring();
		d.LOG_CPU(DEBUG_INFO, "Booting from last-os-image field.\n");
	}
	else{
		relative_path = try_get_path(d,
			config.get_cpu_field(d.card_id, d.cpu_id, cpu_fields::img_path));
	}

	if (!relative_path)
		return false;

	char boot_path[PATH_MAX + 1];
	struct stat info;
	char const*file_lbp = NULL;

	if (!strcmp(relative_path, BLOCKIO_BOOT_DEV_NAME)) {
		d.LOG_CPU(DEBUG_INFO, "BOOT BLOCK DEVICE!\n");
		STRCPY_S(boot_path, relative_path, sizeof(boot_path));
	} else {
		if (!realpath(relative_path, boot_path)) {
			d.LOG_CPU_ERROR("Cannot canonicalize OS image path (got %s): %s!\n",
					relative_path, strerror(errno));
			return false;
		}

		d.LOG_CPU(DEBUG_INFO, "Loading file %s\n", boot_path);
		close_on_exit fd(open_path(boot_path, O_RDONLY));
		if (fstat(fd, &info)) {
			d.LOG_CPU_ERROR("fstat: %s: %s\n", strerror(errno), boot_path);
			return false;
		}
		file_lbp = (char*)mmap(0, info.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (file_lbp == MAP_FAILED) {
			d.LOG_CPU_ERROR("Could not open file: %s\n", boot_path);
			return false;
		}
	}

	unsigned char mac_addr[6] = {0};
	enum vca_card_type type = get_card_type(d.card_id);
	if (type & VCA_PRODUCTION) {
		std::string state;
		state = get_cpu_state(d);
		if (state == get_vca_state_string(VCA_NET_DEV_READY)) {
			d.LOG_CPU(VERBOSE_DEFAULT, "OS ALREADY UP!\n");
			return false;
		}

		if (state != get_vca_state_string(VCA_BIOS_UP) && state != get_vca_state_string(VCA_BIOS_READY) && state != get_vca_state_string(VCA_AFTER_REBOOT)) {
			d.LOG_CPU_ERROR("Card needs to be in \"%s\", \"%s\" or \"%s\" state!\n",
				get_vca_state_string(VCA_BIOS_UP), get_vca_state_string(VCA_BIOS_READY), get_vca_state_string(VCA_AFTER_REBOOT));
			return false;
		}

		if (!open_enabled_blk_devs(d)) {
			d.LOG_CPU_ERROR("Cannot open block devices.\n");
			return false;
		}

		if (!file_lbp) {
			close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
			if (!blk_dev_fd)
				return false;

			if (!check_blk_disk_exist(blk_dev_fd.fd, 0)) {
				d.LOG_CPU_ERROR("Block device vcablk0 need be in inactive/active state!\n");
				return false;
			}
		}

		/* Make sure vop device is removed. It is possible that virtio device is removed
		 * while old vop device is still registered. This happens when card reset
		 * was triggered by the card rather than via vcactl */
		if (!stop_csm(cpu_fd, d)) {
			d.LOG_CPU_ERROR("Could not stop csm!\n");
			return false;
		}

		if (!prepare_cpu(cpu_fd, d)) {
			d.LOG_CPU_ERROR("Could not prepare cpu!\n");
			return false;
		}

		if (try_set_new_bios_params(cpu_fd, d)) {
			reset(d);
			d.LOG_CPU(DEBUG_INFO, "WAITING FOR BIOS UP!\n");
			if (!wait_bios(d))
				return false;
			d.LOG_CPU(DEBUG_INFO, "PREPARING CPU ONCE AGAIN WITH NEW BIOS SETTING!\n");
			if (!prepare_cpu(cpu_fd, d))
				return false;
		}

		if (!get_mac_addr(cpu_fd, d, mac_addr)) {
			d.LOG_CPU_ERROR("Could not get mac address!\n");
			return false;
		}

		if (type == VCA_MV && !is_mv_bios_version_correct(d)) {
			if (d.args.is_force_cmd_enabled()) {
				d.LOG_CPU_WARN("OS booted with wrong BIOS version. You are doing it on your own risk!\n");
			}
			else {
				d.LOG_CPU_ERROR("Booting aborted! Invalid VCA2 BIOS version."
					" Please update to at least \"" BIOS_MV_2_0_VERSION "\" version or newer one.\n");
				d.LOG_CPU(VERBOSE_DEFAULT, "Use '--force' execution mode if you are sure what you try to do.\n");
				return false;
			}
		}

		if (!set_time(cpu_fd, d)){
			d.LOG_CPU_ERROR("Could not set time!\n");
			return false;
		}

		bool boot_res = false;
		if (!file_lbp) {
			d.LOG_CPU(DEBUG_INFO, "TRYING TO BOOT VCABLK0!\n");
			boot_res = try_ioctl_with_blk(cpu_fd, LBP_BOOT_BLKDISK, d);
		} else {
			d.LOG_CPU(DEBUG_INFO, "TRYING TO BOOT LBP!\n");
			boot_res = try_ioctl_with_img(cpu_fd, LBP_BOOT_RAMDISK, d, (void*)file_lbp, info.st_size);
			munmap((void*)file_lbp, info.st_size);
		}

		if (boot_res) {
			d.LOG_CPU(DEBUG_INFO, "EFI OS LOADER COMPLETED!\n");
			start_csm(cpu_fd, d);
		}
		else {
			d.LOG_CPU_ERROR("Boot failed!\n");
			return false;
		}
	} else {
		start_csm(cpu_fd, d);
	}

	if (pass_script(d, mac_addr))
		return config.save_cpu_field(d.card_id, d.cpu_id, "last-os-image", boot_path);

	return false;
}

bool boot_USB(caller_data d)
{
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;
	config.save_cpu_field(d.card_id, d.cpu_id, "last-os-image", "");
	d.LOG_CPU(DEBUG_INFO, "Trying boot from USB!\n");
	enum vca_card_type type = get_card_type(d.card_id);
	unsigned char mac_addr[6] = {0};
	if(type & VCA_PRODUCTION) {

		std::string state;
		state = get_cpu_state(d);
		if(state == get_vca_state_string(VCA_NET_DEV_READY)) {
			d.LOG_CPU(VERBOSE_DEFAULT, "OS ALREADY UP!\n");
			return false;
		}

		if (!open_enabled_blk_devs(d)) {
			d.LOG_CPU_ERROR("Cannot open block devices.\n");
			return false;
		}

		/* Make sure vop device is removed. It is possible that virtio device is removed
		 * while old vop device is still registered. This happens when card reset
		 * was triggered by the card rather than via vcactl */
		stop_csm(cpu_fd, d);

		if (!prepare_cpu(cpu_fd, d))
			return false;

		if (!get_mac_addr(cpu_fd, d, mac_addr))
			return false;

		if (!set_time(cpu_fd, d))
			return false;

		vca_lbp_retval ret;
		if(!csm_ioctl(cpu_fd, LBP_BOOT_FROM_USB, &ret))
			return false;

		if(!check_lbp_state_ok(ret, LBP_BOOT_FROM_USB, d))
			return false;
	}
	start_csm(cpu_fd, d);
	return pass_script(d, mac_addr);
}

bool pwrbtn_boot(caller_data d)
{
	const char *img_path = config.get_cpu_field(d.card_id, d.cpu_id,
				cpu_fields::img_path).get_cstring();

	if (!config.get_global_field(global_fields::auto_boot).get_number()) {
		d.LOG_CPU(DEBUG_INFO, "No autoboot after power button. Autoboot disabled.\n");
		return true;
	}

	if (strcmp(img_path, "") == 0) {
		d.LOG_CPU(DEBUG_INFO, "No autoboot after power button. OS image shall be configured.\n");
		return true;
	}

	char actual_path[PATH_MAX + 1];

	if (strcmp(img_path, BLOCKIO_BOOT_DEV_NAME) != 0) {
		if (!realpath(img_path, actual_path))
			d.LOG_CPU_ERROR("Cannot canonicalize OS image path (got %s): %s!\n",
				img_path, strerror(errno));

		if (!file_exists(actual_path)) {
			d.LOG_CPU_ERROR("Can't access file %s!\n", actual_path);
			return true;
		}
	}
	else
		STRCPY_S(actual_path, img_path, sizeof(actual_path));

	std::string pwrbtn_boot_cmd = ECHO " " VCA_PWRBTN_BOOT_CMD " "
		+ int_to_string(d.card_id) + " "
		+ int_to_string(d.cpu_id) + " "
		+ actual_path + " > "
		MSG_FIFO_FILE;

	if (run_cmd(pwrbtn_boot_cmd.c_str()) == FAIL) {
		LOG_ERROR("Cannot execute: %s\n", pwrbtn_boot_cmd.c_str());
		return false;
	}

	return true;
}

bool boot_gold_bios(caller_data d)
{
	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd)
		return false;

	struct vca_ioctl_desc desc;
	desc.cpu_id = d.cpu_id;
	desc.hold = false;
	if (!vca_ioctl(fd, VCA_ENABLE_GOLD_BIOS, &desc)) {
		d.LOG_CPU_ERROR("Could not initiate BIOS update\n");
		return false;
	}
	//powerdown node to boot it back in golden bios
	if (!hold_power_button(d)) {
		d.LOG_CPU_ERROR("Could not toggle power button\n");
		return false;
	}
#ifndef LINK_DOWN_WORKAROUND
	bool link_up = true;
	while (get_passed_time_ms(start) < NODE_POWERDOWN_TIMEOUT) {
		if (!is_link_up(d)) {
			link_up = false;
			break;
		}
		sleep(1);
	}
	if (link_up) {
		d.LOG_CPU_ERROR("Node powerdown error!\n");
		return false;
	}
#endif
	//boot up node, now with gold bios
	if (!_toggle_power_button(d)) {
		d.LOG_CPU_ERROR("Could not toggle power button\n");
		return false;
	}
	return true;
}

bool setup_standard_bios_update(caller_data d)
{
	std::string state;
	state = get_cpu_state(d);
	if (state != get_vca_state_string(VCA_BIOS_UP) && state != get_vca_state_string(VCA_BIOS_READY)) {
		d.LOG_CPU_ERROR("Card needs to be in \"%s\" or \"%s\" state!\n",
			get_vca_state_string(VCA_BIOS_UP), get_vca_state_string(VCA_BIOS_READY));
	return false;
	}

	check_rcvy_jumper_state(d);

	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;
	if (!prepare_cpu(cpu_fd, d)) {
		d.LOG_CPU_ERROR("Could not prepare cpu!\n");
		return false;
	}
	return true;
}

bool restore_cpu_after_bios_update(caller_data d)
{
	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd)
		return false;
	struct vca_ioctl_desc desc;
	desc.cpu_id = d.cpu_id;
	desc.hold = false;
	if (!vca_ioctl(fd, VCA_DISABLE_GOLD_BIOS, &desc)) {
		d.LOG_CPU_ERROR("Could not disable gold bios\n");
		return false;
	}
	if (!hold_power_button(d))
		return false;
	if (!_toggle_power_button(d))
		return false;
	return true;
}

bool setup_bios_recovery(caller_data d)
{
	if (!(boot_gold_bios(d)))
		return false;

	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;

	bool state = false;
	boost::posix_time::ptime start = get_time();
	do {
		vca_lbp_retval ret;
		if (!csm_ioctl(cpu_fd, LBP_HANDSHAKE, &ret))
			return false;
		if (ret != LBP_STATE_OK) {
			state = false;
		} else {
			state = true;
		}
		if (get_passed_time_ms(start) > GOLD_BIOS_LOAD_TIMEOUT) {
			d.LOG_CPU_ERROR("GOLD BIOS load timeout!\n");
			restore_cpu_after_bios_update(d);
		}
	} while (!state);
	return true;
}

bool set_bios_cfg_for(caller_data &d, filehandle_t fd, vca_csm_ioctl_bios_param_desc desc)
{
	return csm_ioctl(fd, LBP_SET_BIOS_PARAM, &desc) && check_lbp_state_ok(desc.ret, LBP_SET_BIOS_PARAM, d);
}

bool set_bios_cfg_for(caller_data &d, filehandle_t fd, vca_lbp_bios_param param, unsigned long long value)
{
	vca_csm_ioctl_bios_param_desc desc;
	desc.param = param;
	desc.value.value = value;
	return set_bios_cfg_for(d, fd, desc);
}

bool update_bios(caller_data d)
{
	std::string cmd_name = d.get_cmd_name();

	enum vca_card_type type = get_card_type(d.card_id);
	if (type != VCA_MV && type != VCA_FPGA && type != VCA_VCAA && type != VCA_VCGA && cmd_name == RECOVER_BIOS_CMD) {
		LOG_INFO("Recovery BIOS update is supported only on second generation VCA\n");
		return false;
	}

	if (type & VCA_PRODUCTION && (cmd_name != RECOVER_BIOS_CMD)) {
		if (!setup_standard_bios_update(d))
			return false;
	}

	const data_field *file_path_f = d.args(FILE_PATH_ARG);
	if (!file_path_f)
		return false;

	const char *file_path = file_path_f->get_cstring();
	close_on_exit fd(open_path(file_path, O_RDONLY));
	struct stat info;
	if (fstat(fd, &info)) {
		d.LOG_CPU_ERROR("fstat: %s: %s\n", strerror(errno), file_path);
		return false;
	}
	char *file = (char*)mmap(0, info.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (file == MAP_FAILED) {
		d.LOG_CPU_ERROR("Could not open file: %s\n", file_path);
		return false;
	}

	if (cmd_name == RECOVER_BIOS_CMD) {
		if(!setup_bios_recovery(d))
			return false;
	}

	d.LOG_CPU(VERBOSE_DEFAULT, "BIOS UPDATE STARTED. DO NOT POWERDOWN SYSTEM\n");
	fflush(stdout);

	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;

	unsigned long long sgx_value;
	if (is_mv_bios_version_2_0(get_bios_version(d, cpu_fd), BIOS_MV_2_0_VERSION_SGX) && get_bios_cfg_for(d, cpu_fd, VCA_LBP_PARAM_SGX, sgx_value) && sgx_value) {
		if (!set_bios_cfg_for(d, cpu_fd, VCA_LBP_PARAM_SGX, SGX_DISABLED))
			d.LOG_CPU_WARN("Attempt to disable SGX in BIOS configuration failed. Update will continue anyway.\n");
		else
			d.LOG_CPU(DEBUG_INFO, "SGX was disabled. BIOS UPDATE CONTINUED\n");
	}

	if (type & VCA_PRODUCTION) {
		/* reset needed until bios will support unmapping file! */
		if (try_ioctl_with_img(cpu_fd, LBP_FLASH_BIOS, d, (void*)file, info.st_size)) {
			d.LOG_CPU(VERBOSE_DEFAULT, "UPDATE BIOS SUCCESSFUL\n");
			munmap((void*)file, info.st_size);
			if ((cmd_name == RECOVER_BIOS_CMD)) {
				if (!restore_cpu_after_bios_update(d))
					return false;
			}
			else {
				if (type == VCA_MV || type == VCA_FPGA || type == VCA_VCAA || type == VCA_VCGA) {
					press_power_button(d, true);
					sleep(3); // waiting 3 seconds to make sure that CPU power go down
					press_power_button(d, false);
					d.LOG_CPU(VERBOSE_DEFAULT, "Node will be power down and up automatically to make the change active.\n"
						"Please wait for 'bios_up' to start work with the node.\n");
				}
				else
					reset(d);
			}
		}
		else {
			d.LOG_CPU_ERROR("Update bios failed!\n");
			return false;
		}
	} else {
		d.LOG_CPU_ERROR("This type of card does not support update_bios!\n");
		return false;
	}
	return true;
}

bool gold_control(caller_data d)
{
	switch( get_card_type( d.card_id)) {
	case VCA_MV: case VCA_FPGA: case VCA_VCAA: case VCA_VCGA: {
			close_on_exit fd( open_card_fd( d.card_id));
			if( fd) {
				struct vca_ioctl_desc desc;
				desc.cpu_id = d.cpu_id;
				desc.hold = false;
				const std::string subcmd = d.args.get_arg( SUBCMD);
				if( subcmd == "on") {
					if( vca_ioctl(fd, VCA_ENABLE_GOLD_BIOS, &desc))
						return true;
					d.LOG_CPU_ERROR( "Could not enable gold bios\n");
				} else if( subcmd == "off") {
					if( vca_ioctl(fd, VCA_DISABLE_GOLD_BIOS, &desc))
						return true;
					d.LOG_CPU_ERROR( "Could not disable gold bios\n");
				} else
					LOG_ERROR( "unknown subcommand %s\n", subcmd.c_str());
			}
		}
		break;
	default:
		d.LOG_CPU_WARN( "Turning on/off gold BIOS via software is not supported\n");
	}
	return false;
}

bool update_img_file_with_mac(const char * mac)
{
	FILE *fp = fopen(VCA_UPDATE_MAC_SET_MAC_PATH, "w");
	if (!fp) {
		LOG_ERROR("could not create/write to %s\n", VCA_UPDATE_MAC_SET_MAC_PATH);
		return false;
	}

	std::string file_content = std::string("set EEMAC ") + mac[0] + mac[1] +
		mac[3] + mac[4] + mac[6] + mac[7] + mac[9] + mac[10] + mac[12] + mac[13] +
		mac[15] + mac[16];

	fprintf(fp, "%s", file_content.c_str());
	fclose(fp);

	if (!file_exists(VCA_UPDATE_MAC_IMG_PATH)) {
		LOG_ERROR("Can't access file %s!\n", VCA_UPDATE_MAC_IMG_PATH);
		return false;
	}

	/* Added option '-D o' (Overwrites primary names by default) instead of using 'mdel'.
	 * 'mdel' tried to delete not existing file which caused (expected) error */
	std::string copy_cmd(std::string("mcopy -D o -i") + VCA_UPDATE_MAC_IMG_PATH
			     + " " + VCA_UPDATE_MAC_SET_MAC_PATH + " ::EFI/BOOT/setmacev.nsh");

	if (run_cmd(copy_cmd.c_str()) == FAIL) {
		LOG_ERROR("Cannot execute: %s\n", copy_cmd.c_str());
		return false;
	}

	return true;
}


bool update_img_file_with_sn(const char * sn)
{
	FILE *fp = fopen(VCA_SET_SERIAL_NUMBER_PATH, "w");
	if (!fp) {
		LOG_ERROR("could not create/write to %s\n", VCA_SET_SERIAL_NUMBER_PATH);
		return false;
	}

	std::string file_content = std::string("set SS ") + sn;
	fprintf(fp, "%s", file_content.c_str());
	fclose(fp);
	if (!file_exists(VCA_SET_SERIAL_NR_IMG_PATH)) {
		LOG_ERROR("Can't access file %s!\n", VCA_SET_SERIAL_NR_IMG_PATH);
		return false;
	}

	/* Added option '-D o' (Overwrites primary names by default) instead of using 'mdel'.
	 * 'mdel' tried to delete not existing file which caused (expected) error */
	std::string copy_cmd(std::string("mcopy -D o -i") + VCA_SET_SERIAL_NR_IMG_PATH
			     + " " + VCA_SET_SERIAL_NUMBER_PATH + " ::EFI/BOOT/setserialnr.nsh");

	if (run_cmd(copy_cmd.c_str()) == FAIL) {
		LOG_ERROR("Cannot execute: %s\n", copy_cmd.c_str());
		return false;
	}

	return true;
}

bool update_mac_addr(caller_data d)
{
	if (!is_root()) {
		LOG_ERROR("Need to be root to perform this command!\n");
		return false;
	}

	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;

	enum vca_card_type type = get_card_type(d.card_id);
	if(type & VCA_PRODUCTION) {

		std::string state;
		state = get_cpu_state(d);
		if(state != get_vca_state_string(VCA_BIOS_UP) && state != get_vca_state_string(VCA_BIOS_READY)) {
			d.LOG_CPU_ERROR("Card needs to be in \"%s\" or \"%s\" state!\n",
				get_vca_state_string(VCA_BIOS_UP), get_vca_state_string(VCA_BIOS_READY));
			return false;
		}

		d.LOG_CPU(DEBUG_INFO, "Updating img file with mac!\n");
		const data_field * mac_addr_f = d.args(MAC_ADDR_ARG);
		if (!mac_addr_f)
			return false;

		if (!update_img_file_with_mac(mac_addr_f->get_cstring()))
			return false;

		if (!prepare_cpu(cpu_fd, d)) {
			d.LOG_CPU_ERROR("Could not prepare cpu!\n");
			return false;
		}

		close_on_exit fd(open_path(VCA_UPDATE_MAC_IMG_PATH));
		if (!fd)
			return false;

		size_t f_sz = get_file_size(VCA_UPDATE_MAC_IMG_PATH);
		if (f_sz > IMG_MAX_SIZE)
			return false;

		char *buffer = new char[f_sz];
		if (buffer == NULL)
			return false;

		if (read(fd, buffer, f_sz) < 0) {
			d.LOG_CPU_ERROR("could not read from %s\n", VCA_UPDATE_MAC_IMG_PATH);
			delete[] buffer;
			return false;
		}

		/* reset needed until bios will support unmapping file! */
		if(try_ioctl_with_img(cpu_fd, LBP_UPDATE_MAC_ADDR, d, (void*)buffer, f_sz)) {
			d.LOG_CPU(DEBUG_INFO, "UPDATE MAC SUCCESSFUL!\n");
			reset(d);
		}
		else {
			d.LOG_CPU_ERROR("Update mac failed!\n");
			delete[] buffer;
			return false;
		}
	} else {
		d.LOG_CPU_ERROR("This type of card does not support update_mac!\n");
		return false;
	}
	return true;
}

bool set_serial_number(caller_data d)
{
	if (!is_root()) {
		LOG_ERROR("Need to be root to perform this command!\n");
		return false;
	}
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;
	enum vca_card_type type = get_card_type(d.card_id);
	if (type & (VCA_MV | VCA_FPGA | VCA_VCAA | VCA_VCGA)) {

		std::string state = get_cpu_state(d);
		if (state != VCA_BIOS_UP_TEXT && state != VCA_BIOS_READY_TEXT) {
			d.LOG_CPU_ERROR("Card needs to be in \"%s\" or \"%s\" state!\n",
				VCA_BIOS_UP_TEXT, VCA_BIOS_READY_TEXT);
			return false;
		}
		d.LOG_CPU(DEBUG_INFO, "Updating img file with mac!\n");
		const data_field * serial_nr_f = d.args(SN_ARG);
		if (!serial_nr_f)
			return false;
		if (!update_img_file_with_sn(serial_nr_f->get_cstring()))
			return false;
		if (!prepare_cpu(cpu_fd, d)) {
			d.LOG_CPU_ERROR("Could not prepare cpu!\n");
			return false;
		}
		close_on_exit fd(open_path(VCA_SET_SERIAL_NR_IMG_PATH));
		if (!fd)
			return false;
		struct stat fstat_buffer;
		if (fstat(fd, &fstat_buffer)) {
			d.LOG_CPU_ERROR("fstat: %s: %s\n", strerror(errno), VCA_SET_SERIAL_NR_IMG_PATH);
			return false;
		}
		if (fstat_buffer.st_size > IMG_MAX_SIZE)
			return false;
		void *buffer = mmap(0, fstat_buffer.st_size, PROT_READ, MAP_SHARED, fd, 0);
		if (buffer == MAP_FAILED)
			return false;
		/* reset needed until bios will support unmapping file! */
		bool ioctl_ok = try_ioctl_with_img(cpu_fd, LBP_SET_SERIAL_NR, d, buffer, fstat_buffer.st_size);
		munmap(buffer, fstat_buffer.st_size);
		if (ioctl_ok) {
			d.LOG_CPU(DEBUG_INFO, "SETTING SERIAL NR SUCCESSFUL!\n");
			reset(d);
		}
		else {
			d.LOG_CPU_ERROR("Setting serial nr failed!\n");
		}
		return ioctl_ok;
	}
	else {
		d.LOG_CPU_ERROR("This type of card does not support setting sn!\n");
		return false;
	}
	return true;
}


static std::string crc_pair_str(vca::crc_pair crcs)
{
	std::stringstream message;
	message << "CRC1: 0x";
	message << std::setfill('0') << std::setw(8);
	message << std::hex << crcs.first;
	message << " CRC2: 0x";
	message << std::setfill('0') << std::setw(8);
	message << std::hex << crcs.second;
	return message.str();
}


bool is_eeprom_compatible(vca::EepromEntry entry, unsigned card_id)
{
	enum vca_card_type type = get_card_type(card_id);
	LOG_DEBUG("Card type in file: %x\tCard type at device: %x\n", entry.card_gen, type);
	return entry.card_gen == type;
}

bool accept_any_eeprom(vca::EepromEntry entry, unsigned card_id) {
	if (!is_eeprom_compatible(entry, card_id))
		LOG_WARN("Accepting incompatible eeprom, Card type in file: %x\tCard type at device: %x\n", entry.card_gen, get_card_type(card_id));
	return true;
}

uint32_t read_eeprom_crc(std::vector<char> const& eeprom)
{
	unsigned size = eeprom.size();
	if (size < 4 )
		throw std::bad_cast();

	uint32_t crc = (eeprom[size-4])     & 0x000000ff;
	crc = crc + ((eeprom[size-3] << 8)  & 0x0000ffff);
	crc = crc + ((eeprom[size-2] << 16) & 0x00ffffff);
	crc = crc + (eeprom[size-1] << 24);
	return crc;
}


static bool check_if_eeproms_crc_is_allowed(vca::crc_pair candidate, bool card_has_two_eeproms)
{
	for (auto known : vca::known_eeproms) {
		if (!card_has_two_eeproms)
			candidate.second = known.crcs.second;
		if (candidate == known.crcs)
			return true;
	}
	return false;
}

bool check_if_card_state_is_ok_to_update_eeprom(caller_data d)
{
	std::map<unsigned int, std::string> card = get_card_state(d);
	std::string state;
	for (unsigned int i = 0; i < card.size(); i++) {
		state = card[i];
		LOG_DEBUG("state %s for node %d\n", state.c_str(), i);
		if (state == LINK_DOWN_STATE || state == get_vca_state_string(VCA_BIOS_DOWN) ||
		    state == get_vca_state_string(VCA_BOOTING) || state == get_vca_state_string(VCA_FLASHING) ||
		    state == get_vca_state_string(VCA_RESETTING) || state == get_vca_state_string(VCA_BUSY) ||
		    state == get_vca_state_string(VCA_DONE) || state == get_vca_state_string(VCA_ERROR) ||
		    state == get_vca_state_string(VCA_DRV_PROBE_ERROR)) {
			return false;
		}
	}
	return true;
}

bool update_eeprom(caller_data d)
{
	vca::eeprom_binaries eeprom_bin;
	struct vca::EepromEntry valid_entry;
	enum vca_card_type card_type = get_card_type(d.card_id);

	if (!is_root()) {
		LOG_ERROR("Need to be root to perform this command!\n");
		return false;
	}

	if (!check_if_card_state_is_ok_to_update_eeprom(d)) {
		if (!d.args.is_force_cmd_enabled()) {
			LOG_ERROR("Cannot update EEPROM for card %d. Wrong state of one of the nodes!\n", d.card_id);
			LOG_INFO("Use '--force' execution mode if you are sure what you try to do.\n");
			return false;
		}
	}

	// E3S10, VCAA, VCGA have only one eeprom
	bool card_has_two_eeproms = !(card_type & (VCA_FPGA | VCA_VCAA | VCA_VCGA));

	try {
		const data_field *file_path = d.args(FILE_PATH_ARG);
		vca::EepromFile file(file_path->get_cstring());
		if (!file.is_open()) {
			return false;
		}

		std::vector<vca::EepromEntry> valid_entries = file.get_entries(
			d.args.needs_card_type_check() ? is_eeprom_compatible : accept_any_eeprom,
			d.card_id);
		if (valid_entries.size() != 1) {
			LOG_ERROR("Found %lu matching eeprom binary entries for card %d in input file, but exactly 1 is required.\n",
				(unsigned long) valid_entries.size(), d.card_id);
			if (valid_entries.empty() && d.args.needs_card_type_check())
				LOG_WARN("Consider using `--skip-card-type-check' option (see help for usage info)\n");
			return false;
		}
		valid_entry = valid_entries[0];
		eeprom_bin = file.get_binary_eeproms(valid_entry);
	} catch (std::ifstream::failure &e) {
		LOG_ERROR("%s\n", e.what());
		return false;
	} catch (std::bad_cast &e) {
		LOG_ERROR("%s\n", e.what());
		return false;
	} catch (std::runtime_error &e) {
		LOG_ERROR("%s\n", e.what());
		return false;
	} catch ( ... ) {
		LOG_ERROR("EEPROM update aborted!\n");
		return false;
	}

	vca::crc_pair crcs = std::make_pair(
				read_eeprom_crc(eeprom_bin.first),
				read_eeprom_crc(eeprom_bin.second));
	if (!check_if_eeproms_crc_is_allowed(crcs, card_has_two_eeproms)) {
		std::stringstream message;
		message << "EEPROM pair is unknown: " << crc_pair_str(crcs) << ".";
		if (!d.args.is_force_cmd_enabled()) {
			message << " Update aborted.\n";
			LOG_ERROR("%s", message.str().c_str());
			return false;
		} else {
			message << std::endl;
			LOG_WARN("%s", message.str().c_str());
		}
	}

	LOG_INFO("Update EEPROM process started (for card %d). "
		 "Do not power down system!\n", d.card_id);

	close_on_exit fd(open_card_fd(d.card_id));
	if (!fd) {
		return false;
	}

	if (vca_plx_eep_ioctl_with_bin(fd, &eeprom_bin.first.front(),
				       valid_entry.first_eeprom_size) == SUCCESS) {
		LOG_INFO("Update EEPROM for first PCIe switch successful!\n");
	}
	else {
		LOG_ERROR("Update EEPROM for first PCIe switch failed!\n");
		return false;
	}

	if (card_has_two_eeproms) {
		close_on_exit fd_extd(open_extd_card_fd(d.card_id));
		if (!fd_extd) {
			return false;
		}

		if (vca_plx_eep_ioctl_with_bin_mgr_extd(fd_extd, &eeprom_bin.second.front(),
							valid_entry.second_eeprom_size) == SUCCESS) {
			LOG_INFO("Update EEPROM for second PCIe switch successful!\n");
		}
		else {
			LOG_ERROR("Update EEPROM for second PCIe switch failed!\n");
			return false;
		}
	}

	LOG_INFO("Update EEPROM successful (for card %d). "
		"Reboot system is required to reload EEPROM.\n", d.card_id);

	return true;
}

bool clear_smb_event_log(caller_data d)
{
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd)
		return false;

	enum vca_card_type type = get_card_type(d.card_id);
	if (type & VCA_PRODUCTION) {

		std::string state;
		state = get_cpu_state(d);
		if (state != get_vca_state_string(VCA_BIOS_UP) && state != get_vca_state_string(VCA_BIOS_READY)) {
			d.LOG_CPU_ERROR("Card needs to be in \"%s\" or \"%s\" state!\n",
				get_vca_state_string(VCA_BIOS_UP), get_vca_state_string(VCA_BIOS_READY));
			return false;
		}

		close_on_exit fd(open_path(VCA_CLEAR_SMB_EVENT_LOG_PATH));
		if (!fd)
			return false;

		size_t f_sz = get_file_size(VCA_CLEAR_SMB_EVENT_LOG_PATH);
		if (f_sz > IMG_MAX_SIZE)
			return false;

		char *buffer = new char[f_sz];
		if (buffer == NULL)
			return false;

		if (read(fd, buffer, f_sz) < 0) {
			d.LOG_CPU_ERROR("could not read from %s\n", VCA_CLEAR_SMB_EVENT_LOG_PATH);
			delete[] buffer;
			return false;
		}

		if (!prepare_cpu(cpu_fd, d)) {
			d.LOG_CPU(DEBUG_INFO, "Preparing card failed!\n");
			delete[] buffer;
			return false;
		}

		/* reset needed until bios will support unmapping file! */
		if (try_ioctl_with_img(cpu_fd, LBP_CLEAR_SMB_EVENT_LOG, d, (void*)buffer, f_sz)) {
			d.LOG_CPU(DEBUG_INFO, "Clear SMB event log successful!\n");
			reset(d);
		}
		else {
			d.LOG_CPU_ERROR("Clear SMB event log failed!\n");
			delete[] buffer;
			return false;
		}
	}
	else {
		d.LOG_CPU_ERROR("This type of card does not support clear-SMB-event-log!\n");
		return false;
	}
	return true;
}

bool script(caller_data d)
{
	const data_field * file_path = d.args(FILE_PATH_ARG);
	bool ret;

	if (file_path) {
		ret = config.save_cpu_field(d.card_id, d.cpu_id,
			config.get_cpu_field(d.card_id, d.cpu_id,
			cpu_fields::script_path).get_name(),
			file_path->get_cstring());
	}
	else {
		ret = config.save_cpu_field(d.card_id, d.cpu_id,
			config.get_cpu_field(d.card_id, d.cpu_id,
			cpu_fields::script_path).get_name(),
			"");
	}

	return ret;
}

void print_padding(int padding_level)
{
	while (padding_level) {
		printf(" ");
		padding_level--;
	}
}

void print_field(const data_field & data, int spaces) {
	print_padding(spaces);
	if(data.get_type() == data_field::number)
		printf("%s: %d\n", data.get_name(), data.get_number());
	else
		printf("%s: %s\n", data.get_name(), data.get_cstring());
}

void print_blk_config_fields(caller_data d, int padding)
{
	for (int j = 0; j < MAX_BLK_DEVS; j++) {
		if (config.blk_dev_exist(d.card_id, d.cpu_id, j)) {
			print_padding(padding);
			printf("%s%d:\n", BLK_CONFIG_NAME, j);
			for (int k = 0; k < blk_dev_fields::ENUM_SIZE; k++) {
				const data_field blk_data = config.get_blk_field(d.card_id, d.cpu_id, j, (blk_dev_fields::_enum)k);
				print_field(blk_data, padding + 2);
			}
		}
	}
}

bool config_show(caller_data d)
{
	const data_field * param_name = d.args(CONFIG_PARAM_NAME);
	if (param_name) {
		if (d.caller_id == 0) {
			if (config.is_field_global(param_name->get_cstring())) {
				const data_field * const field = config.get_global_field(param_name->get_cstring());
				if (!field)
					return false;
				printf("%s\n", field->get_cstring());
			}
		}
		if (config.is_field_cpu(param_name->get_cstring())) {
			const data_field * const field = config.get_cpu_field(d.card_id, d.cpu_id, param_name->get_cstring());
			if (!field)
				return false;

			if (strcmp(param_name->get_cstring(), config.get_cpu_field(d.card_id, d.cpu_id, cpu_fields::block_devs).get_name()) == 0) {
				printf("card %d cpu %d: \n", d.card_id, d.cpu_id);
				print_blk_config_fields(d, 2);
				return true;
			}

			const data_field * strip_param = d.args(STRIP_PARAM);
			if (!strip_param) {
				printf("card %d cpu %d: \n", d.card_id, d.cpu_id);
				print_field(*field, 2);
			}
			else
				printf("%s\n", field->get_cstring());

		}
		// for vcablkN fields
		else {
			for (int j = 0; j < MAX_BLK_DEVS; j++) {
				if (config.blk_dev_exist(d.card_id, d.cpu_id, j)) {
					if (strcmp(param_name->get_cstring(), get_block_dev_name_from_id(j).c_str()) == 0) {
						printf("card %d cpu %d: \n", d.card_id, d.cpu_id);
						printf("  %s:\n", get_block_dev_name_from_id(j).c_str());
						for (int k = 0; k < blk_dev_fields::ENUM_SIZE; k++) {
							const data_field blk_data = config.get_blk_field(d.card_id, d.cpu_id, j, (blk_dev_fields::_enum)k);
							print_field(blk_data, 4);
						}
					}
				}
			}
		}
	}
	else {
		if(d.caller_id == 0) {
			printf("Global configuration:\n");
			for(int i = 0; i < global_fields::ENUM_SIZE; i++)
				print_field(config.get_global_field((global_fields::_enum)i), 2);
		}

		printf("card %d cpu %d: \n", d.card_id, d.cpu_id);
		for (int i = 0; i < cpu_fields::ENUM_SIZE; i++) {
			const data_field cpu_data = config.get_cpu_field(d.card_id, d.cpu_id, (cpu_fields::_enum)i);
			if (cpu_data.get_name() != config.get_cpu_field(d.card_id, d.cpu_id, cpu_fields::block_devs).get_name()) {
				print_field(cpu_data, 2);
			}
			else {
				printf("  %s:\n", cpu_data.get_name());
				print_blk_config_fields(d, 4);
			}
		}
	}
	return true;
}

bool config_change(caller_data d)
{
	const data_field * param_name = d.args(CONFIG_PARAM_NAME);
	const data_field * param_val = d.args(CONFIG_PARAM_VALUE);
	const data_field * blk_id_f = d.args(BLOCKIO_ID_ARG);
	const data_field * card_id_f = d.args(CARD_ID_ARG);
	const data_field * cpu_id_f = d.args(CPU_ID_ARG);
	bool ret;
	std::string param_name_string = param_name->get_string();
	std::string global_fields_parameters[3] = {"auto-boot", "debug-enabled", "va-min-free-memory-enabled"};
	if (d.caller_id == 0) {
		if (!config.contains_field(param_name->get_cstring())) {
			LOG_ERROR("invalid parameter name: %s\n", param_name->get_cstring());
			return false;
		}
		if (config.is_field_global(param_name->get_cstring())) {
			for (int i = 0; i < 3; i++)
				if (param_name_string == global_fields_parameters[i])
					if (!is_correct_parameter(param_val->get_cstring(), 0, 1))
						LOG_ERROR("Parameter should be in a range <0, 1>!\n");
			ret = config.save_global_field(param_name->get_cstring(),
				param_val->get_cstring());

			return ret;
		}
	}
	if (strcmp(param_name->get_cstring(), config.get_cpu_field(d.card_id, d.cpu_id,
		cpu_fields::va_free_mem).get_name()) == 0) {
		if (!is_correct_parameter(param_val->get_cstring(), 0, 1)) {
			LOG_ERROR("Parameter should be in a range <0, 1>!\n");
			return false;
		}
	}
	if (strcmp(param_name->get_cstring(), config.get_cpu_field(d.card_id, d.cpu_id,
		cpu_fields::ip).get_name()) == 0) {
		if (strcmp(param_val->get_cstring(), DHCP_STRING)) {
			if (!card_id_f || !cpu_id_f) {
				LOG_ERROR("card_id and cpu_id is required to change config field \"ip\"!\n");
				return false;
			}
			if (!is_ip_address(param_val->get_cstring())) {
				LOG_ERROR("Ip address incorrect!\n");
				return false;
			}
		}
	}
	if (strcmp(param_name->get_cstring(), config.get_cpu_field(d.card_id, d.cpu_id,
		cpu_fields::mask).get_name()) == 0) {
		if (!is_correct_parameter(param_val->get_cstring(), 1, 32)) {
				LOG_ERROR("Mask value should be in a range <1,32>!\n");
				return false;
		}
	}
	if (config.is_field_cpu(param_name->get_cstring())) {
		ret = config.save_cpu_field(d.card_id, d.cpu_id,
					     param_name->get_cstring(), param_val->get_cstring());
		return ret;
	}
	if (config.is_field_blk(param_name->get_cstring()) && blk_id_f) {
		if (!config.blk_dev_exist(d.card_id, d.cpu_id, blk_id_f->get_number()))
			config.add_blk_dev_fields(d.card_id, d.cpu_id, blk_id_f->get_number());

		return config.save_blk_field(d.card_id, d.cpu_id, blk_id_f->get_number(),
			param_name->get_cstring(), param_val->get_cstring());
	}

	return true;
}

bool config_use(caller_data d)
{
	if (d.caller_id == 0) {
		int const f= open( MSG_FIFO_FILE, O_WRONLY|O_NONBLOCK);
		if( -1!= f) {
			int const w= write(f, VCA_CONFIG_USE_CMD "\n", sizeof(VCA_CONFIG_USE_CMD));
			close(f);
			if( sizeof(VCA_CONFIG_USE_CMD)== w)
				return true;
			else LOG_ERROR("Error write to " MSG_FIFO_FILE "\n");
		}
		else LOG_ERROR("Error open " MSG_FIFO_FILE ". Check `systemctl status vcactl` \n");
	}
	return false;
}

bool config_default(caller_data d)
{
	if(!close_on_exit( open_blk_fd( d.card_id, d.cpu_id)) || is_any_blkdev_enabled( d))
		return false;

	return config.save_default_config();
}

bool read_temp(caller_data d)
{
    bool ret;
    std::string output;

    ret = vca_agent_command(d, output, VCA_AGENT_SENSORS);

    if (is_os_windows(get_cpu_os(d)) && ret)
    {
        std::istringstream iss_output(output);
        std::string temp_cores[5];

        int i = 0;
        while (std::getline(iss_output, temp_cores[i]))
        {
            i++;
            if (i > 4)
                break;
        }
        for (i = 0; i < 4; i++) {
            temp_cores[i].insert(0, "+");
            if (temp_cores[i].size() < 4) { temp_cores[i].insert(0, " "); } //unification of windows and linux messages
                                                                            //additional space before 2-digit temperature reading
        }
        printf("Card %d Cpu %d\nCore 0:        %s.0\u00B0C  (high = +%s.0\u00B0C,"
            " crit = +%s.0\u00B0C)\nCore 1:        %s.0\u00B0C  (high = +%s.0\u00B0C,"
            " crit = +%s.0\u00B0C)\nCore 2:        %s.0\u00B0C  (high = +%s.0\u00B0C,"
            " crit = +%s.0\u00B0C)\nCore 3:        %s.0\u00B0C  (high = +%s.0\u00B0C,"
            " crit = +%s.0\u00B0C)\n",
            d.card_id, d.cpu_id, temp_cores[0].c_str(), temp_cores[4].c_str(),
            temp_cores[4].c_str(), temp_cores[1].c_str(), temp_cores[4].c_str(),
            temp_cores[4].c_str(), temp_cores[2].c_str(), temp_cores[4].c_str(),
            temp_cores[4].c_str(), temp_cores[3].c_str(), temp_cores[4].c_str(),
            temp_cores[4].c_str());
    }
    else
    {
        if (ret){
            for (int i = 0; i < 3; i++)
                output.erase(0, output.find("\n") + 1);     //unification of windows and linux messages
                                                            //first 3 lines removal for linux standard temperature report
            const char *out_cstr = output.c_str();
            assert(!out_cstr[output.size()]); // tell klocwork the obvious...
            printf("Card %d Cpu %d:\n%s\n", d.card_id, d.cpu_id, out_cstr);
        }

        else
            LOG_ERROR("Execute 'temp' command failed!\n");

    }
    return ret;
}

std::string extract_network_data(std::string output, std::string kind)
{
	std::string begin_pattern;
	std::string end_pattern;

	if (kind == NETWORK_SUBCMD_IP) {
		begin_pattern = "inet ";
		end_pattern = "/";
	}
	else if (kind == NETWORK_SUBCMD_IP6) {
		begin_pattern = "inet6 ";
		end_pattern = "/";
	}
	else if (kind == NETWORK_SUBCMD_MAC) {
		begin_pattern = "ether ";
		end_pattern = " brd";
	}

	int begin_pattern_pos = output.find(begin_pattern);
	if (begin_pattern_pos == -1) {
		LOG_ERROR("%s not found", kind.c_str());
		return "";
	}
	// tmp_output needed because there is more than one end_pattern in original output
	std::string tmp_output = output.substr(begin_pattern_pos, output.size() - begin_pattern_pos);
	int end_pattern_pos = tmp_output.find(end_pattern);
	if (end_pattern_pos == -1) {
		LOG_ERROR("%s not found", kind.c_str());
		return "";
	}

	return tmp_output.substr(begin_pattern.size(), end_pattern_pos - begin_pattern.size());
}

void parse_network_info(caller_data d, std::string subcmd, std::string &output)
{
	if (d.args.size() < 3)
		printf("Card %d Cpu %d:\n", d.card_id, d.cpu_id);

	if (subcmd == NETWORK_SUBCMD_ALL || subcmd == NETWORK_SUBCMD_STATS) {
		std::string pattern = ": ";
		int shift = output.find_first_of(pattern);
		printf("%s\n", output.substr(shift + pattern.size(), output.size() - (shift + pattern.size())).c_str());
	}
	else if (subcmd == NETWORK_SUBCMD_IP || subcmd == NETWORK_SUBCMD_IP6 || subcmd == NETWORK_SUBCMD_MAC)
		printf("%s\n", extract_network_data(output, subcmd).c_str());
	else if (subcmd == NETWORK_SUBCMD_RENEW || subcmd == NETWORK_SUBCMD_VM_MAC)
		printf("%s\n", output.c_str());
	else
		printf("Wrong command: %s\n", subcmd.c_str());
}

void parse_network_info_windows(caller_data d, std::string subcmd, std::string &output)
{
	if (d.args.size() < 3)
		printf("Card %d Cpu %d:\n", d.card_id, d.cpu_id);

	std::istringstream input(output);
	std::string line;

	if (subcmd == NETWORK_SUBCMD_ALL || subcmd == NETWORK_SUBCMD_STATS ||
	    subcmd == NETWORK_SUBCMD_RENEW || subcmd == NETWORK_SUBCMD_VM_MAC ||
	    subcmd == NETWORK_SUBCMD_IP6) {
		printf("%s\n", output.c_str());
	}
	else if (subcmd == NETWORK_SUBCMD_IP) {
		while (std::getline(input, line))
		{
			if (is_ip_address(line.c_str()))
				printf("%s\n", line.c_str());
		}
	}
	else if	(subcmd == NETWORK_SUBCMD_MAC) {
		while (std::getline(input, line))
		{
			if (line.length() == 17)
				printf("%s\n", line.c_str());
			// TODO add better mac validation
		}
	}
	else
		printf("Wrong command: %s\n", subcmd.c_str());
}

bool get_network_info(caller_data d)
{
	bool ret;
	std::string output;
	std::string subcmd;

	const data_field *subcmd_f = d.args(SUBCMD);
	if (subcmd_f)
		subcmd = subcmd_f->get_cstring();

	if (subcmd == NETWORK_SUBCMD_STATS)
		ret = vca_agent_command(d, output, VCA_AGENT_IP_STATS);
	else if (subcmd == NETWORK_SUBCMD_RENEW) {
		const char *ip = get_cpu_field_if_not_empty(d, cpu_fields::ip);
		if (!ip)
			return false;
		if (strncasecmp(DHCP_STRING, ip, strlen(DHCP_STRING))) {
			LOG_ERROR("Cannot renew DHCP lease on node with static IP assigned\n");
			return false;
		}
		ret = vca_agent_command(d, output, VCA_AGENT_RENEW);
	}
	else if (subcmd == NETWORK_SUBCMD_VM_MAC) {
		ret = vca_agent_command(d, output, VCA_AGENT_VM_MAC);
	}
	else
		ret = vca_agent_command(d, output, VCA_AGENT_IP);

	if (ret) {
		if (is_os_windows(get_cpu_os(d)))
			parse_network_info_windows(d, subcmd, output);
		else
			parse_network_info(d, subcmd, output);
	}
	else
		LOG_ERROR("Execute " NETWORK_CMD " command failed!\n");

	return ret;
}

struct Eeprom1 {
	unsigned crc;
	const char* version;
	Eeprom1(unsigned card_id) {
		crc = 0;
		close_on_exit cpu_fd1(open_cpu_fd(card_id, 0));
		if(cpu_fd1)
			if(csm_ioctl(cpu_fd1, VCA_READ_EEPROM_CRC, &crc))
				for(const vca::eeprom_config_info* i= vca::known_eeproms + sizeof(vca::known_eeproms)/sizeof(*vca::known_eeproms) ;
						vca::known_eeproms <= i; --i)
					if(i->crcs.first==crc) { version=i->version ; return;}
		version= "unknown";
	}
};

struct Eeprom2 {
	vca::crc_pair crc;
	const char* version;
	Eeprom2(unsigned card_id){
		close_on_exit cpu_fd1(open_cpu_fd(card_id, 0));
		close_on_exit cpu_fd2(open_cpu_fd(card_id, 2));
		if(cpu_fd1 && cpu_fd2)
			if( csm_ioctl(cpu_fd1, VCA_READ_EEPROM_CRC, &crc.first) && csm_ioctl(cpu_fd2, VCA_READ_EEPROM_CRC, &crc.second) )
				for(const vca::eeprom_config_info* i= vca::known_eeproms + sizeof(vca::known_eeproms)/sizeof(*vca::known_eeproms) ;
						vca::known_eeproms <= i; --i)
					if(i->crcs==crc) { version=i->version ; return;}
		version= "unknown";
	}
};

std::string get_card_serial_num(caller_data cdata)
{
	std::string out;
	const int nodes = get_last_cpu(cdata.card_id) + 1;
	for (int i = 0; i < nodes; ++i) {
		cdata.cpu_id = i;
		if (is_agent_ready(cdata)) {
			if (vca_agent_command(cdata, out, VCA_AGENT_SN_INFO)) {
				return out;
			} else {
				cdata.LOG_CPU(DEBUG_INFO, "vca_agent_command failed\n");
			}
		} else {
			// TODO: try to use SMBIOS TABLE here
			cdata.LOG_CPU(DEBUG_INFO, "Node is not ready to read SerialNumber\n");
		}
	}
	return "not-yet-readable";
}

static bool print_hw_info(caller_data callerData)
{
	vca_card_type const type= get_card_type(callerData.card_id);
	const std::string serial = get_card_serial_num(callerData);
	switch(type) {
	case VCA_FPGA_FAB1:
	case VCA_VCAA_FAB1:
	case VCA_VCGA_FAB1: { // single eeprom
		Eeprom1 version(callerData.card_id);
		printf("Card %d:\t%s,\tEEPROM version: %s (CRC:%08x),\tSerial Number: %s\n",
			callerData.card_id,
			get_card_gen(callerData.card_id).c_str(),
			version.version, version.crc,
			serial.c_str()
		);
		return true;
		}
	case VCA_MV_FAB1: // double eeprom
	case VCA_VV_FAB2:
	case VCA_VV_FAB1: {
		Eeprom2 version(callerData.card_id);
		printf("Card %d:\t%s,\tEEPROM version: %s (CRC1:%08x CRC2:%08x),\tSerial Number: %s\n",
			callerData.card_id,
			get_card_gen(callerData.card_id).c_str(),
			version.version, version.crc.first, version.crc.second,
			serial.c_str()
		);
		return true;
		}
	default:
		fprintf(stderr, "Card %d: unknown(%x)\n", callerData.card_id, type);
	}
	return true;
}

static bool print_system_info(caller_data callerData)
{
	const char *sgx_support = "";
#ifdef SGX
	sgx_support = ", SGX support compiled-in";
#endif
	printf("Apps build: %s%s\n", BUILD_VERSION, sgx_support);

	std::string modulesVersion = get_modules_version(0);
	printf("Modules build: %s\n", modulesVersion.c_str());

	std::string kernelVersion = get_kernel_version();
	printf("Kernel version: %s\n", kernelVersion.c_str());

	return true;
}

static bool print_node_os_info(caller_data d)
{
	if (d.args.size() < 3)
		printf("Card %d Cpu %d:\n", d.card_id, d.cpu_id);

	std::string os_type = get_cpu_os(d);

	if (!os_type.empty()){
		if (os_type == "linux"){
			std::string os_release;
			bool ret = vca_agent_command(d, os_release, VCA_AGENT_OS_INFO);
			if (ret)
				printf("%s\n", os_release.c_str());
			 else
				printf("%s - cannot get more info about node-os\n", os_type.c_str());
			return ret;
		}
		else{
			printf("%s\n", os_type.c_str());
			return true;
		}
	}
	else{
		d.LOG_CPU_ERROR(VCA_OS_TYPE_UNKNOWN_TEXT"\n");
		return false;
	}
}

static bool print_node_uuid(caller_data d)
{
	if (d.args.size() < 3)
		printf("Card %d Cpu %d:\n", d.card_id, d.cpu_id);

	std::string node_cpu_uuid;
	bool ret = vca_agent_command(d, node_cpu_uuid, VCA_AGENT_CPU_UUID);

	if (ret)
		printf("%s\n", node_cpu_uuid.c_str());
	else
		printf("%s - cannot get more info about node-CPU_UUID\n", node_cpu_uuid.c_str());
	return ret;
}

static bool print_node_stats(caller_data d)
{
	if (d.args.size() < 3)
		printf("Card %d Cpu %d:\n", d.card_id, d.cpu_id);

	std::string node_stats;
	bool ret = vca_agent_command(d, node_stats, VCA_AGENT_NODE_STATS);

	if (ret)
		printf("%s\n", node_stats.c_str());
	else
		d.LOG_CPU_ERROR("%s - cannot get more node statistics\n", node_stats.c_str());
	return ret;
}

bool print_node_bios_date_info(caller_data d, close_on_exit& cpu_fd)
{
	vca_csm_ioctl_bios_param_desc buffer;
	buffer.param = VCA_LBP_BIOS_PARAM_BUILD_DATE;
	if (csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &buffer))
		if (LBP_STATE_OK == buffer.ret)	{
			printf("\tRelease Date:\t%04u.%02u.%02u %02u:%02u:%02u\n",
				   (unsigned)buffer.value.time.year,
				   (unsigned)buffer.value.time.month,
				   (unsigned)buffer.value.time.day,
				   (unsigned)buffer.value.time.hour,
				   (unsigned)buffer.value.time.minutes,
				   (unsigned)buffer.value.time.seconds);
			return true;
		}
		else if (LBP_PROTOCOL_VERSION_MISMATCH == buffer.ret) {
			d.LOG_CPU_ERROR("\tToo old version of BIOS which does not support command ' %s BIOS '"
				". Update BIOS at node\n", d.get_cmd_name());
			command_err = EPERM;
			return false;
		}
		else if (LBP_BIOS_INFO_CACHE_EMPTY == buffer.ret) {
			d.LOG_CPU_ERROR("\tCannot read BIOS info. Try again or if you will see this error"
				" repeatedly then try to update bios at node\n");
			command_err = EAGAIN;
			return false;
		}
		else d.LOG_CPU_ERROR("Error %u\n", buffer.ret);
	else command_err = 1;
	return false;
}

std::string get_bios_version(caller_data d, close_on_exit& cpu_fd)
{
	vca_csm_ioctl_bios_param_desc buffer;
	buffer.param = VCA_LBP_BIOS_PARAM_VERSION;

	if (csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &buffer)) {
		if (LBP_STATE_OK == buffer.ret) {
			char command[BUFFER_SIZE];
			if (snprintf(command, sizeof(command), "%.8s", buffer.value.version)) {
				return command;
			}
		}
	}
	return BIOS_VERSION_ERROR;
}

static std::string get_mem_size(caller_data d) {
	int size;
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (!cpu_fd || !csm_ioctl(cpu_fd, VCA_GET_MEM_INFO, &size) || !size)
		return "unknown";
	return int_to_string(size);
}

bool print_node_bios_version_info(caller_data d, close_on_exit& cpu_fd)
{
	vca_csm_ioctl_bios_param_desc buffer;
	buffer.param = VCA_LBP_BIOS_PARAM_VERSION;
	if (csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &buffer))
		if (LBP_STATE_OK == buffer.ret)	{
			printf("\tVersion:\t%.8s\n", buffer.value.version);
			return true;
		}
		else if (LBP_PROTOCOL_VERSION_MISMATCH == buffer.ret) {
			d.LOG_CPU_ERROR("\tToo old version of BIOS which does not support command ' %s BIOS '"
				". Update BIOS at node\n", d.get_cmd_name());
			command_err = EPERM;
			return false;
		}
		else if (LBP_BIOS_INFO_CACHE_EMPTY == buffer.ret) {
			d.LOG_CPU_ERROR("\tCannot read BIOS info. Try again or if you will see this error"
				" repeatedly then try to update bios at node\n");
			command_err = EAGAIN;
			return false;
		}
		else d.LOG_CPU_ERROR("Error %u\n", buffer.ret);
	else command_err = 1;
	return false;
}


static bool print_node_bios_info(caller_data d)
{
	if (d.args.size() < 3)
		printf("Card %d Cpu %d:\n", d.card_id, d.cpu_id);
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	if (cpu_fd) {
		if (get_cpu_state(d) == VCA_BIOS_UP_TEXT)
			if(!try_handshake(cpu_fd, d))
				return false;
		if (print_node_bios_version_info(d, cpu_fd) && print_node_bios_date_info(d, cpu_fd))
			return true;
		else
			return false;
	}
	return false;
}

static bool print_node_meminfo(caller_data d) {
	if (d.caller_id == 0)
		printf(" Card   Cpu  Size(MB) SN(Slot 0) SN(Slot 1)\n");

	std::string sn;
	bool ret = vca_agent_command(d, sn, VCA_AGENT_MEM_INFO);
	if (!ret) {
		d.LOG_CPU_ERROR("Couldn't read node' memory serial number: %s\n", sn.c_str());
		return false;
	}

	char sn1[16] = "-", sn2[16] = "-";
	sscanf(sn.c_str(),
		" Serial Number: %15[^\n]"
		" Serial Number: %15[^\n]",
		sn1, sn2);
	printf("%5d %5d %9s %10s %10s\n",
		d.card_id, d.cpu_id, get_mem_size(d).c_str(), sn1, sn2);
	return true;
}

bool print_info_cmd_data(caller_data d)
{
	std::string subcmd = d.args.get_arg(SUBCMD);

	if (subcmd == INFO_SUBCMD_HW)
		return print_hw_info(d);
	else if (subcmd == INFO_SUBCMD_SYSTEM)
		return print_system_info(d);
	else if (subcmd == INFO_SUBCMD_NODE_OS)
		return print_node_os_info(d);
	else if (subcmd == INFO_SUBCMD_NODE_BIOS)
		return print_node_bios_info(d);
	else if (subcmd == INFO_SUBCMD_NODE_MEMINFO)
		return print_node_meminfo(d);
	else if (subcmd == INFO_SUBCMD_CPU_UUID)
		return print_node_uuid(d);
	else if (subcmd == INFO_SUBCMD_NODE_STATS)
		return print_node_stats(d);

	d.LOG_CPU_ERROR("Unknown subcommand: %s", subcmd.c_str());
	return false;
}

static void print_blk_dev_description(unsigned int card_id, unsigned int cpu_id, std::string name, enum blk_state state, std::string mode, std::string size_mb, std::string file_path)
{
	std::string dash(1, '-'); // workaround for klocwork issue
	if (mode == "") mode = dash;
	if (size_mb == "") size_mb = dash;
	if (file_path == "") file_path = dash;

	printf("%5u %5u %9s %9s %8s %9s %s\n", card_id, cpu_id, name.c_str(), get_blk_state_cstr(state), mode.c_str(), size_mb.c_str(), file_path.c_str());
}

static void print_blk_dev_header()
{
	printf("%5s %5s %9s %9s %8s %9s %2s\n", "Card", "Cpu", "Name", "State", "Mode", "Size(MB)", "FilePath");
	printf("------------------------------------------------------------\n");
}

bool reboot(caller_data d)
{
	bool ret;
	std::string output;

	if (get_cpu_state(d) == get_vca_state_string(VCA_NET_DEV_UP))
		d.LOG_CPU_ERROR("It's impossible to use %s command in %s state, because in this"
		" state VcaAgent doesn't work properly.\n", d.get_cmd_name(), get_vca_state_string(VCA_NET_DEV_UP));
	else {
		ret = vca_agent_command(d, output, VCA_AGENT_OS_REBOOT);

		if (ret) {
			d.LOG_CPU(VERBOSE_DEFAULT, output.append("\n").c_str());
		}
		else {
			d.LOG_CPU_ERROR("Execute '%s' command failed!\n", d.get_cmd_name());
		}
		return ret;
	}
	return false;
}

bool os_shutdown(caller_data d)
{
	vca_card_type const card_type = get_card_type(d.card_id);
	if(( card_type== VCA_MV || card_type== VCA_FPGA || card_type == VCA_VCAA || card_type == VCA_VCGA)
			&& get_cpu_state(d) == get_vca_state_string(VCA_NET_DEV_UP))
		d.LOG_CPU_ERROR("It's impossible to use %s command in %s state, because in this"
		" state VcaAgent doesn't work properly. Please use 'pwrbtn-short' command!\n", d.get_cmd_name(),
		get_vca_state_string(VCA_NET_DEV_UP));
	else {
		std::string output;
		bool ret = vca_agent_command(d, output, VCA_AGENT_OS_SHUTDOWN);

		if (ret)
			d.LOG_CPU(VERBOSE_DEFAULT, output.append("\n").c_str());
		else
			d.LOG_CPU_ERROR("Execute '%s' command failed!\n", d.get_cmd_name());

		return ret;
	}
	return false;
}

bool prm_from_string(const char *str, unsigned long long *encoded_for_bios) {
	struct PRMSize {
		const char *as_string;
		unsigned long long encoded_for_bios;
	} PRMSizes[] = {
		{ "auto", 0x00000000 },
		{  "32",  0x02000000 },
		{  "64",  0x04000000 },
		{  "128", 0x08000000 },
		{ NULL, 0 } // guard, keep last
	};

	for (int i = 0; PRMSizes[i].as_string; ++i) {
		if (!strcmp(PRMSizes[i].as_string, str)) {
			if (encoded_for_bios)
				*encoded_for_bios = PRMSizes[i].encoded_for_bios;
			return true;
		}
	}
	return false;
}

bool epoch_from_string(std::string str, uint64_t *const upper64, uint64_t *const lower64){
	size_t len = str.length();
	size_t boundary = len >= 16 ? len-16 : 0;
	std::string lowerStr = str.substr(boundary, std::string::npos);
	std::string upperStr = str.substr(0, boundary);
	uint64_t tmpLower, tmpUpper = 0;
	try {
		tmpLower = std::stoull(lowerStr, NULL, 16);
		if( !upperStr.empty() )
			tmpUpper = std::stoull(upperStr, NULL, 16);
	} catch ( ... ) {
		return false;
	}
	if( upper64 && lower64 ) {
		*upper64 = tmpUpper;
		*lower64 = tmpLower;
	}
	return true;
}

struct ApertureSize {
	const char *as_string;
	unsigned long long encoded_for_bios;
} ApertureSizes[] = {
	{  "128", 0x00 },
	{  "256", 0x01 },
	{  "512", 0x03 },
	{ "1024", 0x07 },
	{ "2048", 0x0f },
	{ "4096", 0x1f },
	{ NULL, 0 } // guard, keep last
};
bool aperture_from_string(const char *str, unsigned long long *encoded_for_bios) {
	for (int i = 0; ApertureSizes[i].as_string; ++i) {
		if (!strcmp(ApertureSizes[i].as_string, str)) {
			if (encoded_for_bios) {
				*encoded_for_bios = ApertureSizes[i].encoded_for_bios;
			}
			return true;
		}
	}
	return false;
}
const char *aperture_to_string(unsigned long long encoded_for_bios) {
	for (int i = 0; ApertureSizes[i].as_string; ++i) {
		if (encoded_for_bios == ApertureSizes[i].encoded_for_bios) {
			return ApertureSizes[i].as_string;
		}
	}
	return "unknown";
}

struct TdpSize {
	const char *as_string;
	unsigned long long encoded_for_bios;
} TdpSizes[] = {
	{ "0", 0 },
	{ "1", 1 },
	{ "2", 2 },
	{ "3", 3 },
	{ "4", 4 },
	{ "5", 5 },
	{ "6", 6 },
	{ "7", 7 },
	{ "8", 8 },
	{ "9", 9 },
	{ "10", 10 },
	{ "11", 11 },
	{ NULL, 0 } // guard, keep last
};

bool tdp_from_string(const char *str, unsigned long long *encoded_for_bios) {
	for (int i = 0; TdpSizes[i].as_string; ++i) {
		if (!strcmp(TdpSizes[i].as_string, str)) {
			if (encoded_for_bios) {
				*encoded_for_bios = TdpSizes[i].encoded_for_bios;
			}
			return true;
		}
	}
	return false;
}

const char *tdp_to_string(unsigned long long encoded_for_bios) {
	for (int i = 0; TdpSizes[i].as_string; ++i) {
		if (encoded_for_bios == TdpSizes[i].encoded_for_bios) {
			return TdpSizes[i].as_string;
		}
	}
	return "unknown";
}

bool is_bios_cfg_option(const char *cfg)
{
	return (!strcmp(cfg, BIOS_CFG_SGX) ||
		!strcmp(cfg, BIOS_CFG_EPOCH) ||
		!strcmp(cfg, BIOS_CFG_PRM) ||
		!strcmp(cfg, BIOS_CFG_GPU_APERTURE) ||
		!strcmp(cfg, BIOS_CFG_TDP) ||
		!strcmp(cfg, BIOS_CFG_HT) ||
		!strcmp(cfg, BIOS_CFG_GPU));
}

bool is_sgx_enabled(caller_data d){
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	vca_csm_ioctl_bios_param_desc desc;;
	desc.param = VCA_LBP_PARAM_SGX;
	//TODO: shouldn't that be taken from plx_lbp.c "cache"?
	if (!csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &desc))
		return false;
	if (!check_lbp_state_ok(desc.ret, LBP_GET_BIOS_PARAM, d))
		return false;
	return desc.value.value ? true : false;
}
bool is_epoch_default(caller_data d){
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	vca_csm_ioctl_bios_param_desc desc;;
	desc.param = VCA_LBP_PARAM_SGX_TO_FACTORY;
	//TODO: shouldn't that be taken from plx_lbp.c "cache"?
	if (!csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &desc))
		return false;
	if (!check_lbp_state_ok(desc.ret, LBP_GET_BIOS_PARAM, d))
		return false;
	return desc.value.value ? true : false; // TODO: PLX_LBP_PARAM_SGX_TO_FACTORY_YES
}
bool get_bios_cfg(caller_data d)
{
	bool is_gen1_card = get_card_type(d.card_id) & VCA_VV;
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	std::string cfg = d.args.get_arg(BIOS_CFG_NAME_ARG);
	std::string state = get_cpu_state(d);
	vca_csm_ioctl_bios_param_desc desc;
	struct {
		const std::string cfg_name;
		vca_lbp_bios_param bios_param;
		bool do_read;
		vca_lbp_retval ret_state;
		int ret_value;
	} cfg_desc[] = {
		{BIOS_CFG_SGX,          VCA_LBP_PARAM_SGX},
		{BIOS_CFG_GPU_APERTURE, VCA_LBP_PARAM_GPU_APERTURE},
		{BIOS_CFG_TDP,          VCA_LBP_PARAM_TDP},
		#ifdef SGX
		{BIOS_CFG_HT,           VCA_LBP_PARAM_HT},
		{BIOS_CFG_GPU,          VCA_LBP_PARAM_GPU},
		{BIOS_CFG_PRM,          VCA_LBP_PARAM_SGX_MEM}
		#endif
	};

	// Mark parameters for later reading
	if(!cfg.empty()) {
		if (is_gen1_card && cfg != BIOS_CFG_GPU_APERTURE) {
			d.LOG_CPU_WARN("'%s' parameter is not supported on VCA GEN1 card.\n", cfg.c_str());
			return false;
		}
		for (auto &c : cfg_desc)
			if (c.cfg_name == cfg)
				c.do_read = true;
	} else {
		for (auto &c : cfg_desc)
			c.do_read = !is_gen1_card;
		cfg_desc[1].do_read = true;
	}

	// Check node state
	if (state == get_vca_state_string(VCA_BIOS_UP)) {
		if (!set_lbp_param(cpu_fd, VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS,
			config.get_global_field(global_fields::handshake_irq_timeout_ms).get_number(), d))
			return false;
		if (!try_handshake(cpu_fd, d))
			return false;
	}
	else if (state == get_vca_state_string(VCA_BIOS_DOWN) ||
		 state == get_vca_state_string(VCA_ERROR) ||
		 state == get_vca_state_string(VCA_POWERING_DOWN) ||
		 state == get_vca_state_string(VCA_POWER_DOWN) ||
		 state == LINK_DOWN_STATE) {
		d.LOG_CPU_ERROR("BIOS configuration could not be read on node in %s state!\n", state.c_str());
		return false;
	}

	// Read parameters' values
	for (auto &c : cfg_desc) {
		if (!c.do_read)
			continue;
		desc.param = c.bios_param;
		if (!csm_ioctl(cpu_fd, LBP_GET_BIOS_PARAM, &desc))
			return false;
		c.ret_state = desc.ret;
		c.ret_value = desc.value.value;
	}

	// Interpret and print out results
	std::string cmd_output = "BIOS configuration:\n";
	for (auto &c : cfg_desc) {
		if (!c.do_read)
			continue;

		if (c.cfg_name == BIOS_CFG_SGX) {
			cmd_output += "SGX:\t\t";
			if (c.ret_state == LBP_STATE_OK)
				cmd_output += c.ret_value ? "enabled\n" : "disabled\n";
		}
		else if (c.cfg_name == BIOS_CFG_GPU_APERTURE) {
			cmd_output += "GPU_APERTURE:\t";
			if (c.ret_state == LBP_STATE_OK)
				cmd_output += std::string(aperture_to_string(c.ret_value)) + "\n";
		}
		else if (c.cfg_name == BIOS_CFG_TDP) {
			cmd_output += "TDP:\t\t";
			if (c.ret_state == LBP_STATE_OK)
				cmd_output += "Base +" + std::string(tdp_to_string(c.ret_value)) + " W\n";
		}
		else if (c.cfg_name == BIOS_CFG_HT) {
			cmd_output += "HT:\t\t";
			if (c.ret_state == LBP_STATE_OK)
				cmd_output += c.ret_value ? "enabled\n" : "disabled\n";
		}
		else if (c.cfg_name == BIOS_CFG_GPU) {
			cmd_output += "GPU:\t\t";
			if (c.ret_state == LBP_STATE_OK)
				cmd_output += c.ret_value ? "enabled\n" : "disabled\n";
		}
		else if (c.cfg_name == BIOS_CFG_PRM) {
			cmd_output += "PRM:\t\t";
			if (c.ret_state == LBP_STATE_OK) {
				if( !is_sgx_enabled(d) )
					cmd_output += "N/A (SGX disabled)\n"; //the information is there and is intact, but should probably not be presented to user
				else {
					if(c.ret_value == 0)
						cmd_output += "AUTO\n";
					else {
						std::stringstream hexVal;
						hexVal << std::hex << std::showbase << c.ret_value << std::endl;
						cmd_output += hexVal.str();
					}
				}
			}
		}
		if (c.ret_state == LBP_PROTOCOL_VERSION_MISMATCH)
			cmd_output += "This version of BIOS does not support reading parameter '" + c.cfg_name + "'\n";
		else if (c.ret_state == LBP_BIOS_INFO_CACHE_EMPTY )
			cmd_output += "Cannot read BIOS info. Update node BIOS if this error persists\n";
		else if (c.ret_state != LBP_STATE_OK)
			cmd_output += "Error: Command returned with status " + std::string(get_vca_lbp_retval_str(c.ret_state)) + "\n";
	}
	d.LOG_CPU(VERBOSE_DEFAULT, "%s", cmd_output.c_str());
	return true;
}

// return true if sgx==enabled and aperture_size==aperture_val is supported conf.
static bool is_sgx_supported_for_aperture(unsigned long long aperture_val) {
	return aperture_val <= 0x03; // less than or equal to 512MB
	// NOTE: when changing this function, grep also for
	// 'Invalid BIOS configuration requested' in this file
}
static bool is_aperture_size_valid_on_gen1_card(unsigned long long aperture_val) {
	return aperture_val <= 0x07; // less than or equal to 1024MB
	// NOTE: when changing this function, grep also for
	// 'greater than 1024MB on VCA GEN1 card' in this file
}

bool prepare_SPAD_to_ready(caller_data d, filehandle_t cpu_fd)
{
	if (!set_lbp_param(cpu_fd, VCA_LBP_PARAM_i7_IRQ_TIMEOUT_MS,
		config.get_global_field(global_fields::handshake_irq_timeout_ms).get_number(), d))
		return false;

	return try_handshake(cpu_fd, d);
}

bool set_bios_cfg(caller_data d)
{
	bool is_gen1_card = get_card_type(d.card_id) & VCA_VV;
	std::vector<std::string> cfgs = d.args.get_args(BIOS_CFG_NAME_ARG);
	std::vector<std::string> values = d.args.get_args(BIOS_CFG_VALUE_ARG);
	std::string status = get_cpu_state(d);
	close_on_exit cpu_fd(open_cpu_fd(d.card_id, d.cpu_id));
	std::vector<vca_csm_ioctl_bios_param_desc> desc;

	if(values.size() < cfgs.size()){
		d.LOG_CPU_ERROR("No value provided for %s\n", cfgs.back().c_str());
		return false;
	}

	if ( status != VCA_BIOS_UP_TEXT && status != VCA_BIOS_READY_TEXT) {
		d.LOG_CPU_ERROR("Node needs to be in \"%s\" or \"%s\" state!\n", VCA_BIOS_UP_TEXT, VCA_BIOS_READY_TEXT);
		return false;
	}
	if (status == VCA_BIOS_UP_TEXT) {
		if (!prepare_SPAD_to_ready(d, cpu_fd))
			return false;
	}

	// This for loop will fill desc vector. This vector contains data structures that will be sent to BIOS in order to update its configuration
	for(unsigned int i=0; i < cfgs.size(); ++i)
	{
		vca_csm_ioctl_bios_param_desc dsc = { LBP_STATE_OK, VCA_LBP_BIOS_PARAM__INVALID, 0 };
		if (cfgs[i] == BIOS_CFG_SGX) {
			if (is_gen1_card) {
				d.LOG_CPU_ERROR("SGX is not supported on VCA GEN1 card.\n");
			} else if( values[i] == "disable" && !is_epoch_default(d) )
					d.LOG_CPU_ERROR("EPOCH must be reset to factory default before disabling SGX.\n");
			else {
				if( values[i] == "enable" ) {
					dsc.param = VCA_LBP_PARAM_SGX_TO_FACTORY;
					dsc.value.value = 1; // TODO: PLX_LBP_PARAM_SGX_TO_FACTORY_YES
					desc.push_back(dsc); // TODO: a hack to run multiple commands in place of one
					dsc.param = VCA_LBP_PARAM_SGX_MEM;
					dsc.value.value = 0; // TODO: PLX_LBP_PARAM_SGX_MEM_AUTO
					desc.push_back(dsc); // TODO: a hack to run multiple commands in place of one
				}
				dsc.param = VCA_LBP_PARAM_SGX;
				dsc.value.value = values[i] == "enable";
			}
		} else if (cfgs[i] == BIOS_CFG_EPOCH) {
			if (is_gen1_card)
				d.LOG_CPU_ERROR("SGX is not supported on VCA GEN1 card.\n");
			else if ( !is_sgx_enabled(d) )
				d.LOG_CPU_ERROR("SGX is disabled. Enable first.\n");
			else {
				std::string lowCaseValue = values[i];
				std::transform(lowCaseValue.begin(), lowCaseValue.end(), lowCaseValue.begin(), ::tolower);
				if( 0 == lowCaseValue.compare("factory-default") ) {
					dsc.param = VCA_LBP_PARAM_SGX_TO_FACTORY;
					dsc.value.value = 1; // TODO: PLX_LBP_PARAM_SGX_TO_FACTORY_YES
				} else if( 0 == lowCaseValue.compare("random") ) {
					dsc.param = VCA_LBP_PARAM_SGX_OWNER_EPOCH_TYPE;
					dsc.value.value = 1; // TODO: PLX_LBP_PARAM_SGX_EPOCH_TYPE_NEW_RANDOM
				} else {
					uint64_t upper64;
					uint64_t lower64;
					if( epoch_from_string(values[i], &upper64, &lower64) ) {
							dsc.param = VCA_LBP_PARAM_SGX_OWNER_EPOCH_TYPE;
							dsc.value.value = 2; // PLX_LBP_PARAM_SGX_EPOCH_TYPE_USER_DEFINED
							desc.push_back(dsc); // TODO: a hack to run multiple commands in place of one
							dsc.param = VCA_LBP_PARAM_EPOCH_0;
							dsc.value.value = lower64;
							desc.push_back(dsc); // TODO: a hack to run multiple commands in place of one
							dsc.param = VCA_LBP_PARAM_EPOCH_1;
							dsc.value.value = upper64;
					} else
						d.LOG_CPU_ERROR("invalid value for EPOCH: `%s'\n", values[i].c_str());
				}
			}
		} else if (cfgs[i] == BIOS_CFG_PRM) {
			if (is_gen1_card)
				d.LOG_CPU_ERROR("SGX is not supported on VCA GEN1 card.\n");
			else if ( !is_sgx_enabled(d) )
				d.LOG_CPU_ERROR("SGX is disabled. Enable first.\n");
			else
				if (!prm_from_string(values[i].c_str(), &dsc.value.value))
					d.LOG_CPU_ERROR("invalid value for PRM: `%s'\n", values[i].c_str());
				else {
					dsc.param = VCA_LBP_PARAM_SGX_MEM;
				}
		} else if (cfgs[i] == BIOS_CFG_HT) {
			if (is_gen1_card) {
				d.LOG_CPU_ERROR("Hyper-threading enabling/disabling is not supported on VCA GEN1 card.\n");
			} else {
				dsc.param = VCA_LBP_PARAM_HT;
				dsc.value.value = values[i] == "enable";
			}
		} else if (cfgs[i] == BIOS_CFG_GPU) {
			if (is_gen1_card) {
				d.LOG_CPU_ERROR("GPU enabling/disabling is not supported on VCA GEN1 card.\n");
			} else {
				dsc.param = VCA_LBP_PARAM_GPU;
				dsc.value.value = values[i] == "enable";
			}
		} else if (cfgs[i] == BIOS_CFG_GPU_APERTURE){
			if (!aperture_from_string(values[i].c_str(), &dsc.value.value)){
				d.LOG_CPU_ERROR("invalid value for GPU_APERTURE: `%s'\n", values[i].c_str());
			} else if (is_gen1_card && !is_aperture_size_valid_on_gen1_card(dsc.value.value)){
				d.LOG_CPU_ERROR("GPU_APERTURE could not be greater than 1024MB on VCA GEN1 card.\n");
			} else
				dsc.param = VCA_LBP_PARAM_GPU_APERTURE;
		} else if (cfgs[i] == BIOS_CFG_TDP) {
			if (is_gen1_card) {
				d.LOG_CPU_ERROR("VCA GEN1 card doesn't support changing TDP value.\n");
			} else if (!tdp_from_string(values[i].c_str(), &dsc.value.value)){
				d.LOG_CPU_ERROR("Value (%s) to change TDP is not correct. See 'vcactl help' for reference.\n", values[i].c_str());
			} else
				dsc.param = VCA_LBP_PARAM_TDP;
		}
		if(dsc.param != VCA_LBP_BIOS_PARAM__INVALID){
			if( VCA_LBP_PARAM_EPOCH_1 != dsc.param  && // security requirement is for ESON to never leave the node
				VCA_LBP_PARAM_SGX_OWNER_EPOCH_TYPE != dsc.param) { // allow to set a new random EPOCH
				unsigned long long current_status;
				if(!get_bios_cfg_for(d, cpu_fd, dsc.param, current_status)){
					d.LOG_CPU_ERROR("Prerequisite checking failed for %s. Aborting.\n", cfgs[i].c_str());
					return false;
				} else if(dsc.value.value == current_status){
					d.LOG_CPU_WARN("Value for %s already set as requested. Skipping.\n", cfgs[i].c_str());
					continue;
				}
			}
			desc.push_back(dsc);
		}
	}

	if(desc.empty()){
		d.LOG_CPU_WARN("No changes in BIOS cfg will be applied. Exiting.\n");
		return true;
	}

	vca_csm_ioctl_bios_param_desc *sgx_desc = nullptr;
	vca_csm_ioctl_bios_param_desc *aperture_desc = nullptr;
	bool only_tdp_change = true;
	for(auto &dsc : desc){
		if(dsc.param == VCA_LBP_PARAM_SGX)
			sgx_desc = &dsc;
		if(dsc.param == VCA_LBP_PARAM_GPU_APERTURE)
			aperture_desc = &dsc;
		if(dsc.param != VCA_LBP_PARAM_TDP)
			only_tdp_change = false;
	}
	int max_attempts = sgx_desc ? 3 : 1;

	// compat check is supposed to make sure that aperture value doesn't conflict with SGX. It must be <= 512MB if SGX is enabled
	// GEN1 doesn't support SGX therafore no conflicts for them
	bool needs_compat_check = sgx_desc || (aperture_desc && !is_gen1_card);
	if (needs_compat_check) {
		// if SGX and GPU aperture values are set in one command, check if desc values do not conflict with each other
		if(sgx_desc && aperture_desc){
			if(sgx_desc->value.value && !is_sgx_supported_for_aperture(aperture_desc->value.value)){
				d.LOG_CPU_ERROR("Invalid BIOS configuration requested. SGX could be enabled only with aperture size <= 512MB. Aperture size may be changed with vcactl set-BIOS-cfg gpu-aperture <size> command\n");
				return false;
			// even when there's no conflict, we have to be sure parameters will be set in proper order
			} else if (
				(sgx_desc->value.value && sgx_desc < aperture_desc) || // when enabling SGX, aperture value must be decreased first
				(!sgx_desc->value.value && aperture_desc < sgx_desc))  // when disabling SGX, aperture value change must wait for SGX being disabled
					std::swap(*sgx_desc, *aperture_desc);
		} else {
		// if only one of those settings (SGX or aperture) is set in this command call, other value is read from current BIOS config
			vca_lbp_bios_param other_param = sgx_desc != nullptr ? VCA_LBP_PARAM_GPU_APERTURE : VCA_LBP_PARAM_SGX;
			unsigned long long other_param_value;
			if (!get_bios_cfg_for(d, cpu_fd, other_param, other_param_value)) {
				d.LOG_CPU_ERROR("Prerequisite checking failed. Aborting.\n");
				return false;
			}
			if (
				(sgx_desc && sgx_desc->value.value && !is_sgx_supported_for_aperture(other_param_value)) ||
				(!sgx_desc && other_param_value && !is_sgx_supported_for_aperture(aperture_desc->value.value))) {
					d.LOG_CPU_ERROR("Invalid BIOS configuration requested. SGX could be enabled only with aperture size <= 512MB. Aperture size may be changed with vcactl set-BIOS-cfg gpu-aperture <size> command\n");
					return false;
			}
		}
	}

	// this loop actually applies the changes to BIOS, using structures in desc vector. Node will be restarted if necessary
	for (int i = 0; i < max_attempts; i++)
	{
		for(auto &dsc : desc)
			set_bios_cfg_for(d, cpu_fd, dsc);
		if(!only_tdp_change){
			d.LOG_CPU(VERBOSE_DEFAULT, "BIOS configuration changed. Node will be restarted...\n");
			// Gen2 is being restarted by applying pwrbtn-long and pwrbtn-short commands
			// changing TDP does not require reset, hence skip if it's the only change in config
			if (!is_gen1_card) {
				press_power_button(d, true);
				sleep(TIME_TO_POWER_DOWN_NODE_S); // waiting to make sure that CPU power go down
				press_power_button(d, false);
				d.LOG_CPU(VERBOSE_DEFAULT, "Node will be powered down and up to make the change active.\n"
					"Please wait for 'bios_up' to start work with the node.\n");
			}
			else {
				reset(d);
				d.LOG_CPU(VERBOSE_DEFAULT, "Node will be reset to make the change active.\n"
					"Please wait for 'bios_up' to start work with the node.\n");
			}

			if (!wait_bios(d))
				return false;

			if (!prepare_SPAD_to_ready(d, cpu_fd))
				return false;
		}

		// check if configuration was changed succesfully
		bool ret = true;
		for(auto &dsc : desc){
			unsigned long long new_value;
			if (!get_bios_cfg_for(d, cpu_fd, dsc.param, new_value) && new_value == dsc.value.value){
				ret = false;
			}
		}
		if(ret)
			return ret;
		if (i + 1 != max_attempts)
			d.LOG_CPU(VERBOSE_DEFAULT, "Retrying to set param in BIOS\n");
	}
	d.LOG_CPU_ERROR("Failed to set param in BIOS. Exiting...\n");
	return false;
}

bool print_blk_list(caller_data d, filehandle_t blk_dev_fd, unsigned int blk_dev_id)
{
	std::string name = get_block_dev_name_from_id(blk_dev_id);
	std::string mode;
	std::string file_path;
	std::string size_mb;

	enum blk_state state = BLK_STATE_DISABLED;

	if (check_blk_disk_exist(blk_dev_fd, blk_dev_id)) {
		struct vcablk_disk_info_desc info;
		info.disk_id = blk_dev_id;
		info.exist = 0;

		int rc = ioctl(blk_dev_fd, VCA_BLK_GET_DISK_INFO, &info);
		if (rc < 0) {
			LOG_ERROR("Ioctl VCA_BLK_GET_DISK_INFO error: %s\n", strerror(errno));
			return false;
		}

		file_path = "";
		if (info.type == VCABLK_DISK_TYPE_MEMORY) {
			mode = BLOCKIO_MODE_RAMDISK;
		}
		else {
			file_path = info.file_path;
			mode = get_mode_string(info.mode);
		}

		size_mb = int_to_string(B_TO_MB(info.size));

		if (is_blk_disk_active(d, blk_dev_fd, blk_dev_id))
			state = BLK_STATE_ACTIVE;
		else
			state = BLK_STATE_INACTIVE;

	}
	else if (config.blk_dev_exist(d.card_id, d.cpu_id, blk_dev_id)) {
		std::string s_mode = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::mode).get_string();
		std::string s_path = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::path).get_string();
		size_mb = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::ramdisk_size_mb).get_string();

		file_path = "";
		bool ramdisk = (s_mode == BLOCKIO_MODE_RAMDISK);
		if (ramdisk) {
			mode = BLOCKIO_MODE_RAMDISK;
		} else {
			file_path = s_path;
			mode = s_mode;
		}

		if (is_blk_enabled(d.card_id, d.cpu_id, blk_dev_id))
			state = BLK_STATE_ENABLED;
		else
			state = BLK_STATE_DISABLED;
	} else
		return true;

	print_blk_dev_description(d.card_id, d.cpu_id, name, state, mode, size_mb, file_path);
	return true;
}

bool blockio_ctl_list(caller_data d)
{
	close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
	if (!blk_dev_fd)
		return false;

	if (d.caller_id == 0) {
		print_blk_dev_header();
	}

	if (d.args.get_arg(BLOCKIO_ID_ARG).empty()) { // print all available block devices for given card and cpu
		for (int i = 0; i < MAX_BLK_DEVS; i++) {
			if (!print_blk_list(d, blk_dev_fd.fd, i))
				return false;
		}
	}
	else {
		unsigned int blk_dev_id = atoi(d.args.get_arg(BLOCKIO_ID_ARG).c_str());
		if (!print_blk_list(d, blk_dev_fd.fd, blk_dev_id))
			return false;
	}

	return true;
}

bool update_blk_config_fields(caller_data d, unsigned int blk_dev_id, const char *mode, const char *ramdisk_size, const char *file_path)
{
	if (!config.blk_dev_exist(d.card_id, d.cpu_id, blk_dev_id)) {
		if (config.add_blk_dev_fields(d.card_id, d.cpu_id, blk_dev_id)) {
			d.LOG_CPU(DEBUG_INFO, "Block device %s added to config file.\n",
				get_block_dev_name_from_id(blk_dev_id).c_str());
		}
		else {
			d.LOG_CPU_ERROR("Cannot add block device %s to config file!\n",
				get_block_dev_name_from_id(blk_dev_id).c_str());
			return false;
		}
	}

	if (!config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_MODE_STR, "") ||
		!config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_RAMDISK_SIZE_STR, "0") ||
		!config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_PATH_STR, ""))
		return false;

	if (mode && (!ramdisk_size != !file_path)) {
		if (ramdisk_size &&
				!config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_RAMDISK_SIZE_STR, ramdisk_size)) {
			return false;
		}
		else if (file_path &&
				!config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_PATH_STR, file_path)) {
			return false;
		}

		if (!config.save_blk_field(d.card_id, d.cpu_id, blk_dev_id, BLK_CONFIG_MODE_STR, mode))
			return false;
	}
	else {
		LOG_ERROR("Invalid config blockio params!\n");
		return false;
	}

	d.LOG_CPU(DEBUG_INFO, "Block device %s data updated in config.\n", get_block_dev_name_from_id(blk_dev_id).c_str());

	if (!config.get_vca_config_from_file()) {
		LOG_ERROR("could not parse vca configuration file!\n");
		return false;
	}

	return true;
}

bool check_blk_path(caller_data d, int cards_num)
{
	// TODO: refactor handling of command starting in order to simplify
	//       and remove this ugly static
	static std::string previous_file_path;
	unsigned int blk_dev_id = atoi(d.args.get_arg(BLOCKIO_ID_ARG).c_str());
	std::string mode = d.args.get_arg(BLOCKIO_MODE_ARG);
	std::string file_path;
	std::string tmp_path;

	if (!strcmp(d.args.get_cmd_name(), BOOT_CMD) && is_blk_enabled(d.card_id, d.cpu_id, blk_dev_id)) {
		if (blk_dev_id != 0) {
			d.LOG_CPU_ERROR("Currently booting from blockio is supported only for vcablk0.\n");
			return false;
		}
		file_path = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::path).get_string();
		mode = config.get_blk_field(d.card_id, d.cpu_id, blk_dev_id, blk_dev_fields::mode).get_string();
	} else if (!strcmp(d.args.get_cmd_name(), BLOCKIO_CMD) && mode == BLOCKIO_MODE_RW) {
		file_path = d.args.get_arg(BLOCKIO_MODE_PARAM_ARG);
	} else {
		return true;
	}

	if (mode == BLOCKIO_MODE_RAMDISK)
		return true;

	if (previous_file_path == file_path) {
		d.LOG_CPU_ERROR("File %s is already opened in other block device. Cannot open the same file on few block devices.\n", file_path.c_str());
		return false;
	}

	for (int i = 0; i < MAX_BLK_DEVS; i++) {
		for (int j = 0; j < cards_num; j++) {
			for (int k = get_last_cpu( j); 0<= k; --k) {
				if (j == d.card_id && k == d.cpu_id) {
					continue; // reusing own device is perfectly ok
				}
				tmp_path = config.get_blk_field(j, k, i, blk_dev_fields::path).get_string();
				mode = config.get_blk_field(j, k, i, blk_dev_fields::mode).get_string();
				if (mode != BLOCKIO_MODE_RAMDISK && file_path == tmp_path && is_blk_enabled(j, k, i)) {
					d.LOG_CPU_ERROR("File %s is already opened in other block device. Cannot open the same file on few block devices.\n", file_path.c_str());
					return false;
				}
			}
		}
	}

	previous_file_path = file_path;
	return true;
}

bool blockio_ctl_open(caller_data d)
{
	close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
	if (!blk_dev_fd)
		return false;

	if (d.args.get_arg(BLOCKIO_ID_ARG).empty())
		return false;

	struct vca_blk_dev_info blk_dev_info;
	unsigned int blk_dev_id = atoi(d.args.get_arg(BLOCKIO_ID_ARG).c_str());

	if (check_blk_disk_exist(blk_dev_fd.fd, blk_dev_id)) {
		d.LOG_CPU_ERROR("Block device %s already exist.\n", get_block_dev_name_from_id(blk_dev_id).c_str());
		return false;
	}

	if (!check_blk_path(d, get_cards_num()))
		return false;

	std::string mode = d.args.get_arg(BLOCKIO_MODE_ARG);
	if (mode.empty()) {
		d.LOG_CPU(DEBUG_INFO, "Using block device described in config\n");
	} else if (!strcmp(mode.c_str(), BLOCKIO_MODE_RAMDISK)) {
		std::string disk_size = d.args.get_arg(BLOCKIO_MODE_PARAM_ARG);
		if (!is_unsigned_number(disk_size.c_str()) || atoi(disk_size.c_str()) == 0) {
			LOG_ERROR("Cannot parse ramdisk size! %s \n", disk_size.c_str());
			return false;
		} else if (!update_blk_config_fields(d, blk_dev_id, mode.c_str(), disk_size.c_str(), NULL)) {
			d.LOG_CPU_ERROR("Cannot update config for block device %s\n",
				get_block_dev_name_from_id(blk_dev_id).c_str());
			return false;
		}
		d.LOG_CPU(DEBUG_INFO, "Using block device described in config\n");
	} else if (!strcmp(mode.c_str(), BLOCKIO_MODE_RO) || !strcmp(mode.c_str(), BLOCKIO_MODE_RW)) {
		std::string file_path = d.args.get_arg(BLOCKIO_MODE_PARAM_ARG);
		char resolved_path[PATH_MAX + 1];
		if (!realpath(file_path.c_str(), resolved_path)) {
			d.LOG_CPU_ERROR("Cannot canonicalize block path (got %s): %s!\n",
					file_path.c_str(), strerror(errno));
			return false;
		} else if (!file_exists(resolved_path)) {
			d.LOG_CPU_ERROR("Wrong blockio type param! Can't access file %s.\n", resolved_path);
			return false;
		} else if (!update_blk_config_fields(d, blk_dev_id, mode.c_str(), NULL, resolved_path)) {
			d.LOG_CPU_ERROR("Cannot update config for block device %s\n",
				get_block_dev_name_from_id(blk_dev_id).c_str());
			return false;
		}
	} else {
		d.LOG_CPU_ERROR("Unknown block mode %s\n", mode.c_str());
		return false;
	}

	if (config.blk_dev_exist(d.card_id, d.cpu_id, blk_dev_id)) {
		if (!get_blk_config_field(d, blk_dev_id, blk_dev_info)) {
			d.LOG_CPU_ERROR("Cannot get blk_dev_info from config\n");
			return false;
		}
	}
	else {
		d.LOG_CPU_ERROR("Block device %s not defined in config file!\n",
			get_block_dev_name_from_id(blk_dev_id).c_str());
		return false;
	}

	if (!enable_blk_dev(d, blk_dev_id))
		return false;

	if (!is_bios_up_or_ready(d) && (get_cpu_state(d) != get_vca_state_string(VCA_BIOS_DOWN))) {
		if (open_blk_dev(blk_dev_fd.fd, blk_dev_info) != SUCCESS) {
			d.LOG_CPU_ERROR("Open block device %s failed!\n", get_block_dev_name_from_id(blk_dev_id).c_str());
			return false;
		}
	}

	return true;
}

bool blockio_ctl_close(caller_data d)
{
	close_on_exit blk_dev_fd = open_blk_fd(d.card_id, d.cpu_id);
	if (!blk_dev_fd)
		return false;

	std::string blockioarg = d.args.get_arg(BLOCKIO_ID_ARG);
	if (blockioarg.empty())
		return false;

	unsigned int blk_dev_id = atoi(blockioarg.c_str());
	if (!is_blk_enabled(d.card_id, d.cpu_id, blk_dev_id)) {
		d.LOG_CPU_WARN("Block device %s is not open."
			" You do not need to close it.\n", get_block_dev_name_from_id(blk_dev_id).c_str());
		return true;
	}

	if (check_if_blkdev_is_not_active(d) || d.args.is_force_cmd_enabled()){
		if (close_blk_dev(blk_dev_fd.fd, blk_dev_id) != SUCCESS) {
			d.LOG_CPU(DEBUG_INFO, "Close block device %s failed!\n", get_block_dev_name_from_id(blk_dev_id).c_str());
			return false;
		}
		return disable_blk_dev(d, blk_dev_id);
	}
	else {
		if (blk_dev_id == 0){
			d.LOG_CPU_ERROR("You're trying to disconnect active block device!" \
				" It may lead to data corruption. Please use os-shutdown command first." \
				" If you're sure that you want to close it anyway, " \
				"and OS is not booted from this device, please use --force flag.\n");
			return false;
		}
		else {
			d.LOG_CPU_ERROR("You're trying to disconnect active block device!" \
				" It may lead to data corruption. Please use os-shutdown command first." \
				" If you're sure that you want to close it anyway, please use --force flag.\n");
			return false;
		}
	}
}

bool blockio_ctl(caller_data d)
{
	d.LOG_CPU(DEBUG_INFO, "Executing " BLOCKIO_CMD " %s command.\n", d.args.get_arg(SUBCMD).c_str());

	if (d.args.get_arg(SUBCMD) == BLOCKIO_SUBCMD_LIST) {
		if (!blockio_ctl_list(d)) {
			d.LOG_CPU_ERROR("Failed to list block devices!\n");
			return false;
		}
	}
	else if (d.args.get_arg(SUBCMD) == BLOCKIO_SUBCMD_OPEN) {
		if (!blockio_ctl_open(d)) {
			d.LOG_CPU_ERROR("Failed to open block device!\n");
			return false;
		}
	}
	else if (d.args.get_arg(SUBCMD) == BLOCKIO_SUBCMD_CLOSE) {
		if (!blockio_ctl_close(d)) {
			d.LOG_CPU_ERROR("Failed to close block device!\n");
			return false;
		}
	}
	else {
		d.LOG_CPU_ERROR("Wrong blockio subcommand!\n");
		return false;
	}

	return true;
}

std::string get_card_gen(const unsigned card_id)
{
	enum vca_card_type card_type = get_card_type(card_id);
	if(card_type == VCA_UNKNOWN)
		return "unknown";

	std::string card_gen;
	switch(card_type) {
	case VCA_VV_FAB1:
	case VCA_VV_FAB2:
		card_gen = "VCA GEN 1";
		break;
	case VCA_MV:
		card_gen = "VCA GEN 2";
		break;
	case VCA_FPGA:
		card_gen = "VCA FPGA";
		break;
	case VCA_VCAA:
		card_gen = "VCAC-A";
		break;
	case VCA_VCGA:
		card_gen = "VCAC-R";
		break;
	default:
		card_gen = "VCA GEN UNKNOWN";
	}
	if(card_type == VCA_MV) {
		__u32 boardId = get_board_id(card_id);
		card_gen = card_gen + " FAB " + boost::lexical_cast<std::string>(boardId);
	}
	return card_gen;
}


std::vector<std::string> get_subcmd_list(const char *_cmd)
{
	std::vector<std::string> subcommand_list;
	std::string cmd = char_to_string((char *)_cmd);

	if (cmd == NETWORK_CMD) {
		subcommand_list.push_back(NETWORK_SUBCMD_ALL);
		subcommand_list.push_back(NETWORK_SUBCMD_IP);
		subcommand_list.push_back(NETWORK_SUBCMD_IP6);
		subcommand_list.push_back(NETWORK_SUBCMD_MAC);
		subcommand_list.push_back(NETWORK_SUBCMD_VM_MAC);
		subcommand_list.push_back(NETWORK_SUBCMD_STATS);
		subcommand_list.push_back(NETWORK_SUBCMD_RENEW);
	}
	else if (cmd == BLOCKIO_CMD) {
		subcommand_list.push_back(BLOCKIO_SUBCMD_LIST);
		subcommand_list.push_back(BLOCKIO_SUBCMD_OPEN);
		subcommand_list.push_back(BLOCKIO_SUBCMD_CLOSE);
	}
	else if (cmd == GOLD_CMD) {
		subcommand_list.push_back("on");
		subcommand_list.push_back("off");
	}
	else if (cmd == INFO_CMD) {
		subcommand_list.push_back(INFO_SUBCMD_HW);
		subcommand_list.push_back(INFO_SUBCMD_SYSTEM);
		subcommand_list.push_back(INFO_SUBCMD_NODE_OS);
		subcommand_list.push_back(INFO_SUBCMD_NODE_BIOS);
		subcommand_list.push_back(INFO_SUBCMD_NODE_MEMINFO);
		subcommand_list.push_back(INFO_SUBCMD_CPU_UUID);
		subcommand_list.push_back(INFO_SUBCMD_NODE_STATS);
	}
	else if (cmd == ID_LED_CMD) {
		subcommand_list.push_back(ID_LED_SUBCMD_ON);
		subcommand_list.push_back(ID_LED_SUBCMD_OFF);
	}
	else {
		LOG_ERROR("Unknown command: %s\n", cmd.c_str());
	}

	return subcommand_list;
}

std::string print_subcmd_list_content(std::vector<std::string> subcommand_list) {
	std::string output;
	for (size_t i = 0; i < subcommand_list.size(); i++) {
		output += subcommand_list.at(i);
		if (subcommand_list.size() - 1 == i)
			output += ".";
		else
			output += ", ";
	}
	return output;
}

static inline subcmds *get_subcmds(const char *_cmd, const std::vector<std::string> &subcommand_list = std::vector<std::string>())
{
	subcmds *subcmd = new subcmds();
	std::string cmd = char_to_string((char *)_cmd);

	if (cmd == NETWORK_CMD) {
		subcmd->add_subcmd(NETWORK_SUBCMD_ALL,		"print all network information");
		subcmd->add_subcmd(NETWORK_SUBCMD_IP,		"print IP address");
		subcmd->add_subcmd(NETWORK_SUBCMD_IP6,		"print IPv6 address");
		subcmd->add_subcmd(NETWORK_SUBCMD_MAC,		"print MAC address");
		subcmd->add_subcmd(NETWORK_SUBCMD_VM_MAC,	"print VM MAC address");
		subcmd->add_subcmd(NETWORK_SUBCMD_STATS,	"print interface statistics");
		subcmd->add_subcmd(NETWORK_SUBCMD_RENEW,	"renew IP address assigned by DHCP");
	}
	else if (cmd == BLOCKIO_CMD) {
		subcmd->add_subcmd(BLOCKIO_SUBCMD_LIST,		"list all blockio devices");
		subcmd->add_subcmd(BLOCKIO_SUBCMD_OPEN,		"open/create blockio device");
		subcmd->add_subcmd(BLOCKIO_SUBCMD_CLOSE,	"close blockio device");
	}
	else if (cmd == GOLD_CMD) {
		subcmd->add_subcmd("on",			"boot golden bios");
		subcmd->add_subcmd("off",			"boot standard bios");
	}
	else if (cmd == INFO_CMD) {
		subcmd->add_subcmd(INFO_SUBCMD_HW,				"print device information");
		subcmd->add_subcmd(INFO_SUBCMD_SYSTEM,			"print system information");
		subcmd->add_subcmd(INFO_SUBCMD_NODE_OS,			"print OS booted on cpu");
		subcmd->add_subcmd(INFO_SUBCMD_NODE_BIOS,		"print BIOS version on cpu");
		subcmd->add_subcmd(INFO_SUBCMD_NODE_MEMINFO,    "print node memory size and memory SN");
		subcmd->add_subcmd(INFO_SUBCMD_CPU_UUID,		"print cpu uuid");
		subcmd->add_subcmd(INFO_SUBCMD_NODE_STATS,		"print node stats");
	}
	else if (cmd == ID_LED_CMD) {
		subcmd->add_subcmd(ID_LED_SUBCMD_ON,				"turn led light on");
		subcmd->add_subcmd(ID_LED_SUBCMD_OFF,				"turn led light off");
	}
	else {
		LOG_ERROR("Unknown command: %s\n", cmd.c_str());
	}

	return subcmd;
}

#if VCACTL_PARSING_FUNCTIONS

parsing_output optional_card_id(const char * arg, args_holder & holder)
{
	if (!arg || !is_unsigned_number(arg))
		return NOT_PARSED;

	int card_id = atoi(arg);
	if (strcmp(holder.get_cmd_name(), "config")) {
		if (!card_exists(card_id)) {
			LOG_ERROR("Card %u not found!\n", card_id);
			return PARSING_FAILED;
		}
	}
	holder.add_arg(CARD_ID_ARG, card_id);
	return PARSED;
}

parsing_output requires_card_id(const char *arg, args_holder & holder)
{
	parsing_output ret = optional_card_id(arg, holder);
	if (ret == NOT_PARSED) {
		LOG_ERROR("This command needs card_id to work!\n");
		return PARSING_FAILED;
	}
	return ret;
}

parsing_output optional_cpu_id(const char *arg, args_holder & holder)
{
	if (!arg || !is_unsigned_number(arg))
		return NOT_PARSED;

	const data_field *card_id = holder(CARD_ID_ARG);
	if (!card_id)
		return NOT_PARSED;

	int cpu_id = atoi(arg);
	if (cpu_id > get_last_cpu(card_id->get_number())) {
		LOG_ERROR("No cpu %d on card %d found!\n", cpu_id, card_id->get_number());
		return PARSING_FAILED;
	}
	holder.add_arg(CPU_ID_ARG, cpu_id);
	return PARSED;
}

parsing_output requires_cpu_id(const char *arg, args_holder & holder)
{
	parsing_output ret = optional_cpu_id(arg, holder);
	if (ret == NOT_PARSED) {
		LOG_ERROR("This command needs cpu_id to work!\n");
		return PARSING_FAILED;
	}
	return ret;
}

parsing_output optional_blockio_id(const char *arg, args_holder &holder)
{
	int blockio_id;
	if (extract_block_dev_id(arg, blockio_id)) {
		holder.add_arg(BLOCKIO_ID_ARG, blockio_id);
		return PARSED;
	}

	const std::string subcmd = holder.get_arg(SUBCMD);
	if ((subcmd == BLOCKIO_SUBCMD_LIST && arg) || subcmd == BLOCKIO_SUBCMD_OPEN || subcmd == BLOCKIO_SUBCMD_CLOSE) {
		LOG_ERROR("Wrong blockio id! Correct value: %sN, where N >= 0 and N < %d.\n",
			BLK_CONFIG_NAME, MAX_BLK_DEVS);
		return PARSING_FAILED;
	}

	return NOT_PARSED;
}

parsing_output optional_blockio_type(const char *arg, args_holder &holder)
{
	if (holder.get_arg(SUBCMD) == BLOCKIO_SUBCMD_CLOSE && arg) {
		LOG_ERROR("Command \"blockio close\" accept only one parameter (blockio id)!\n");
		return PARSING_FAILED;
	}
	else if (holder.get_arg(SUBCMD) == BLOCKIO_SUBCMD_LIST && arg) {
		LOG_ERROR("Command \"blockio list\" accept only one parameter (blockio id)!\n");
		return PARSING_FAILED;
	}
	else if (holder.get_arg(SUBCMD) == BLOCKIO_SUBCMD_OPEN) {
		if (!arg)
			return PARSED; // parsed is returned to omit next parsing_output function

		if ((!strcmp(arg, BLOCKIO_MODE_RO) || !strcmp(arg, BLOCKIO_MODE_RW) || !strcmp(arg, BLOCKIO_MODE_RAMDISK))) {
			holder.add_arg(BLOCKIO_MODE_ARG, arg);
			return PARSED;
		}
		else {
			LOG_ERROR("Wrong blockio type! Allowed values: RO, RW, ramdisk.\n");
			return PARSING_FAILED;
		}
	}

	return NOT_PARSED;
}

parsing_output optional_blockio_type_param(const char *arg, args_holder &holder)
{
	if (holder.get_arg(SUBCMD) != BLOCKIO_SUBCMD_OPEN)
		return NOT_PARSED;

	if (holder.get_arg(BLOCKIO_MODE_ARG).empty())
		return NOT_PARSED;

	if (!arg) {
		LOG_ERROR("Missing blockio type param!\n");
		return PARSING_FAILED;
	}

	if (holder.get_arg(BLOCKIO_MODE_ARG) == BLOCKIO_MODE_RO ||
	    holder.get_arg(BLOCKIO_MODE_ARG) == BLOCKIO_MODE_RW) {
		if (!file_exists(arg)) {
			LOG_ERROR("Wrong blockio type param! Can't access file %s.\n", arg);
			return PARSING_FAILED;
		}
	}
	else if (holder.get_arg(BLOCKIO_MODE_ARG) == BLOCKIO_MODE_RAMDISK) {
		if (!is_unsigned_number(arg) || atoi(arg) == 0) {
			LOG_ERROR("Wrong blockio type param!\n"
				  "Invalid block device size (%s). Have to be more than 0.\n", arg);
			return PARSING_FAILED;
		}
	}
	else {
		LOG_ERROR("Wrong blockio type! Allowed values: RO, RW, ramdisk.\n");
		return PARSING_FAILED;
	}

	holder.add_arg(BLOCKIO_MODE_PARAM_ARG, arg);
	return PARSED;
}

parsing_output optional_file(const char *arg, args_holder & holder)
{
	if (!arg)
		return NOT_PARSED;

	if (!strcmp(arg, FORCE_LAST_OS_IMAGE))
		return NOT_PARSED;

	if (!strcmp(BLOCKIO_BOOT_DEV_NAME, arg)) {
		holder.add_arg(FILE_PATH_ARG, arg);
		return PARSED;
	}

	if (file_exists(arg)) {
		holder.add_arg(FILE_PATH_ARG, arg);
		return PARSED;
	}

	LOG_ERROR("file %s does not exist!\n", arg);
	return PARSING_FAILED;
}

parsing_output requires_file(const char *arg, args_holder & holder)
{
	parsing_output ret = optional_file(arg, holder);
	if (ret == NOT_PARSED) {
		LOG_ERROR("missing filepath parameter!\n");
		return PARSING_FAILED;
	}
	return ret;
}

parsing_output requires_mac_addr(const char *arg, args_holder & holder)
{
	const char * error = "please input mac address in canonical form! Example: 00-06-af-7d-66-b8\n";
	if (!arg) {
		LOG_ERROR("Missing MAC address!\n");
		return PARSING_FAILED;
	}

	if (strlen(arg) != 17) {
		LOG_ERROR("%s\n", error);
		return PARSING_FAILED;
	}

	static const char separators[] = { '-', ':' };
	static size_t separators_size = sizeof(separators) / sizeof(separators[0]);
	const char * mac = arg;
	for (int i = 0; i < 17; i += 3)
		if (!is_hex_digit(mac[i]) || !is_hex_digit(mac[i + 1])) {
			LOG_ERROR("%s\n", error);
			return PARSING_FAILED;
		}

	for (int i = 0; i < 14; i += 3) {
		bool found_separator = false;
		for (size_t j = 0; j < separators_size; j++)
			if (mac[i + 2] == separators[j]) {
				found_separator = true;
				break;
			}
		if (!found_separator) {
			LOG_ERROR("%s\n", error);
			return PARSING_FAILED;
		}

	}
	holder.add_arg(MAC_ADDR_ARG, arg);
	return PARSED;
}


parsing_output requires_serial_number(const char *arg, args_holder & holder)
{
	if (!arg) {
		LOG_ERROR("Missing Serial Number!\n");
		return PARSING_FAILED;
	}

	if (strlen(arg) > SN_MAX) {
		LOG_ERROR("Serial Number length should not exceed %d characters!\n", SN_MAX);
		return PARSING_FAILED;
	}
	holder.add_arg(SN_ARG, arg);
	return PARSED;
}

parsing_output optional_config_param(const char *arg, args_holder & holder)
{
	int blockio_id;
	if ((arg && config.contains_field(arg)) || extract_block_dev_id(arg, blockio_id)) {
		holder.add_arg(CONFIG_PARAM_NAME, arg);
		return PARSED;
	}
	return NOT_PARSED;
}

parsing_output requires_config_param(const char *arg, args_holder & holder)
{
	if (!arg) {
		LOG_ERROR("Missing parameter name!\n");
		return PARSING_FAILED;
	}
	if (config.contains_field(arg)) {
		/* to be removed in future */
		char * tmp = const_cast<char *>(arg);
		std::replace(tmp, tmp + strlen(tmp), '_', '-');

		holder.add_arg(CONFIG_PARAM_NAME, arg);
		return PARSED;
	}

	LOG_ERROR("Invalid parameter name: %s\n", arg);
	return PARSING_FAILED;
}

parsing_output requires_config_value(const char *arg, args_holder & holder)
{
	if (!arg) {
		LOG_ERROR("Missing parameter value!\n");
		return PARSING_FAILED;
	}

	holder.add_arg(CONFIG_PARAM_VALUE, arg);
	return PARSED;
}

parsing_output optional_value_only_param(const char *arg, args_holder & holder)
{
	if (!arg)
		return NOT_PARSED;

	const data_field * param_name = holder(CONFIG_PARAM_NAME);
	if (!param_name)
		return NOT_PARSED;

	if (config.is_field_cpu(param_name->get_cstring()) && !strcmp(arg, "--value")) {
		holder.add_arg(STRIP_PARAM, arg);
		return PARSED;
	}
	return NOT_PARSED;
}

parsing_output optional_value_force_get_last_os(const char *arg, args_holder & holder)
{
	if (!arg)
		return NOT_PARSED;

	if (!strcmp(arg, FORCE_LAST_OS_IMAGE)) {
		holder.add_arg(FORCE_GET_LAST_OS_IMAGE, arg);
		return PARSED;
	}
	return NOT_PARSED;
}

parsing_output requires_smb_id(const char *arg, args_holder & holder)
{
	if (!arg) {
		LOG_ERROR("missing SMB_ID parameter!\n");
		return PARSING_FAILED;
	}

	if (!is_unsigned_number(arg)) {
		LOG_ERROR("last parameter must be a number between 0-7!\n");
		return PARSING_FAILED;
	}
	int smb_id = atoi(arg);
	if (smb_id > 7 || smb_id < 0) {
		LOG_ERROR("last parameter must be a number between 0-7!\n");
		return PARSING_FAILED;
	}

	holder.add_arg(SMB_ID_ARG, smb_id);
	return PARSED;
}

parsing_output requires_trigger(const char *arg, args_holder & holder)
{
	if (!arg || !is_unsigned_number(arg)) {
		LOG_ERROR("Need trigger value! Only 0 or 1 may be used.\n");
		return PARSING_FAILED;
	}
	int trigger = atoi(arg);
	if (trigger < 0 || trigger > 1) {
		LOG_ERROR("Value %d not allowed! Only 0 or 1 may be used.\n", trigger);
		return PARSING_FAILED;
	}

	holder.add_arg(TRIGGER_ARG, trigger);
	return PARSED;
}

parsing_output optional_subcommand(const char *arg, args_holder &holder)
{
	std::vector<std::string> subcommand_list = get_subcmd_list(holder.get_cmd_name());

	if (subcommand_list.empty())
		return PARSING_FAILED;

	std::vector<std::string>::iterator findIter = std::find(subcommand_list.begin(), subcommand_list.end(), char_to_string((char *)arg));
	if (findIter == subcommand_list.end()) {
		return NOT_PARSED;
	}

	holder.add_arg(SUBCMD, arg);
	return PARSED;
}

parsing_output requires_subcommand(const char *arg, args_holder & holder)
{
	std::vector<std::string> subcommand_list = get_subcmd_list(holder.get_cmd_name());
	if (subcommand_list.empty())
		return PARSING_FAILED;

	if (!arg) {
		LOG_ERROR("Need subcommands: %s\n", print_subcmd_list_content(subcommand_list).c_str());
		return PARSING_FAILED;
	}

	std::vector<std::string>::iterator findIter = std::find(subcommand_list.begin(), subcommand_list.end(), char_to_string((char *)arg));
	if (findIter == subcommand_list.end()) {
		LOG_ERROR("Need subcommands: %s\n", print_subcmd_list_content(subcommand_list).c_str());
		return PARSING_FAILED;
	}

	holder.add_arg(SUBCMD, arg);
	return PARSED;
}

parsing_output optional_ip(const char *arg, args_holder & holder)
{
	if (!arg)
		return NOT_PARSED;

	if (holder(CPU_ID_ARG)) {
		if (!is_ip_address(arg)) {
			LOG_ERROR("Ip address incorrect!\n");
			return PARSING_FAILED;
		}
		holder.add_arg(IP_ADDR_ARG, arg);
	}
	return PARSED;
}

parsing_output optional_bios_cfg_name(const char *arg, args_holder & holder)
{
	if (!arg) {
		return NOT_PARSED;
	}

	if (!is_bios_cfg_option(arg)) {
		LOG_ERROR("BIOS parameter (%s) not allowed! See 'vcactl help' for reference.\n", arg);
		return PARSING_FAILED;
	}

	holder.add_arg(BIOS_CFG_NAME_ARG, arg);
	return PARSED;
}

parsing_output requires_bios_cfg_name(const char *arg, args_holder & holder)
{
	parsing_output ret = optional_bios_cfg_name(arg, holder);
	if (ret == NOT_PARSED) {
		LOG_ERROR("Missing BIOS configuration name! See 'vcactl help' for reference.\n");
		return PARSING_FAILED;
	}
	return ret;
}

parsing_output requires_bios_cfg_value(const char *arg, args_holder & holder)
{
	if (!arg) {
		LOG_ERROR("Missing BIOS configuration value! See 'vcactl help' for reference.\n");
		return PARSING_FAILED;
	}

	std::string arg_s = std::string(arg);
	std::string value = holder.get_args(BIOS_CFG_NAME_ARG).back();

	if (value == BIOS_CFG_SGX) {
		if (arg_s != "enable" && arg_s != "disable") {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				  arg, BIOS_CFG_SGX);
			return PARSING_FAILED;
		}
	}
	else if (value == BIOS_CFG_EPOCH) {
		std::string lowCaseValue = arg_s;
		std::transform(lowCaseValue.begin(), lowCaseValue.end(), lowCaseValue.begin(), ::tolower);
		if (lowCaseValue != "factory-default" && lowCaseValue != "random" && !epoch_from_string(arg_s,NULL,NULL) ) {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				  arg, BIOS_CFG_EPOCH);
			return PARSING_FAILED;
		}
	}
	else if (value == BIOS_CFG_PRM) {
		if ( !prm_from_string(arg, NULL) ) {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				  arg, BIOS_CFG_PRM);
			return PARSING_FAILED;
		}
	}
	else if (value == BIOS_CFG_HT) {
		if (arg_s != "enable" && arg_s != "disable") {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				arg, BIOS_CFG_HT);
			return PARSING_FAILED;
		}
	}
	else if (value == BIOS_CFG_GPU) {
		if (arg_s != "enable" && arg_s != "disable") {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				arg, BIOS_CFG_GPU);
			return PARSING_FAILED;
		}
	}
	else if (value == BIOS_CFG_GPU_APERTURE) {
		if (!aperture_from_string(arg, NULL)) {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				  arg, BIOS_CFG_GPU_APERTURE);
			return PARSING_FAILED;
		}
	}
	else if (value == BIOS_CFG_TDP) {
		if (!tdp_from_string(arg, NULL)) {
			LOG_ERROR("Wrong value (%s) for '%s'. See 'vcactl help' for reference.\n",
				arg, BIOS_CFG_TDP);
			return PARSING_FAILED;
		}
	}

	holder.add_arg(BIOS_CFG_VALUE_ARG, arg);
	return PARSED;
}

parsing_output requires_bios_cfg_name_and_value_pairs(const char *arg, args_holder &holder){
	if (holder.get_args(BIOS_CFG_NAME_ARG).size() == holder.get_args(BIOS_CFG_VALUE_ARG).size()){
		if (requires_bios_cfg_name(arg, holder) != PARSED)
			return PARSING_FAILED;
	} else
		if (requires_bios_cfg_value(arg, holder) != PARSED)
			return PARSING_FAILED;
	return PARSED_AND_CONTINUE;
}

#endif // VCACTL_PARSING_FUNCTIONS

static cmd_desc const commands_desc[] = {
    cmd_desc("status", new sequential_caller(status), optional_card_id, optional_cpu_id),
    cmd_desc("reset", new threaded_caller(_reset), optional_card_id, optional_cpu_id),
    cmd_desc("wait", new threaded_caller(wait), optional_card_id, optional_cpu_id),
    cmd_desc("wait-BIOS", new threaded_caller(wait_bios), optional_card_id, optional_cpu_id),
    cmd_desc("boot", new sequential_caller(boot), optional_card_id, optional_cpu_id, optional_file, optional_value_force_get_last_os),
    cmd_desc("reboot", new threaded_caller(reboot), optional_card_id, optional_cpu_id, optional_file),
    cmd_desc("update-BIOS", new threaded_caller(update_bios), optional_card_id, optional_cpu_id, requires_file),
    cmd_desc("recover-BIOS", new threaded_caller(update_bios), optional_card_id, optional_cpu_id, requires_file),
    cmd_desc("update-MAC", new sequential_caller(update_mac_addr), requires_card_id, requires_cpu_id, requires_mac_addr),
    cmd_desc("update-SN", new sequential_caller(set_serial_number), requires_card_id, requires_serial_number),
    cmd_desc("update-EEPROM", new sequential_caller(update_eeprom), optional_card_id, requires_file),
    cmd_desc("clear-SMB-event-log", new threaded_caller(clear_smb_event_log), optional_card_id, optional_cpu_id),
    cmd_desc("script", new sequential_caller(script), optional_card_id, optional_cpu_id, optional_file),
    cmd_desc("config-show", new sequential_caller(config_show), optional_card_id, optional_cpu_id, optional_config_param, optional_value_only_param),
    cmd_desc("config", new sequential_caller(config_change), optional_card_id, optional_cpu_id, optional_blockio_id, requires_config_param, requires_config_value),
    cmd_desc("config-use", new sequential_caller(config_use)),
    cmd_desc("config-default", new sequential_caller(config_default)),
    cmd_desc("temp", new sequential_caller(read_temp), optional_card_id, optional_cpu_id),
    cmd_desc("ICMP-watchdog", new threaded_caller(ICMP_watchdog), requires_trigger, optional_card_id, optional_cpu_id, optional_ip),
    cmd_desc("network", new sequential_caller(get_network_info), get_subcmds("network"), requires_subcommand, optional_card_id, optional_cpu_id),
    cmd_desc("info", new sequential_caller(print_info_cmd_data), get_subcmds("info"), requires_subcommand, optional_card_id, optional_cpu_id),
    cmd_desc("info-hw", new sequential_caller(print_hw_info), optional_card_id),
    cmd_desc("info-system", new sequential_caller(print_system_info)),
    cmd_desc("pwrbtn-short", new threaded_caller(toggle_power_button), optional_card_id, optional_cpu_id),
    cmd_desc("pwrbtn-long", new threaded_caller(hold_power_button), optional_card_id, optional_cpu_id),
    cmd_desc("help", new sequential_caller(help)),
    cmd_desc("blockio", new sequential_caller(blockio_ctl), get_subcmds("blockio"), requires_subcommand, optional_card_id, optional_cpu_id,
						  optional_blockio_id, optional_blockio_type, optional_blockio_type_param),
    cmd_desc("os-shutdown", new sequential_caller(os_shutdown), optional_card_id, optional_cpu_id),
    cmd_desc("get-BIOS-cfg", new sequential_caller(get_bios_cfg), optional_card_id, optional_cpu_id, optional_bios_cfg_name),
    cmd_desc("set-BIOS-cfg", new threaded_caller(set_bios_cfg), optional_card_id, optional_cpu_id, requires_bios_cfg_name_and_value_pairs),
    cmd_desc("id-led", new threaded_caller(id_led), get_subcmds("id-led"), requires_subcommand, optional_card_id)
};


static const cmd_desc debug_cmds_desc[] = {
    cmd_desc("boot-USB", new threaded_caller(boot_USB), optional_card_id, optional_cpu_id),
    cmd_desc("set-SMB-id", new sequential_caller(set_SMB_id), requires_card_id, requires_smb_id),
    cmd_desc(GOLD_CMD, new threaded_caller(gold_control), requires_subcommand, optional_card_id, optional_cpu_id),
};

const cmd_desc * get_command(const char * func_name)
{
	if(config.get_global_field(global_fields::debug_enabled).get_number() == 1)
		for(const cmd_desc& i: debug_cmds_desc)
			if( strcmp( func_name, i.name)== 0)
				return &i;
	for(const cmd_desc& i: commands_desc)
		if( strcmp( func_name, i.name)== 0)
			return &i;
	return NULL;
}

void single_call(function_caller &f, int card_id, int cpu_id, args_holder &holder)
{
	f.call(caller_data(card_id, cpu_id, holder));
}

void single_card_call(function_caller &f, int card_id, args_holder &holder)
{
	f.call(caller_data(card_id, holder));
}

void all_cpus(function_caller &f, int card_id, args_holder &holder)
{
	for (int j=0, last= get_last_cpu(card_id); j<= last; ++j)
		f.call(caller_data(card_id, j, holder));
}

void all_cards(function_caller &f, args_holder &holder)
{
	int cards_num = get_cards_num();
	for (int i = 0; i < cards_num; i++)
		single_card_call(f, i, holder);
}

void all_cards_all_cpus(function_caller &f, args_holder &holder)
{
	int cards_num = get_cards_num();
	for (int i = 0; i < cards_num; i++)
		all_cpus(f, i, holder);
}

bool parse_and_execute(const cmd_desc *cmd, args_holder &holder, char *argv[])
{
	std::string cmd_name = std::string(cmd->name);

	if (!cmd->parse_args(argv, holder))
		return false;

	int available_nodes = count_available_nodes();
	if (available_nodes == FAIL) {
		LOG_ERROR("Count available devices failed!\n");
		return false;
	}
	else if (available_nodes == 0) {
		LOG_DEBUG("No nodes are available!\n");
		return false;
	}

	if (!holder.is_modprobe_check_skipped() && !devices_ready(holder, available_nodes)) {
		return false;
	}

	const data_field *card_id = holder(CARD_ID_ARG);
	const data_field *cpu_id = holder(CPU_ID_ARG);

	if (cmd_name == UPDATE_EEPROM_CMD || cmd_name == VCA_HW_INFO_CMD ||
	    (cmd_name == INFO_CMD && (holder.get_arg(SUBCMD) == INFO_SUBCMD_HW))) {
		if (!card_id) {
			all_cards(*cmd->caller, holder);
			return true;
		}
		else {
			single_card_call(*cmd->caller, card_id->get_number(), holder);
			return true;
		}
	}
	else if (cmd_name == CONFIG_DEFAULT_CMD || cmd_name == VCA_SYS_INFO_CMD ||
		(cmd_name == INFO_CMD && (holder.get_arg(SUBCMD) == INFO_SUBCMD_SYSTEM))) {
		single_call(*cmd->caller, 0, 0, holder);
		return true;
	}
	else if (!card_id) {
		all_cards_all_cpus(*cmd->caller, holder);
		return true;
	}
	else if (card_id && !cpu_id) {
		all_cpus(*cmd->caller, card_id->get_number(), holder);
		return true;
	}
	else if (card_id && cpu_id) {
		single_call(*cmd->caller, card_id->get_number(), cpu_id->get_number(), holder);
		return true;
	}
	else {
		LOG_ERROR("Cannot parse vcactl command!");
		return false;
	}
}

int main(int argc, char *argv[])
{
	try {
		const cmd_desc *cmd;
		args_holder arg_holder;

		if (argc >= 2) {
			log_args(argc, argv);
		}

		/* start after program name */
		int parsed_args = 1;

		/* execution mode params at begining */
		while (parsed_args < argc) {
			if (arg_holder.process_execution_flag(argv[parsed_args])) {
				++parsed_args;
			} else {
				break;
			}
		}

		/* execution mode params at the end of cmdline */
		while (parsed_args < argc) {
			if (arg_holder.process_execution_flag(argv[argc - 1])) {
				--argc; // drop last one - it was execution mode flag
				argv[argc] = NULL; // and prevent using it as command param
			} else {
				break;
			}
		}

		if (parsed_args == argc) {
			LOG_WARN("Missing command!\n");
			print_help();
			exit(EINVAL);
		}

		/* for command config-default xml config file shouldn't be loaded, because it can be broken */
		if (strcmp(argv[parsed_args], CONFIG_DEFAULT_CMD) && !load_vca_config())
			exit(EIO);

		/* get command name */
		cmd = get_command(argv[parsed_args]);
		++parsed_args;

		thread_manager *thread_mgr = new thread_manager();
		threaded_caller::set_thread_manager(thread_mgr);

		if (!cmd) {
			command_err = EINVAL;
			LOG_ERROR("Wrong command!\n\n");
			print_help();
		}
		else {
			/* success indicates that all commands were executed,
			but it does not mean that each of these commands finished succesfully */
			arg_holder.set_cmd_name(cmd->name);
			bool success = parse_and_execute(cmd, arg_holder, argv + parsed_args);
			if (!success)
				command_err = EINVAL;
		}

		/* ~thread_manager waits for all threads to finish before exitting */
		delete thread_mgr;

		return command_err;
	}
	catch(...) {
		return 127;
	}
}
