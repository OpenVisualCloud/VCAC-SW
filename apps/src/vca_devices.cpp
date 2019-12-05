#include "vca_devices.h"
#include "vca_mgr_ioctl.h"
#include "vca_mgr_extd_ioctl.h"
#include "vca_csm_ioctl.h"

#include <dirent.h>

unsigned get_cards_num() {
	unsigned cnt = 0;
	for (unsigned j = 0; j < MAX_CARDS; ++j) {
		cnt += card_exists(j);
	}
	return cnt;
}

bool card_exists(int card_id) {
    struct stat i;
    std::string const path=VCA_MGR_DEV_PATH + int_to_string(card_id);
    if( stat( path.c_str(), &i) == 0)
        return S_ISCHR( i.st_mode);
    return false;
}

bool vca_ioctl(filehandle_t fd, unsigned long ioctl_cmd, void *arg) {
	int rc = ioctl(fd, ioctl_cmd, arg);
	if (rc != SUCCESS) {
		LOG_ERROR("%s failed: %s!\n", get_vca_ioctl_name(ioctl_cmd), strerror(errno));
		return false;
	}
	return true;
}

const char *get_vca_ioctl_name(unsigned long ioctl_cmd) {
	switch(ioctl_cmd) {
	case VCA_READ_CARD_TYPE:
		return "VCA_READ_CARD_TYPE";
	case VCA_READ_CPU_NUM:
		return "VCA_READ_CPU_NUM";
	case VCA_RESET:
		return "VCA_RESET";
	case VCA_POWER_BUTTON:
		return "VCA_POWER_BUTTON";
	case VCA_SET_SMB_ID:
		return "VCA_SET_SMB_ID";
	case VCA_UPDATE_EEPROM:
		return "VCA_UPDATE_EEPROM";
	case VCA_UPDATE_SECONDARY_EEPROM:
		return "VCA_UPDATE_SECONDARY_EEPROM";
	case VCA_READ_MODULES_BUILD:
		return "VCA_READ_MODULES_BUILD";
	case VCA_READ_BOARD_ID:
		return "VCA_READ_BOARD_ID";
	case VCA_READ_EEPROM_CRC:
		return "VCA_READ_EEPROM_CRC";
	case VCA_ENABLE_GOLD_BIOS:
		return "VCA_ENABLE_GOLD_BIOS";
	case VCA_DISABLE_GOLD_BIOS:
		return "VCA_DISABLE_GOLD_BIOS";
	case VCA_CHECK_POWER_BUTTON:
		return "VCA_CHECK_POWER_BUTTON";
	default:
		LOG_DEBUG("vca ioctl command name for %lx not found!\n", ioctl_cmd);
		return "";
	};
}

enum vca_card_type get_card_type(int card_id)
{
	close_on_exit card_fd(open_card_fd(card_id));
	if (!card_fd)
		return VCA_UNKNOWN;
	vca_card_type type;
	if (!vca_ioctl(card_fd, VCA_READ_CARD_TYPE, &type))
		return VCA_UNKNOWN;
	return type;
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
