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
 * Intel VCA driver.
 *
 */
#ifndef __VCA_DEV_COMMON_H_
#define __VCA_DEV_COMMON_H_

#define PLX_MEM_DEBUG 0

#include "../vca_virtio/uapi/vca_virtio_ring.h"

#define __vca_align(a, x) (((a) + (x) - 1) & ~((x) - 1))

/**
 * struct vca_device_desc: Virtio device information shared between the
 * virtio driver and userspace backend
 *
 * @type: Device type: console/network/disk etc.  Type 0/-1 terminates.
 * @num_vq: Number of virtqueues.
 * @feature_len: Number of bytes of feature bits.  Multiply by 2: one for
   host features and one for guest acknowledgements.
 * @config_len: Number of bytes of the config array after virtqueues.
 * @status: A status byte, written by the Guest.
 * @config: Start of the following variable length config.
 */
struct vca_device_desc {
	__s8 type;
	__u8 num_vq;
	__u8 feature_len;
	__u8 config_len;
	__u8 status;
	__le64 config[0];
} __attribute__ ((aligned(8)));

#define GUEST_ACK_NONE 0
#define GUEST_ACK_RECEIVED 1
#define GUEST_ACK_DONE 2

/**
 * struct vca_device_ctrl: Per virtio device information in the device page
 * used internally by the host and card side drivers.
 *
 * @vdev: Used for storing VCA vdev information by the guest.
 * @config_change: Set to 1 by host when a config change is requested.
 * @vdev_reset: Set to 1 by guest to indicate virtio device has been reset.
 * @guest_ack: Set to 1 by guest to ack a command.
 * @host_ack: Set to 1 by host to ack a command.
 * @used_address_updated: Set to 1 by guest when the used address should be
 * updated.
 * @c2h_vdev_conf_db: The doorbell number to be used by guest. Set by host.
 * @c2h_vdev_avail_db: The doorbell, new available descriptors to write.
 * @c2h_vdev_used_db: The doorbell, consumed descriptors to write.
 * @h2c_vdev_conf_db: The doorbell number to be used by host. Set by guest.
 * @h2c_vdev_avail_db: The doorbell, new available descriptors to write.
 * @h2c_vdev_used_db: The doorbell, consumed descriptors to write.
 */
struct vca_device_ctrl {
	__le64 vdev;
	__u8 config_change;
	__u8 vdev_reset;
	__u8 guest_ack;
	__u8 host_ack;
	__u8 used_address_updated;
	__s8 c2h_vdev_conf_db;
	__s8 c2h_vdev_avail_db;
	__s8 c2h_vdev_used_db;
	__s8 h2c_vdev_conf_db;
	__s8 h2c_vdev_avail_db;
	__s8 h2c_vdev_used_db;
	__le64 kvec_buf_address;
	__le32 kvec_buf_elems;
} __attribute__ ((aligned(8)));

#define VCA_TEST_FLAG_EVENT_H2C_CRASH_OS                (((__u64)1)<<63)
#define VCA_TEST_FLAG_EVENT_H2C_ALLOC_DMA_BUFF          (((__u64)1)<<62)
#define VCA_TEST_FLAG_EVENT_H2C_RUN_THREAD_DMA_BUFF     (((__u64)1)<<61)

/**
 * struct vca_bootparam: Virtio device independent information in device page
 *
 * @magic: A magic value used by the card to ensure it can see the host
 * @version_host: Version code on host to detect that modules are compatible
 * @version_card: Version code on card to detect that modules are compatible
 * @test_flags_events: Special test flags to crash OS, or start some test tasks.
 * @reserved: Reserve for feature use
 * @h2c_config_db: Host to Card Virtio config doorbell set by card
 * @shutdown_status: Card shutdown status set by card
 * @node_id: Unique id of the node
 * @h2c_scif_db - SCIF doorbell which the host will hit to trigger an interrupt
 * @c2h_scif_db - SCIF doorbell which the card will hit to trigger the host
 * @scif_host_dma_addr - SCIF host queue pair DMA address
 * @scif_card_dma_addr - SCIF card queue pair DMA address
 * @net_config_dma_addr: dma mapped memory of net_config
 * @net_config_size: size of net_config_dma_addr
 * @sys_config_dma_addr: dma mapped memory of sys_config
 * @sys_config_size: size of sys_config_dma_addr
 * @card_csa_mem_dma_addr: dma mapped csa memory from card
 * @card_csa_mem_size: size of card_csa_mem_dma_addr
 * @h2c_csa_mem_db: doorbell number to be used by csa_mem. Set by host.
 * @csa_command: command for vca_csa to execute
 * @csa_finished: signals if card mapped new memory in card_csa_mem_dma_addr
 */
