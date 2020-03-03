/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2017 Intel Corporation.
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

#include <sys/types.h>
#include <sys/file.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sstream>
#include <fstream>
#include <cstdio>

#include "helper_funcs.h"

const char LOGFILE_OLD[]    = "/var/log/vca/vcactl.log.old";
const char LOGFILE_CURR[]   = "/var/log/vca/vcactl.log";
const char LOGFILE_HEADER[] = "date\tpid\tuptime\tancestors\tcmd\r\n";

const off_t LOGFILE_SIZE_LIMIT = ((1 << 20) * 250); // 250MB per file

const char LOGFILE_SHM_MUTEX[] = "vcactl_logfile";
const unsigned int LOGFILE_LOCK_TIMEOUT = 3; // seconds

/* log_args() will log arguments with which program was invoked at commandline,
 * along with other useful info.
 *
 * Logs will be written in TSV (tab-separated values) format.
 *
 * Logs are written to two files defined above.
 * Logs will be always written to LOGFILE_CURR file.
 * When CURR file fills up, it will be moved to OLD one and started over.
 *
 * Exclusions:
 * - windows host ignored for now;
 * - ENV not logged, looks unused in vcactl.
 */

// helpers predeclarations
bool log_string(const std::string &msg);
std::string read_uptime();
std::string simply_quote(std::string s);
std::string parent_cmds();


bool log_args(int argc, char **argv) {
	std::ostringstream oss;

	oss << get_time()
		<< '\t' << getpid()
		<< '\t' << read_uptime()
		<< '\t' << parent_cmds()
		<< '\t' << argv[0];

	for (int i = 1; i < argc; ++i) {
		oss << ' ' << simply_quote(argv[i]);
	}

	oss << "\r\n";

	try {
		return log_string(oss.str());
	} catch (std::exception &e) { // both interprocess and std
		LOG_WARN("log_args: exception: %s\n", e.what());
	}
	return false;
}

bool log_string(const std::string &msg) {
	close_on_exit fd(open_path(LOGFILE_CURR, O_WRONLY));
	if (!fd)
		return false;
	for (time_t now = time(NULL); flock(fd, LOCK_EX|LOCK_NB); sleep(0.2))
		if (time(NULL) - now > LOGFILE_LOCK_TIMEOUT) {
			LOG_WARN("log_string: timeout while acquiring lock\n");
			return false;
		}

	struct stat stat_data = {0};
	int ret = stat(LOGFILE_CURR, &stat_data);
	if (ret == FAIL && errno != ENOENT) {
		LOG_WARN("log_string: could not stat %s: %s\n", LOGFILE_CURR, strerror(errno));
		return false;
	}

	bool needs_init = (errno == ENOENT);

	// if file grows too big, there is need to backup it and start new one
	if (stat_data.st_size >= LOGFILE_SIZE_LIMIT) {
		needs_init = true;
		ret = rename(LOGFILE_CURR, LOGFILE_OLD);
		if (ret == FAIL) {
			LOG_WARN("log_string: could not rename %s to %s: %s\n", LOGFILE_CURR, LOGFILE_OLD, strerror(errno));
			return false;
		}
	}

	std::ofstream out(LOGFILE_CURR, std::ofstream::app);
	if (needs_init) {
		out << LOGFILE_HEADER;
	}
	out << msg;
	if (!out) {
		LOG_WARN("log_string: could not open/write to %s\n", LOGFILE_CURR);
		return false;
	}

	if (needs_init) {
		out.close();
		return FAIL != change_group_and_mode(LOGFILE_CURR);
	}

	return true;
}

bool needs_escape(const std::string &s) {
	for (size_t i = 0; i < s.size(); ++i) {
		int c = s[i];
		if ( ! (isalnum(c) || c == '/' || c == '.' || c == '_' || c == '-')) {
			return true;
		}
	}
	return s.empty();
}

std::string simply_quote(std::string s) {
	if (!needs_escape(s)) {
		return s;
	}

	// avoid TABs since we are writing in TSV format
	replace_all(s, "\t", "TABULATOR");

	// method from: https://stackoverflow.com/questions/15783701/which-characters-need-to-be-escaped
	replace_all(s, "'", "'\''");
	return "'" + s + "'";
}

std::string read_uptime() {
	std::ifstream in("/proc/uptime");
	std::string uptime;
	in >> uptime;
	return uptime;
}

std::string read_procfs(const std::string &pid, const std::string &file, int field_no = 1) {
	std::ifstream in(("/proc/" + pid + '/' + file).c_str());
	std::string val;
	for (; field_no > 0; --field_no) {
		if (!(in >> val)) {
			return "";
		}
	}
	return val;
}

// walk process tree from parent up to root/init
// return space-separated names of processes on that walk (parent is last)
// remove duplicated entries (common when using fork())
std::string parent_cmds() {
	std::string out, prev, cmd, pid = int_to_string(getppid());

	while (true) {
		cmd = read_procfs(pid, "comm");
		if (cmd != prev) {
			out = cmd + " " + out;
			prev = cmd;
		}

		pid = read_procfs(pid, "stat", 4);
		if (pid.empty() || pid == "1") {
			break;
		}
	}

	return out;
}
