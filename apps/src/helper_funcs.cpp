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

bool is_unsigned_number(const char *str)
{
	int len = strlen(str);
	for (int i = 0; i < len; i++)
		if (!isdigit(str[i]))
			return false;
	return len > 0;
}

bool is_hex_digit(char digit)
{
	if ((digit > 47 && digit < 58) ||
		(digit > 64 && digit < 71) ||
		(digit > 96 && digit < 103))
		return true;
	return false;
}

bool is_ip_address(const char *str)
{
	boost::asio::ip::address a;
	try {
		a = boost::asio::ip::address::from_string(str);
	}
	catch (...) {
		return false;
	}

	return a.is_v4();
}

bool is_correct_parameter(const char *str, long param_min, long param_max)
{
	char *ptr;
	long num = strtol(str, &ptr, 0);
	if (*ptr != '\0')
		return false;
	return num >= param_min && num <= param_max;
}

bool is_root()
{
	return (getuid() == 0);
}


bool file_exists(const char *filename)
{
    struct stat buf;
    if( stat( filename, &buf) == 0)
        return S_ISREG( buf.st_mode);
    return false;
}

size_t get_file_size(const char *filename)
{
	struct stat buf;
	stat(filename, &buf);
	return buf.st_size;
}


/* On success function get_id() return id of 'vcausers_default' user or
 * 'vcausers' group, if it fail then -1 is being returned.
 * Parameter 'kind' decide which id will be returned */
int get_id(enum unix_ownership_kind kind)
{
	char output[SMALL_OUTPUT_SIZE] = "";
	unsigned int id = 0;
	const char *cmd;

	switch (kind)
	{
	case USER:
		cmd = "id -u vcausers_default 2>&1";
		break;
	case GROUP:
		cmd = "id -g vcausers_default 2>&1";
		break;
	default:
		return FAIL;
	}

	if (run_cmd_with_output(cmd, output, sizeof(output)) == FAIL) {
		common_log("Cannot execute: %s\n", cmd);
		return FAIL;
	}

	id = atoi(output);

	if (!id) {
		if (kind == USER)
			common_log("Cannot find user 'vcausers_default'!\n");
		else if (kind == GROUP)
			common_log("Cannot find group 'vcausers'!\n");

		return FAIL;
	}
	else
		return id;
}

int apply_va_min_free_memory()
{
	char output[PAGE_SIZE] = "";
	const char *cmd;

	cmd = "sysctl -w vm.min_free_kbytes=" MIN_MEM_FREE_OF_CACHE_HOST_SIDE " 2>&1";

	if (run_cmd_with_output(cmd, output, sizeof(output)) == FAIL) {
		common_log("Cannot execute: %s\n", cmd);
		return FAIL;
	}
	else
		return SUCCESS;
}

int get_vcausers_default_user_id()
{
	return get_id(USER);
}

int get_vcausers_group_id()
{
	return get_id(GROUP);
}

/* On success function drop_root_privileges() change privileges of
 * process and return 0, if it fail then -1 is being returned. */
int drop_root_privileges()
{
	int vca_default_user = 0;
	int vcausers_group = 0;
	int rc = SUCCESS;

	vca_default_user = get_vcausers_default_user_id();
	vcausers_group = get_vcausers_group_id();

	if (vca_default_user == FAIL || vcausers_group == FAIL)
		return FAIL;

	rc = setregid(vcausers_group, vcausers_group);
	if (rc == FAIL) {
		common_log("Cannot change real and effective group id"
			" of vca daemon process!\n");
		return rc;
	}

	rc = setreuid(vca_default_user, vca_default_user);
	if (rc == FAIL) {
		common_log("Cannot change real and effective user id"
			" of vca daemon process!\n");
		return rc;
	}

	return rc;
}

/* On success function change_group_and_mode() will change group to 'vcausers',
 * and mode to 664 (owner read/write, group read/write,
 * others read-only). Parameter 'file' is a path to file which will be changed. */
