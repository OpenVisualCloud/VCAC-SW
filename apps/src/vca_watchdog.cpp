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

#include "vcassd_common.h"
#include "vca_config_parser.h"
#include "helper_funcs.h"

extern struct vca_config *config;

extern "C" void *vca_ping_daemon(void *arg)
{
	struct vca_info *vca = (struct vca_info *)arg;
	vcasslog("%s: ping daemon started!\n", vca->name);
	volatile bool &finish = vca->vca_ping.daemon_ti.finish;

	char output[BUFFER_SIZE] = "";
	char ping_output[SMALL_OUTPUT_SIZE] = "";
	std::string ip;
	int ret = 0;

	/* wait for os to be ready */
	for(; VCA_NET_DEV_READY!=get_vca_state(vca); sleep(1))
		if (finish)
			goto finish;

	/* does not needs lock, because this thread would be dead when reloading config */
	if (strlen(vca->vca_ping.ip_address))
		ip = vca->vca_ping.ip_address;
	else {
		/* We don't need to check length of source string, because
		 * card_id and cpu_id is being validated when they were created.
		 * Format also is known and it is constant. All together is
		 * good enough to fit in buffer[1024].
		 */
		std::string ip_cmd = "vcactl  network ip "
				   + int_to_string(vca->card_id) + " "
				   + int_to_string(vca->cpu_id);

		if (run_cmd_with_output(ip_cmd.c_str(), output, sizeof(output)) == FAIL) {
			vcasslog("Cannot execute: %s\n", ip_cmd.c_str());
			finish = false;
			return NULL;
		}
		if (is_ip_address(output)) {
			ip = output;
			vcasslog("Ip address for Card %d Cpu %d: %s\n",
				vca->card_id, vca->cpu_id, ip.c_str());
		}
		else {
			vcasslog("Ip address received from Card %d Cpu %d is incorrect.\n",
				vca->card_id, vca->cpu_id);
			finish = false;
			return NULL;
		}
	}
	{
		int max_response_time =
				config->get_global_field(global_fields::ICMP_response_timeout_s).get_number();
		int interval =
				config->get_global_field(global_fields::ICMP_ping_interval_s).get_number();

		std::string ping_cmd = PING " -c 1 "
				" -w " + int_to_string(max_response_time)
				+ " -i " + int_to_string(interval)
				+ " " + ip + " 2>&1 > /dev/null ; " ECHO " $?";
		/* ping while getting response */
		do {
			if (run_cmd_with_output(ping_cmd.c_str(), ping_output,
									sizeof(ping_output)) == FAIL) {
				vcasslog("Cannot execute: %s\n", ping_cmd.c_str());
				goto finish;
			}
			if (atoi(ping_output) != SUCCESS) {
				ret = -1;
			}
			else
				sleep(interval);

		} while (ret != -1 && !finish);
	}
	/* when ping don't get response, then need to execute appropriate script */
	if (ret == -1 && !finish) {
		const char *script = config->get_cpu_field(vca->card_id, vca->cpu_id,
			cpu_fields::daemon_script).get_cstring();

		if (!script)
			script = config->get_global_field(global_fields::default_daemon_script).get_cstring();

		std::string script_cmd = "/bin/bash " + std::string(script) + " "
					+ int_to_string(vca->card_id) + " "
					+ int_to_string(vca->cpu_id);

		if (script_cmd.length() < BUFFER_SIZE) {
			if (run_cmd(script_cmd.c_str()) == FAIL)
				vcasslog("Cannot execute: %s\n", script_cmd.c_str());
		}
		else
			vcasslog("Error: Command for executing daemon script is too long!"
				" Please decrease length of value passed into xml file.\n");
	}
finish:
	finish = false;
	vcasslog("%s: ping daemon ended!\n", vca->name);
	return NULL;
}
