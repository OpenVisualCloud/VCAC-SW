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
 * Intel Virtio Over PCIe (VOP) driver.
 *
 */
#ifndef _VOP_MAIN_H_
#define _VOP_MAIN_H_

#include <linux/miscdevice.h>
#include "../vca_virtio/uapi/vca_virtio_config.h"
#include "../vca_virtio/include/vca_virtio.h"

#if !defined(USHRT_MAX)
#define USHRT_MAX	((u16)(~0U))
#endif

#ifdef VCA_IN_KERNEL_BUILD
#include <linux/vca_dev_common.h>
#else
#include "../common/vca_dev_common.h"
#endif
#include "../common/vca_dev.h"
#include "vop_common.h"

#include "../bus/vop_bus.h"

/*
 * Note on endianness.
 * 1. Host can be both BE or LE
 * 2. Guest/card is LE. Host uses le_to_cpu to access desc/avail
 *    rings and ioreadXX/iowriteXX to access used ring.
 * 3. Device page exposed by host to guest contains LE values. Guest
 *    accesses these using ioreadXX/iowriteXX etc. This way in general we
 *    obey the virtio spec according to which guest works with native
 *    endianness and host is aware of guest endianness and does all
 *    required endianness conversion.
 * 4. Data provided from user space to guest (in ADD_DEVICE and
 *    CONFIG_CHANGE ioctl's) is not interpreted by the driver and should be
 *    in guest endianness.
 */

/*
 * vop_info - Allocated per invocation of VOP probe
 *
 * @vpdev: VOP device
 * @hotplug_work: Handle virtio device creation, deletion and configuration
 * @cookie: Cookie received upon requesting a virtio configuration interrupt
 * @h2c_config_db: The doorbell used by the peer to indicate a config change
 * @vdev_list: List of "active" virtio devices injected in the peer node
 * @vop_mutex: Synchronize access to the device page as well as serialize
 *             creation/deletion of virtio devices on the peer node
 * @dma_ch: The DMA channel used by this transport for data transfers.
 * @name: Name for this transport used in misc device creation.
 * @miscdev: The misc device registered.
 * @dbg: Debugfs entry
 */
struct vop_info {
	struct vop_device *vpdev;
	struct work_struct hotplug_work;
	struct vca_irq *cookie;
	int h2c_config_db;
	struct list_head vdev_list;
	struct mutex vop_mutex;
	struct dma_chan *dma_ch;
	char name[16];
	struct miscdevice miscdev;
	struct {
		struct dentry *debug_fs;
		unsigned long dma_test_pages;
	} dbg;
};


struct vop_host_virtio_dev;

#define VRING_INDEX_RECV 0
#define VRING_INDEX_SEND 1

/**
 * struct vop_vringh - Virtio ring host information.
 *
 * @vring: The VOP vring used for setting up user space mappings.
 * @vrh: The host VRINGH used for accessing the card vrings.
 * @riov: The VRINGH read kernel IOV.
 * @wiov: The VRINGH write kernel IOV.
 * @head: The VRINGH head index address passed to vringh_getdesc_kern(..).
 * @vr_mutex: Mutex for synchronizing access to the VRING.
 * @buf: Temporary kernel buffer used to copy in/out data
 * from/to the card via DMA.
 * @buf_da: dma address of buf.
 * @vdev: Back pointer to VOP virtio device for vringh_notify(..).
 */
struct vop_vringh {
	struct vca_vring vring;
	struct vringh vrh;
	spinlock_t vr_spinlock;

	/* TODO remove fields specific to host/card */
	struct vop_card_virtio_dev *vdev;
	void *_vop_vdev;

	int index;
	struct virtqueue *vq;
	wait_queue_head_t complete_queue;
	unsigned int cookie, completed_cookie;


	int remove_me_ready;
};

/* data for implementing virtio device on host side */
struct vop_host_virtio_dev {

	int virtio_id;
	struct vop_device *vdev;
	struct virtio_device virtio_device;
	struct vca_device_desc *dd;
	struct vca_device_ctrl *dc;

	// vring to access memory of host virtio device
	struct vop_vringh vop_vringh[VCA_MAX_VRINGS];
	dma_addr_t pa[VCA_MAX_VRINGS];

	wait_queue_head_t state_change;
	bool ready;
};