struct vca_bootparam {
	__le32 magic;
	__u32 version_host;
	__u32 version_card;
	__u64 test_flags_events;
	struct { __u64 DontUseDma:1;} flags;
	__u64 reserved[2];
	__s8 h2c_config_db;
	__u8 node_id;
	__u8 h2c_scif_db;
	__u8 c2h_scif_db;
	__u64 scif_host_dma_addr;
	__u64 scif_card_dma_addr;
	__u64 net_config_dma_addr;
	__u32 net_config_size;
	__u64 sys_config_dma_addr;
	__u32 sys_config_size;
	__u64 card_csa_mem_dma_addr;
	__u32 card_csa_mem_size;
	__s8 h2c_csa_mem_db;
	__u8 csa_command;
	__u8 csa_finished;
	__u64 test_host_dma_addr;
	__u64 test_card_dma_addr;
	__u8 os_type;
	/* TODO: find better place for these members */
	__u64 blockio_dp;
	__u32 blockio_ftb_db;
	__u8 mac_addr[6];
	__u64 net_config_windows_dma_addr;
	__u32 net_config_windows_size;
} __attribute__ ((aligned(8)));

/**
 * struct vca_vqconfig: This is how we expect the device configuration field
 * for a virtqueue to be laid out in config space.
 *
 * @address: Guest/VCA physical address of the virtio ring
 * (avail and desc rings)
 * @used_address: Guest/VCA physical address of the used ring
 * @num: The number of entries in the virtio_ring
 */
struct vca_vqconfig {
	__le64 address;
	__le16 num;
} __attribute__ ((aligned(8)));

/*
 * The alignment to use between consumer and producer parts of vring.
 * This is pagesize for historical reasons.
 */
#define VCA_VIRTIO_RING_ALIGN		4096

/*
 * Maximum number of vrings/virtqueues per device.
 */
#define VCA_MAX_VRINGS			4

/*
 * Maximum length of device configuration section
 */
#define VCA_MAX_CONFIG_LEN		16

/*
 * Maximum length of device feature section
 */
#define VCA_MAX_FEATURE_LEN		16

/*
 * Vring entries (power of 2) to ensure desc and avail rings
 * fit in a single page
 */
#define VCA_VRING_ENTRIES		1024

/**
 * Max size of the desc block in bytes: includes:
 *	- struct vca_device_desc
 *	- struct vca_vqconfig (num_vq of these)
 *	- host and guest features
 *	- virtio device config space
 */
#define VCA_MAX_DESC_BLK_SIZE		256

/**
 * struct _vca_vring_info - Host vring info exposed to userspace backend
 * for the avail index and magic for the card.
 *
 * @avail_idx: host avail idx
 * @magic: A magic debug cookie.
 */
struct _vca_vring_info {
	__u16 avail_idx;
	__le32 magic;
};

/**
 * struct vca_vring - Vring information.
 *
 * @vr: The virtio ring.
 * @info: Host vring information exposed to the userspace backend for the
 * avail index and magic for the card.
 * @va: The va for the buffer allocated for vr and info.
 * @len: The length of the buffer required for allocating vr and info.
 */
struct vca_vring {
	struct vring vr;
	struct _vca_vring_info *info;
	void *va;
	int len;
};

#define vca_aligned_desc_size(d) __vca_align(vca_desc_size(d), 8)

#ifndef INTEL_VCA_CARD
static inline unsigned vca_desc_size(const struct vca_device_desc *desc)
{
	return sizeof(*desc) + desc->num_vq * sizeof(struct vca_vqconfig)
		+ desc->feature_len * 2 + desc->config_len;
}

static inline struct vca_vqconfig *
vca_vq_config(const struct vca_device_desc *desc)
{
	return (struct vca_vqconfig *)(desc + 1);
}

static inline __u8 *vca_vq_features(const struct vca_device_desc *desc)
{
	return (__u8 *)(vca_vq_config(desc) + desc->num_vq);
}

static inline __u8 *vca_vq_configspace(const struct vca_device_desc *desc)
{
	return vca_vq_features(desc) + desc->feature_len * 2;
}

static inline struct vca_device_desc *
vca_host_device_desc(const struct vca_device_desc *desc)
{
	return  (struct vca_device_desc *)((uint8_t*)desc + (vca_aligned_desc_size(desc)
		+ sizeof(struct vca_device_ctrl)));
}

static inline unsigned vca_total_desc_size(struct vca_device_desc *desc)
{
	return 2 * (vca_aligned_desc_size(desc) + sizeof(struct vca_device_ctrl));
}
#endif

/* Device page size */
#define VCA_DP_SIZE 4096

#define VCA_MAGIC 0xC0011DEA

#define VCA_PROTOCOL_VERSION 0x6


/* MAC address for virtual network adapters on host side */
#define H_MAC_0	0xFE
#define H_MAC_1 0x00
#define H_MAC_2 0x00
#define H_MAC_3 0x00


#endif