int change_group_and_mode(const char *file)
{
	int rc = SUCCESS;

	rc = chown(file, -1, get_vcausers_group_id());
	if (rc == FAIL) {
		common_log("Cannot change group ownership of a file '%s': %s\n", file, strerror(errno));
		return rc;
	}

	rc = chmod(file, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
	if (rc == FAIL) {
		common_log("Cannot change permissions of a file '%s': %s\n", file, strerror(errno));
		return rc;
	}

	return rc;
}

int _run_cmd(const char *cmdline, bool is_output_needed, char *output, unsigned int output_size)
{
	pid_t pid;
	int status;
	int output_fd[2];

	struct rlimit rl;

	char const *const args[] = { "/bin/sh", "-c", cmdline, NULL };
	char *sys_path = (char *)VCA_SAFE_PATH;
	char const *const env[] = { sys_path, NULL };

	getrlimit(RLIMIT_STACK, &rl);

	size_t arg_size = strlen(args[0])+1 + strlen(args[1])+1 + strlen(args[2])+1 + 1
			+ strlen(env[0])+1 + 1; // +1 for each terminating NULL (both in strings and arrays)

	if (arg_size >= rl.rlim_cur && rl.rlim_cur != RLIM_INFINITY) {
		common_log("Too long argument's length for execve() command!");
		return FAIL;
	}

	if (pipe(output_fd) == FAIL) {
		common_log("Cannot create pipe for execve output: %s!\n",
			strerror(errno));
		return FAIL;
	}

	pid = fork();
	switch (pid) {
	case 0: // child process
		/* redirecting stdout/stderr to pipe in case of read it by parent process */
		close(output_fd[0]);
		dup2(output_fd[1], STDOUT_FILENO);
		dup2(output_fd[1], STDERR_FILENO);
		close(output_fd[1]);

		execve(args[0], (char **)args, (char **)env);
		/* when execve() failed, then error message is printed to pipe
		 * to get error back in parent process */
		common_log("%s\n", strerror(errno));
		exit(EXIT_FAILURE);
	case FAIL:
		common_log("Fork failed!\n");
		return FAIL;
	default: // parent process
		char error_output[BUFFER_SIZE];
		int len;

		if (waitpid(pid, &status, WUNTRACED | WCONTINUED) == FAIL) {
			common_log("Waitpid() error: %s\n", strerror(errno));
			close(output_fd[0]);
			close(output_fd[1]);
			return FAIL;
		}
		else if (WIFEXITED(status) && (WEXITSTATUS(status) != 0)) {
			close(output_fd[1]);
			/* to fit null-terminator we read 1 char less than array size */
			len = read(output_fd[0], error_output, sizeof(error_output) - 1);
			if (len == FAIL) {
				common_log("Cannot read output from execve process: %s\n",
					strerror(errno));
				close(output_fd[0]);
				return FAIL;
			}
			else {
				if (len > 0 && error_output[len - 1] == '\n')
					error_output[len - 1] = '\0';
				else
					error_output[len] = '\0';
			}

			close(output_fd[0]);
			common_log("Executing execve command failed: %s\n", error_output);
			return FAIL;
		}
		if (is_output_needed) {
			close(output_fd[1]);
			if (output == NULL) {
				common_log("Cannot return output, dedicated buffer is NULL!");
				close(output_fd[0]);
				return FAIL;
			}
			else if (output_size == 0) {
				common_log("Cannot return output, dedicated buffer size is 0!");
				close(output_fd[0]);
				return FAIL;
			}

			/* to fit null-terminator we read 1 char less than array size */
			len = read(output_fd[0], output, output_size - 1);
			if (len == FAIL) {
				common_log("Cannot read output from execve process: %s\n", strerror(errno));
				close(output_fd[0]);
				return FAIL;
			}
			else {
				if (len > 0 && output[len - 1] == '\n')
					output[len - 1] = '\0';
				else
					output[len] = '\0';
			}

			close(output_fd[0]);
		}
		else {
			close(output_fd[0]);
			close(output_fd[1]);
		}
	}
	return SUCCESS;
}

int run_cmd(const char *cmdline)
{
	return _run_cmd(cmdline, false, NULL, 0);
}

int run_cmd_with_output(const char *cmdline, char *output, unsigned int output_size)
{
	return _run_cmd(cmdline, true, output, output_size);
}

static filehandle_t file_create_or_open(const char *file_name, int flags)
{
	int rc;
	filehandle_t fd = -1;

	/* first try to create file - getting error in case it already exists */
	fd = open(file_name, O_CREAT | O_EXCL | flags,
		S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if (-1 == fd)
	{
		if (errno != EEXIST) {
			common_log("Cannot open file %s: %s!\n",
				file_name, strerror(errno));
			return fd;
		}

		/* file exists - open and exit */
		fd = open(file_name, O_CREAT | flags,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (-1 == fd) {
			common_log("Cannot open file %s: %s!\n",
				file_name, strerror(errno));
			return fd;
		}
		else {
			/* file created - set file permissions */
			rc = change_group_and_mode(file_name);
			if (rc == FAIL) {
				close(fd);
				return -1;
			}
		}
	}

	return fd;
}

filehandle_t lock_file(const char *lock_file_path)
{
	int locked_file;
	char buffer[SMALL_OUTPUT_SIZE];
	struct flock fl;
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if ((locked_file = file_create_or_open(lock_file_path, O_WRONLY|O_CLOEXEC)) == -1) {
		common_log("Cannot open/create lock file %s : %s!\n", lock_file_path, strerror(errno));
		return FAIL;
	}

	snprintf(buffer, SMALL_OUTPUT_SIZE, "%ld\n", (long)getpid());
	if (write(locked_file, buffer, strlen(buffer)) < 0) {
		close(locked_file);
		common_log("Cannot write pid number to lock file %s!\n", lock_file_path);
		return FAIL;
	}

	if (fcntl(locked_file, F_SETLK, &fl) < 0) {
		close(locked_file);
		common_log("Cannot lock file %s. Already locked.\n", lock_file_path);
		return FAIL;
	}

	return locked_file;
}

/* c++ functions */

std::string int_to_string(int val)
{
	std::stringstream ss;
	ss << val;
	return ss.str();
}

std::string char_to_string(char *c_string)
{
	std::string s = c_string;
	return s;
}

bool is_forcing_cmd_confirmed()
{
	std::string confirmation;

	std::getline(std::cin, confirmation);

	if (confirmation == "yes" || confirmation == "y" ||
	    confirmation == "Yes" || confirmation == "Y" ||
	    confirmation == "YES") {
		return true;
	}

	return false;
}

boost::posix_time::ptime get_time()
{
	boost::date_time::microsec_clock<boost::posix_time::ptime> t;
	return t.local_time();
}

unsigned int get_passed_time_ms(const boost::posix_time::ptime start)
{
	boost::posix_time::time_period tp(start, get_time());
	return tp.length().total_milliseconds();
}


/* replace all occurences of 'what' string in 'str' with 'with' string */
int replace_all(std::string &str, const std::string &what, const std::string &with)
{
	if (str.empty() || what.empty())
		return 0;
	size_t start_pos = 0;
	int count = 0;
	while ((start_pos = str.find(what, start_pos)) != std::string::npos)
	{
		str.replace(start_pos, what.length(), with);
		start_pos += with.length();
		count++;
	}

	return count;
}

bool could_open_file(const char *file_path)
{
	close_on_exit fd(open_path(file_path, O_RDONLY));
	return fd;
}

filehandle_t open_path(const char* path, int flags)
{
	filehandle_t fd = open(path, flags);
	if (fd < 0) {
		LOG_FULL("Could not open %s\n", path);
	}
	return fd;
}

namespace Printer {

e_verbose verbose = VERBOSE_DEFAULT;

}; // end of namespace Printer