/**
 * struct vop_card_virtio_dev - Host information for a card Virtio device.
 *
 * @virtio_id - Virtio device id.
 * @waitq - Waitqueue to allow ring3 apps to poll.
 * @vpdev - pointer to VOP bus device.
 * @poll_wake - Used for waking up threads blocked in poll.
 * @out_bytes - Debug stats for number of bytes copied from host to card.
 * @in_bytes - Debug stats for number of bytes copied from card to host.
 * @out_bytes_dma - Debug stats for number of bytes copied from host to card
 * using DMA.
 * @in_bytes_dma - Debug stats for number of bytes copied from card to host
 * using DMA.
 * @tx_len_unaligned - Debug stats for number of bytes copied to the card where
 * the transfer length did not have the required DMA alignment.
 * @tx_dst_unaligned - Debug stats for number of bytes copied where the
 * destination address on the card did not have the required DMA alignment.
 * @vvr - Store per VRING data structures.
 * @virtio_bh_work - Work struct used to schedule virtio bottom half handling.
 * @dd - Virtio device descriptor.
 * @dc - Virtio device control fields.
 * @list - List of Virtio devices.
 * @virtio_db - The doorbell used by the card to interrupt the host.
 * @virtio_cookie - The cookie returned while requesting interrupts.
 * @vi: Transport information.
 * @vdev_mutex: Mutex synchronizing virtio device injection,
 *              removal and data transfers.
 * @destroy: Track if a virtio device is being destroyed.
 * @deleted: The virtio device has been deleted.
 * @cdev: common transport data
 * @host: host side device info
 */
struct vop_card_virtio_dev {
	int virtio_id;
	wait_queue_head_t waitq;
	struct vop_device *vpdev;
	int poll_wake;
	unsigned long out_bytes;
	unsigned long in_bytes;
	unsigned long out_bytes_dma;
	unsigned long in_bytes_dma;
	unsigned long tx_len_unaligned;
	unsigned long tx_dst_unaligned;
	unsigned long rx_dst_unaligned;
	/*
	struct vop_vringh vvr[VCA_MAX_VRINGS];
	dma_addr_t pa[VCA_MAX_VRINGS];
	*/
	struct work_struct virtio_bh_work;
	struct vca_device_desc *dd;
	struct vca_device_ctrl *dc;
	struct list_head list;
	int virtio_conf_db;
	int virtio_avail_db;
	int virtio_used_db;
	struct vca_irq *virtio_conf_db_cookie;
	struct vca_irq *virtio_avail_db_cookie;
	struct vca_irq *virtio_used_db_cookie;
	struct vop_info *vi;
	struct mutex vdev_mutex;
	struct completion destroy;
	bool deleted;
	struct vop_dev_common cdev;
	struct vop_host_virtio_dev host;
};

#define VOP_MAX_VRINGS 4

/*
 * _vop_vdev - Allocated per virtio device instance injected by the peer.
 *
 * @vdev: Virtio device
 * @list: list of _vop_dev
 * @desc: Virtio device page descriptor
 * @dc: Virtio device control
 * @vpdev: VOP device which is the parent for this virtio device
 * @vr: Buffer for accessing the VRING
 * @used: Buffer for used
 * @reset_done: Track whether VOP reset is complete
 * @virtio_cookie: Cookie returned upon requesting a interrupt
 * @c2h_vdev_db: The doorbell used by the guest to interrupt the host
 * @h2c_vdev_db: The doorbell used by the host to interrupt the guest
 * @dnode: The destination node
 */
struct _vop_vdev {
	struct virtio_device vdev;
	struct list_head list;
	struct vca_device_desc __iomem *desc;
	struct vca_device_ctrl __iomem *dc;
	struct vop_device *vpdev;
	void __iomem *vr[VOP_MAX_VRINGS];
	dma_addr_t pa[VOP_MAX_VRINGS];
	size_t pa_size[VOP_MAX_VRINGS];
	struct completion reset_done;
	struct vca_irq *virtio_conf_db_cookie;
	struct vca_irq *virtio_avail_db_cookie;
	struct vca_irq *virtio_used_db_cookie;
	int c2h_vdev_conf_db;
	int c2h_vdev_avail_db;
	int c2h_vdev_used_db;
	int h2c_vdev_conf_db;
	int h2c_vdev_avail_db;
	int h2c_vdev_used_db;
	int dnode;

	struct vop_dev_common cdev;

	int virtio_id;

	struct vop_vringh vop_vringh[VCA_MAX_VRINGS];

	struct {
		struct vca_device_desc __iomem *desc;
		struct vca_device_ctrl __iomem *dc;
	} host;

	u32 status;
};



/* Helper API to check if a virtio device is running */
static inline bool vop_vdevup(struct vop_card_virtio_dev *vdev)
{
	return !!vdev->dd->status;
}

void vop_init_debugfs(struct vop_info *vi);
void vop_exit_debugfs(struct vop_info *vi);
int vop_host_init(struct vop_info *vi);
void vop_host_uninit(struct vop_info *vi);
void vop_stop_network_traffic(struct vop_device *vpdev);

#endif
