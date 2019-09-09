#ifndef _VCA_DEVICES_H_
#define _VCA_DEVICES_H_

#include "helper_funcs.h"

#define DEV_PATH		"/dev/"
#define VCA_MGR_DEV_PATH	"/dev/vca_mgr"
#define VCA_MGR_EXTD_DEV_PATH	"/dev/vca_mgr_extd"
#define VCA_CPU_DEV_PATH	"/dev/vca"
#define VCA_BLK_DEV_PATH	"/dev/vca_blk_bcknd"

bool card_exists(int card_id);
unsigned get_cards_num();
int count_available_nodes();
int count_ready_nodes();
bool is_node_ready(int card_id, int cpu_id);
filehandle_t open_card_fd(int card_id);
filehandle_t open_extd_card_fd(int card_id);
filehandle_t open_cpu_fd(int card_id, int cpu_id);
filehandle_t open_blk_fd(int card_id, int cpu_id);

#endif // _VCA_DEVICES_H_
