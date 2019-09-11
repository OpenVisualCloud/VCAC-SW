#include "vca_devices.h"

unsigned get_cards_num() {
	unsigned cnt = 0;
	for (unsigned j = 0; j < MAX_CARDS; ++j) {
		cnt += card_exists(j);
	}
	return cnt;
}

bool card_exists(int card_id) {
	return character_device_exists((VCA_MGR_DEV_PATH + int_to_string(card_id)).c_str());
}

int count_available_nodes() {
	char output[SMALL_OUTPUT_SIZE] = "";

	const char *lspci_cmd = "lspci -n | grep '8086:295[4-6]' | wc -l";
	if (run_cmd_with_output(lspci_cmd, output, sizeof(output)) == FAIL) {
		LOG_ERROR("Cannot execute: %s\n", lspci_cmd);
		return FAIL;
	}
	return atoi(output);
}

int count_ready_nodes() {
	DIR *dp;
	struct dirent *file;
	int count = 0;
	std::string cpu_path;

	dp = opendir(VCASYSFSDIR);
	if (!dp)
		return FAIL;

	while ((file = readdir(dp)) != NULL) {
		if (strncmp(file->d_name, "vca", 3) != 0) {
			// to exclude "." and ".."
			continue;
		}
		cpu_path = DEV_PATH + std::string(file->d_name);
		count += could_open_file(cpu_path.c_str());
	}
	closedir(dp);
	return count;
}

bool is_node_ready(int card_id, int cpu_id) {
	// check if sysfs and devfs files exists and are readable
	const std::string sysfs_file_name = VCASYSFSDIR "/vca" + int_to_string(card_id) + int_to_string(cpu_id);
	const std::string devfs_file_name = VCA_CPU_DEV_PATH + int_to_string(card_id) + int_to_string(cpu_id);

	return could_open_file(sysfs_file_name.c_str()) && could_open_file(devfs_file_name.c_str());
}

filehandle_t open_card_fd(int card_id) {
	return open_path((VCA_MGR_DEV_PATH + int_to_string(card_id)).c_str());
}

filehandle_t open_extd_card_fd(int card_id) {
	return open_path((VCA_MGR_EXTD_DEV_PATH + int_to_string(card_id)).c_str());
}

filehandle_t open_cpu_fd(int card_id, int cpu_id) {
	return open_path((VCA_CPU_DEV_PATH + int_to_string(card_id) + int_to_string(cpu_id)).c_str());
}

filehandle_t open_blk_fd(int card_id, int cpu_id) {
	return open_path((VCA_BLK_DEV_PATH + int_to_string(card_id) + int_to_string(cpu_id)).c_str());
}

filehandle_t open_pxe_fd(int card_id, int cpu_id) {
	return open_path((VCA_PXE_DEV_PATH + int_to_string(card_id) + int_to_string(cpu_id)).c_str());
}
