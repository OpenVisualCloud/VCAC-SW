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
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/version.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <asm/uaccess.h>
#include <linux/module.h>

#include "../vca_virtio/uapi/vca_virtio_net.h"
#include "../vca_device/vca_device.h"
#include "../common/vca_dev_common.h"
#include "../common/vca_common.h"
#include "vca_ioctl.h"
#include "vop_main.h"
#include "vop_common.h"
#include "vop_kvec_buff.h"

static int vop_vringh_reset(struct vop_card_virtio_dev *vdev, bool start);
static void vop_virtio_del_card_device(struct vop_card_virtio_dev *vdev);

static bool host_device_ready(struct vop_card_virtio_dev *card_virtio_dev)
{
	return (READ_ONCE(card_virtio_dev->host.dd->status) & VIRTIO_CONFIG_S_DRIVER_OK) != 0;
}

/* Helper API to obtain the VOP PCIe device */
static inline struct device *vop_dev(struct vop_card_virtio_dev *vdev)
{
	return vdev->vpdev->dev.parent;
}

/* Helper API to check if a virtio device is initialized */
static inline int vop_card_virtio_dev_inited(struct vop_card_virtio_dev *vdev)
{
	if (!vdev)
		return -EINVAL;
	/* Device has not been created yet */
	if (!vdev->dd || !vdev->dd->type) {
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}
	/* Device has been removed/deleted */
	if (vdev->dd->type == -1) {
		dev_dbg(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, -ENODEV);
		return -ENODEV;
	}
	return 0;
}

/* send irq to remote peer informing that new available/used descriptors
   are expected become available soon  soon */
static void vop_send_head_up_irq(
	struct vop_dev_common *cdev,
	enum vop_heads_up_notification op)
{
	struct vop_card_virtio_dev *vdev = container_of(cdev,
		struct vop_card_virtio_dev, cdev);
	int db = -1;

	switch (op) {
	case vop_notify_used:
		db = vdev->dc->h2c_vdev_used_db;
		break;
	case vop_notify_available:
		db = vdev->dc->h2c_vdev_avail_db;
		break;
	default:
		BUG_ON(true);
		return;
	}

	vdev->vpdev->hw_ops->send_intr(vdev->vpdev, db);
}

static void vop_consumed_from_host_notification(struct vringh *vrh)
{
	struct vop_vringh *vvrh = container_of(vrh, struct vop_vringh, vrh);

	BUG_ON(!vvrh->vq);

	// we've finished using descriptors from local device, notify
	// virtqueue
	pr_debug("%s kicking  vring no %d used_idx:%d\n", __func__, vvrh->index,
		vvrh->vring.vr.used->idx);
	vca_vring_interrupt(0, vvrh->vq);
}

static void vop_virtio_init_post(struct vop_card_virtio_dev *vdev)
{
	int ret = vop_vringh_reset(vdev, true);

	vdev->dc->used_address_updated = 0;

	if (ret) {
		mutex_lock(&vdev->vdev_mutex);
		vop_virtio_del_card_device(vdev);
		mutex_unlock(&vdev->vdev_mutex);
		return;
	}

	dev_dbg(vop_dev(vdev), "%s: device type %d LINKUP\n",
		 __func__, vdev->virtio_id);
}


static inline void vop_virtio_peer_device_reset(struct vop_card_virtio_dev *vdev)
{
	vop_vringh_reset(vdev, false);
	vdev->dc->vdev_reset = 0;
	vdev->dc->host_ack = 1;
}

static void vop_virtio_reset_devices(struct vop_info *vi)
{
	struct list_head *pos, *tmp;
	struct vop_card_virtio_dev *vdev;

	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev = list_entry(pos, struct vop_card_virtio_dev, list);
		vdev->poll_wake = 1;
		wake_up_interruptible(&vdev->waitq);
	}
}

/* Determine the total number of bytes consumed in a VRINGH KVEC */
static inline u32 vop_vringh_iov_consumed(struct vringh_kiov *iov)
{
	int i;
	u32 total = iov->consumed;

	for (i = 0; i < iov->i; i++)
		total += iov->iov[i].iov_len;
	return total;
}

static void vop_bh_handler(struct work_struct *work)
{
	struct vop_card_virtio_dev *vdev = container_of(work, struct vop_card_virtio_dev,
			virtio_bh_work);

	pr_debug("%s %d\n", __func__, __LINE__);
	pr_debug("config_change %d vdev_reset %d guest_ack %d host_ack %d"
		"used_address_updated %d c2h_vdev_conf_db %d c2h_vdev_avail_db "
		"%d c2h_vdev_used_db %d h2c_vdev_conf_db %d h2c_vdev_avail_db "
		"%d h2c_vdev_used_db %d\n",
		vdev->dc->config_change,
		vdev->dc->vdev_reset,
		vdev->dc->guest_ack,
		vdev->dc->host_ack,
		vdev->dc->used_address_updated,
		vdev->dc->c2h_vdev_conf_db,
		vdev->dc->c2h_vdev_avail_db,
		vdev->dc->c2h_vdev_used_db,
		vdev->dc->h2c_vdev_conf_db,
		vdev->dc->h2c_vdev_avail_db,
		vdev->dc->h2c_vdev_used_db
		);

	if (vdev->dc->used_address_updated)
		vop_virtio_init_post(vdev);
	else if (vdev->dc->vdev_reset)
		vop_virtio_peer_device_reset(vdev);
	else
		vop_kvec_check_cancel(&vdev->cdev);

	pr_debug("%s card device %x notified\n",
		__func__,
		vdev->virtio_id);

	vdev->poll_wake = 1;

	wake_up_interruptible(&vdev->waitq);
}

static irqreturn_t _vop_virtio_intr_conf_handler(int irq, void *data)
{
	struct vop_card_virtio_dev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	vpdev->hw_ops->ack_interrupt(vpdev, vdev->virtio_conf_db);

	schedule_work(&vdev->virtio_bh_work);

	return IRQ_HANDLED;
}


static irqreturn_t _vop_virtio_intr_avail_handler(int irq, void *data)
{
	struct vop_card_virtio_dev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	vpdev->hw_ops->ack_interrupt(vpdev, vdev->virtio_avail_db);

	if (host_device_ready(vdev)) {
		common_dev_heads_up_avail_irq(&vdev->cdev);
	} else {
		pr_debug("%s not ready \n", __func__);
	}

	return IRQ_HANDLED;
}

static irqreturn_t _vop_virtio_intr_used_handler(int irq, void *data)
{
	struct vop_card_virtio_dev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	common_dev_heads_up_used_irq(&vdev->cdev);
	vpdev->hw_ops->ack_interrupt(vpdev, vdev->virtio_used_db);
	return IRQ_HANDLED;
}

static int vop_copy_dp_entry(struct vop_card_virtio_dev *vdev,
			     struct vca_device_desc *argp, __u8 *type,
			     struct vca_device_desc **devpage)
{
	struct vop_device *vpdev = vdev->vpdev;
	struct vca_device_desc *devp;
	struct vca_vqconfig *vqconfig;
	int ret = 0, i;
	bool slot_found = false;

	vqconfig = vca_vq_config(argp);
	for (i = 0; i < argp->num_vq; i++) {
		if (le16_to_cpu(vqconfig[i].num) > VCA_VRING_ENTRIES) {
			ret =  -EINVAL;
			dev_err(vop_dev(vdev), "%s %d err %d\n",
				__func__, __LINE__, ret);
			goto exit;
		}
	}

	/* Find the first free device page entry */
	for (i = sizeof(struct vca_bootparam);
		i < VCA_DP_SIZE - vca_total_desc_size(argp);
		i += vca_total_desc_size(devp)) {
		devp = vpdev->hw_ops->get_dp(vpdev) + i;
		if (devp->type == 0 || devp->type == -1) {
			slot_found = true;
			break;
		}
	}
	if (!slot_found) {
		ret =  -EINVAL;
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, ret);
		goto exit;
	}

	/*
	 * Save off the type before doing the memcpy. Type will be set in the
	 * end after completing all initialization for the new device.
	 */
	*type = argp->type;
	argp->type = 0;
	memcpy(devp, argp, vca_desc_size(argp));

	/* copy description for host device */
	memcpy(vca_host_device_desc(devp), argp, vca_desc_size(argp));
	vca_host_device_desc(devp)->type = *type;


	*devpage = devp;
exit:
	return ret;
}

static void vop_init_device_ctrl(struct vca_device_ctrl *dc)
{
	dc->config_change = 0;
	dc->guest_ack = GUEST_ACK_NONE;
	dc->vdev_reset = 0;
	dc->host_ack = 0;
	dc->used_address_updated = 0;
	dc->c2h_vdev_conf_db = -1;
	dc->c2h_vdev_avail_db = -1;
	dc->c2h_vdev_used_db = -1;
	dc->h2c_vdev_conf_db = -1;
	dc->h2c_vdev_avail_db = -1;
	dc->h2c_vdev_used_db = -1;
}


#define to_host_virtio_device(vd) container_of(vd, struct vop_host_virtio_dev, virtio_device)


/* This gets the device's feature bits. */
static u64 vop_get_features(struct virtio_device *virtio_dev)
{
	struct vop_host_virtio_dev* host_virtio_dev = to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct vca_device_desc *desc = host_virtio_dev->dd;
	unsigned int i, bits;
	u64 features = 0;
	u8  *in_features = vca_vq_features(desc);
	int feature_len = desc->feature_len;

	dev_dbg(&vdev->dev, "%s features: %llx\n", __func__,*(u64*)in_features);

	bits = min_t(unsigned, feature_len,
		     sizeof(virtio_dev->features)) * 8;
	for (i = 0; i < bits; i++)
		if (in_features[i / 8] & (BIT(i % 8)))
			features |= BIT(i);

	return features;
}

static int vop_finalize_features(struct virtio_device *virtio_dev)
{
	struct vop_host_virtio_dev* host_virtio_dev =
			to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct vca_device_desc *desc = host_virtio_dev->dd;

	unsigned int i, bits;
	u8 feature_len = desc->feature_len;
	/* Second half of bitmap is features we accept. */
	u8 *out_features = vca_vq_features(desc) + feature_len;

	dev_dbg(&vdev->dev, "%s features: %llx\n", __func__, virtio_dev->features);

	/* Give virtio_ring a chance to accept features. */
	vca_vring_transport_features(virtio_dev);

#ifdef VIRTIO_NET_F_OFFSET_RXBUF
	/* Give virtio_net a chance to accept features. */
	vca_virtio_net_features(virtio_dev);

	dev_dbg(&vdev->dev, "%s feature VIRTIO_NET_F_OFFSET_RXBUF %s\n", __func__,
		virtio_has_feature(virtio_dev, VIRTIO_NET_F_OFFSET_RXBUF) ? "Enabled" :
		"Disabled");
#else
#pragma message "Feature VIRTIO_NET_F_OFFSET_RXBUF not implemented in kernel"
	/* Unknown features should be cleaned by kernel */
	BUG_ON(virtio_has_feature(virtio_dev, __VIRTIO_NET_F_OFFSET_RXBUF));
#endif


	memset_io(out_features, 0, feature_len);
	bits = min_t(unsigned, feature_len,
		     sizeof(virtio_dev->features)) * 8;
	for (i = 0; i < bits; i++) {
		if (__virtio_test_bit(virtio_dev, i))
			out_features[i / 8] = out_features[i / 8] | (BIT(i % 8));
	}

	return 0;
}

/*
 * Reading and writing elements in config space
 */
static void vop_get(struct virtio_device *virtio_dev, unsigned int offset,
		    void *buf, unsigned len)
{
	struct vop_host_virtio_dev* host_virtio_dev =
		to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct vca_device_desc *desc = host_virtio_dev->dd;

	dev_dbg(&vdev->dev, "%s offset %x\n", __func__, offset);

	if (offset + len > desc->config_len)
		return;
	memcpy(buf, vca_vq_configspace(desc) + offset, len);
}

static void vop_set(struct virtio_device *virtio_dev, unsigned int offset,
		    const void *buf, unsigned len)
{
	struct vop_host_virtio_dev* host_virtio_dev =
		to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct vca_device_desc *desc = host_virtio_dev->dd;

	dev_dbg(&vdev->dev, "%s offset %x\n", __func__, offset);

	if (offset + len > desc->config_len)
		return;
	memcpy(vca_vq_configspace(desc) + offset, buf, len);
}

/*
 * The operations to get and set the status word just access the status
 * field of the device descriptor. set_status also interrupts the host
 * to tell about status changes.
 */
static u8 vop_get_status(struct virtio_device *virtio_dev)
{
	return to_host_virtio_device(virtio_dev)->dd->status;
}

static void vop_set_status(struct virtio_device *virtio_dev, u8 status)
{
	struct vop_host_virtio_dev* host_virtio_dev =
		to_host_virtio_device(virtio_dev);
	struct vop_card_virtio_dev *card_dev =
		container_of(host_virtio_dev, struct vop_card_virtio_dev, host);
	struct vop_device *vdev =  host_virtio_dev->vdev;

	host_virtio_dev->dd->status = status;


	if (status & VIRTIO_CONFIG_S_DRIVER_OK) {
		int ret = common_dev_start(&card_dev->cdev);
		if (ret) {
			dev_err(&vdev->dev, "%s failed to start cdev %d\n",
				__func__, ret);
		}
	} else {
		common_dev_stop(&card_dev->cdev);
	}
}

static void vop_reset(struct virtio_device *virtio_dev)
{
	struct vop_host_virtio_dev* host_virtio_dev =
		to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct vca_device_desc *desc = host_virtio_dev->dd;
	int i;

	dev_dbg(&vdev->dev, "%s\n", __func__);

	for (i = 0; i < host_virtio_dev->dd->num_vq; i++) {
		/*
		 * Avoid lockdep false positive. The + 1 is for the vop
		 * spinlock which is held in the reset devices code path.
		 */
		spin_lock_nest_lock(&host_virtio_dev->vop_vringh[i].vr_spinlock,  i + 1);
	}

	desc->status = 0;
	for (i = 0; i < host_virtio_dev->dd->num_vq; i++) {
		struct vringh *vrh = &host_virtio_dev->vop_vringh[i].vrh;

		vrh->completed = 0;
		vrh->last_avail_idx = 0;
		vrh->last_used_idx = 0;
	}

	for (i = 0; i < host_virtio_dev->dd->num_vq; i++) {
		/*
		 * Avoid lockdep false positive. The + 1 is for the vop
		 * spinlock which is held in the reset devices code path.
		 */
		spin_unlock(&host_virtio_dev->vop_vringh[i].vr_spinlock);
	}
}

static void vop_del_vqs(struct virtio_device *virtio_dev)
{
	struct vop_host_virtio_dev* host_virtio_dev =
		to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct virtqueue *vq, *n;
	int idx = 0;

	dev_dbg(&vdev->dev, "%s\n", __func__);

	list_for_each_entry_safe(vq, n, &virtio_dev->vqs, list) {
		vca_vring_del_virtqueue(vq);
		++idx;
	}
}

/*
 * The virtio_ring code calls this API when it wants to notify the Host.
 * Some descriptors wait to read and send.
 */
static bool vop_notify_self(struct virtqueue *vq)
{
	struct vop_vringh *vvrh = vq->priv;
	struct vop_host_virtio_dev *host_virtio_dev = &vvrh->vdev->host;
	struct vop_card_virtio_dev *card_dev =
		container_of(host_virtio_dev, struct vop_card_virtio_dev, host);

	BUG_ON(vvrh->index != VRING_INDEX_SEND);

	pr_debug("%s read AVAIL desc  for host  device vring no %d "
		"avail idx is %d\n",  __func__,
		vvrh->index,
		vvrh->vring.vr.avail->idx);

	descriptor_read_notification(&card_dev->cdev);

	return true;
}

/*
 * The virtio_ring code calls this API when it wants to notify the Host.
 * Some descriptors wait to write.
 */
static bool vop_notify_peer(struct virtqueue *vq)
{
	struct vop_vringh *vvrh = vq->priv;
	struct vop_host_virtio_dev *host_virtio_dev = &vvrh->vdev->host;
	struct vop_card_virtio_dev *card_dev =
		container_of(host_virtio_dev, struct vop_card_virtio_dev, host);

	BUG_ON(vvrh->index != VRING_INDEX_RECV);

	if (host_device_ready(card_dev)) {
		if (card_dev->cdev.kvec_buff.remote_write_kvecs.mapped) {
			vop_kvec_buf_update(&card_dev->cdev);
		}
	}

	return true;
}

/*
 * This routine will assign vring's allocated in host/io memory. Code in
 * virtio_ring.c however continues to access this io memory as if it were local
 * memory without io accessors.
 */
static struct virtqueue *vop_find_vq(struct virtio_device *virtio_dev,
				     unsigned index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name)
{
	struct vop_host_virtio_dev* host_virtio_dev =
		to_host_virtio_device(virtio_dev);
	struct vop_card_virtio_dev *card_dev =
		container_of(host_virtio_dev, struct vop_card_virtio_dev, host);
	struct vca_device_desc *desc = host_virtio_dev->dd;
	struct vop_device *vdev = host_virtio_dev->vdev;
	struct virtqueue *vq;
	int err;
	u32 num;
	struct vca_vqconfig __iomem *vqconfig;
	struct vca_vqconfig config;
	void *va;
	bool (*notify)(struct virtqueue *);

	BUG_ON(index >= VCA_MAX_VRINGS);

	if (index >= desc->num_vq || index >= VCA_MAX_VRINGS)
		return ERR_PTR(-ENOENT);

	if (!name)
		return ERR_PTR(-ENOENT);

	vqconfig = vca_vq_config(desc) + index;
	memcpy_fromio(&config, vqconfig, sizeof(config));

	num = le16_to_cpu(config.num);

	va = host_virtio_dev->vop_vringh[index].vring.va;
	if (!va) {
		dev_err(&vdev->dev, "%s %d vring mem not allocated\n",
			__func__, __LINE__);
		return  ERR_PTR(-ENOMEM);
	}

#ifdef VIRTIO_RING_F_DMA_MAP
	if (index == VRING_INDEX_SEND) {
		__virtio_clear_bit(virtio_dev, VIRTIO_RING_F_DMA_MAP);
	} else if (index == VRING_INDEX_RECV) {
		__virtio_set_bit(virtio_dev, VIRTIO_RING_F_DMA_MAP);
	}
	dev_dbg(&vdev->dev, "creating VQ no %d DMA map: %s\n", index,
		virtio_has_feature(virtio_dev, VIRTIO_RING_F_DMA_MAP) ?
		"yes" : "no");
#endif

	if (index == VRING_INDEX_SEND) {
		notify = vop_notify_self;
	} else if (index == VRING_INDEX_RECV) {
		notify = vop_notify_peer;
	} else {
		notify = NULL;
	}

	vq = vca_vring_new_virtqueue(
			index,
			num,
			VCA_VIRTIO_RING_ALIGN,
			virtio_dev,
			false,
			(void __force *)va, notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto err;
	}

	vq->priv = &host_virtio_dev->vop_vringh[index];

	/* setup host vring */
	host_virtio_dev->vop_vringh[index].vq = vq;
	host_virtio_dev->vop_vringh[index].index = index;
	host_virtio_dev->vop_vringh[index].vdev = card_dev;

	vring_init(
		&host_virtio_dev->vop_vringh[index].vring.vr,
		num,
		va,
		VCA_VIRTIO_RING_ALIGN);

	err = vca_vringh_init_kern(&host_virtio_dev->vop_vringh[index].vrh,
		*(u32*)vca_vq_features(desc),
		num,
		false,
		host_virtio_dev->vop_vringh[index].vring.vr.desc,
		host_virtio_dev->vop_vringh[index].vring.vr.avail,
		host_virtio_dev->vop_vringh[index].vring.vr.used);
	if (err) {
		dev_err(&vdev->dev, "error initializing vringh\n");
		goto err;
	}

	host_virtio_dev->vop_vringh[index].vrh.notify =
		vop_consumed_from_host_notification;

	return vq;
err:
	return ERR_PTR(err);
}

static int vop_find_vqs(struct virtio_device *virtio_dev, unsigned nvqs,
			struct virtqueue *vqs[],
			vq_callback_t *callbacks[],
			const char *names[])
{
	struct vop_host_virtio_dev* host_virtio_dev =
				to_host_virtio_device(virtio_dev);
	struct vop_device *vdev =  host_virtio_dev->vdev;
	struct vca_device_desc *desc = host_virtio_dev->dd;

	int i, err;

	dev_dbg(&vdev->dev, "%s num vqs: %x, device description has %x\n",
		__func__, nvqs, desc->num_vq);

	/* We must have this many virtqueues. */
	if (nvqs > desc->num_vq || nvqs >= VCA_MAX_VRINGS) {
		dev_err(&vdev->dev, "%s: error invalid vqs number %i\n",
						__func__, nvqs);
		return -ENOENT;
	}

	for (i = 0; i < nvqs; ++i) {
		dev_info(&vdev->dev, "%s: %d: %s\n",
			__func__, i, names[i]);
		vqs[i] = vop_find_vq(virtio_dev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	return 0;
error:
	vop_del_vqs(virtio_dev);
	return err;
}
/*
 * The config ops structure as defined by virtio config
 */
static struct virtio_config_ops vop_vq_config_ops = {
	.get_features = vop_get_features,
	.finalize_features = vop_finalize_features,
	.get = vop_get,
	.set = vop_set,
	.get_status = vop_get_status,
	.set_status = vop_set_status,
	.reset = vop_reset,
	.find_vqs = vop_find_vqs,
	.del_vqs = vop_del_vqs,
};

static void vop_virtio_release_dev(struct device *_d)
{
	/*
	 * No need for a release method similar to virtio PCI.
	 * Provide an empty one to avoid getting a warning from core.
	 */
}

static int vop_unregister_host_device(struct vop_card_virtio_dev *vdev)
{
	if (vdev->host.ready) {
		vca_unregister_virtio_device(&vdev->host.virtio_device);
		vdev->host.ready = false;
	}
	return 0;
}

static int vop_register_host_device(struct vop_card_virtio_dev *vdev)
{
	struct vca_device_desc *dd = vdev->host.dd;
	int ret = 0;
	int i;

	if (vdev->host.ready)
		return 0;

	for (i = 0; i < vdev->host.dd->num_vq; i++)
		memset(vdev->host.vop_vringh[i].vring.va, 0,
		       vdev->host.vop_vringh[i].vring.len);

	memset(&vdev->host.virtio_device, 0, sizeof(vdev->host.virtio_device));
	vdev->host.virtio_id = vdev->virtio_id;
	vdev->host.vdev = vdev->vpdev;

	if (vdev->host.virtio_id == VIRTIO_ID_NET &&
	    dd->feature_len >= sizeof(u32)) {
		u8 card_id, cpu_id;
		struct virtio_net_config* net_config =
			(struct virtio_net_config*)vca_vq_configspace(dd);

		vdev->vpdev->hw_ops->get_card_and_cpu_id(vdev->vpdev, &card_id,
							 &cpu_id);

		dev_dbg(&vdev->vpdev->dev,
			"setting mac %x:%x \n", card_id + 1, cpu_id + 1);

		*(u32*)vca_vq_features(dd) |= (1 << VIRTIO_NET_F_MAC);
		net_config->mac[0] = H_MAC_0;
		net_config->mac[1] = H_MAC_1;
		net_config->mac[2] = H_MAC_2;
		net_config->mac[5] = H_MAC_3;
		net_config->mac[4] = card_id + 1;
		net_config->mac[5] = cpu_id + 1;
	}

	vdev->host.virtio_device.dev.parent = &vdev->vpdev->dev;
	vdev->host.virtio_device.dev.release = vop_virtio_release_dev;
	vdev->host.virtio_device.id.device = vdev->dd->type;
	vdev->host.virtio_device.config = &vop_vq_config_ops;
	vdev->host.virtio_device.priv = (void*)(u64)vdev->vpdev->dnode;

	dev_dbg(&vdev->vpdev->dev, "%s num vqs %u\n", __func__, dd->num_vq);
	ret = vca_register_virtio_device(&vdev->host.virtio_device);
	if (ret) {
		dev_err(&vdev->vpdev->dev,
			"Failed to register vop device type \n");
	} else {
		vdev->host.ready = true;
	}

	return ret;
}

static void vop_deallocate_host_vrings(struct vop_card_virtio_dev *vdev)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vi->vpdev;
	struct vca_device_desc* host_dd = vdev->host.dd;
	int i;

	if (!host_dd)
		return;

	for (i = 0; i < host_dd->num_vq; i++) {
		if (vdev->host.vop_vringh[i].vring.va) {
			dma_unmap_single(&vpdev->dev, vdev->host.pa[i],
					 vdev->host.vop_vringh[i].vring.len,
					  DMA_BIDIRECTIONAL);
			free_pages(
			    (unsigned long)vdev->host.vop_vringh[i].vring.va,
			    get_order(vdev->host.vop_vringh[i].vring.len));
		}
	}
}

static int vop_allocate_host_vrings(struct vop_card_virtio_dev *vdev)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vi->vpdev;
	struct vca_device_desc* host_dd = vca_host_device_desc(vdev->dd);
	struct vca_vqconfig *host_vqconfig;
	struct vca_device_ctrl* host_dc = (void *)host_dd +
		vca_aligned_desc_size(host_dd);
	struct vca_vqconfig config;
	size_t vr_size, _vr_size;
	void *va;
	dma_addr_t vr_addr;
	int num;
	int i;
	int err;

	vdev->host.dd = host_dd;
	vdev->host.dc = host_dc;

	vop_init_device_ctrl(host_dc);

	for (i = 0; i < host_dd->num_vq; i++) {
		spin_lock_init(&vdev->host.vop_vringh[i].vr_spinlock);

		host_vqconfig = vca_vq_config(host_dd) + i;
 		memcpy_fromio(&config, host_vqconfig, sizeof(config));

		num = le16_to_cpu(config.num);
		_vr_size = vring_size(le16_to_cpu(config.num), VCA_VIRTIO_RING_ALIGN);
		vr_size = PAGE_ALIGN(_vr_size + sizeof(struct _vca_vring_info));

		dev_dbg(&vpdev->dev,
			"allocating host vring idx%d num:%x size %x\n", i, num,
			(u32)vr_size);
		va =  (void*) __get_free_pages(
				GFP_KERNEL | __GFP_ZERO | GFP_DMA,
				get_order(vr_size));
		if (!va) {
			dev_err(&vpdev->dev, "error allocating vring\n");
			err = -ENOMEM;
			goto err;
		}

		memset(va, 0x0, _vr_size);

		vr_addr = dma_map_single(&vpdev->dev, va, vr_size,
					 DMA_BIDIRECTIONAL);
		if ((err = dma_mapping_error(&vpdev->dev, vr_addr))) {
			dev_err(&vpdev->dev, "error dma-mapping vring :%d\n", err);
			free_pages((unsigned long)va, get_order(vr_size));
			goto err;
		}
		CHECK_DMA_ZONE(&vpdev->dev, vr_addr);
		dev_dbg(&vpdev->dev, "host vring: va:%p pa:%llx\n", va, vr_addr);

		vdev->host.vop_vringh[i].vring.va = va;
		vdev->host.vop_vringh[i].vring.len = vr_size;
		vdev->host.pa[i] = vr_addr;

		host_vqconfig->address = cpu_to_le64(vr_addr);
	}

	return 0;
err:
	vop_deallocate_host_vrings(vdev);
	return err;

}

static int vop_common_dev_init(struct vop_card_virtio_dev *vdev)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vi->vpdev;
	struct vop_host_virtio_dev *host_virtio_dev = &vdev->host;
	struct vop_vringh *host_vrings = host_virtio_dev->vop_vringh;
	struct vop_vringh *vringh_from = host_vrings + VRING_INDEX_SEND;
	struct vop_vringh *vringh_rcv = host_vrings + VRING_INDEX_RECV;
	int num_decriptors =
		vca_vq_config(host_virtio_dev->dd)[VRING_INDEX_RECV].num;
	int rc;

	rc = common_dev_init(&vdev->cdev, vpdev,
		vringh_from,
		vringh_rcv,
		num_decriptors,
		vop_send_head_up_irq,
		vdev->host.dd,
		vdev->dd);

	if (rc)
		return rc;

	/* this is written to CARD device control */
	vdev->dc->kvec_buf_address =  vdev->cdev.kvec_buff.local_write_kvecs.pa;
	vdev->dc->kvec_buf_elems   =  num_decriptors;

	dev_dbg(&vpdev->dev, "host side KVEC buffer pa %llx elems %x\n",
		       vdev->dc->kvec_buf_address,
		       vdev->dc->kvec_buf_elems);

	return 0;
}

static int vop_setup_irqs(struct vop_card_virtio_dev *vdev)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vi->vpdev;
	u8 card_id, cpu_id;
	int err;
	char irqname[16];

	vpdev->hw_ops->get_card_and_cpu_id(vpdev, &card_id, &cpu_id);
	snprintf(irqname, sizeof(irqname), "vop%u%uvirtio%d", card_id, cpu_id,
			vdev->virtio_id);

	vdev->virtio_conf_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_conf_db_cookie = vpdev->hw_ops->request_irq(vpdev,
			_vop_virtio_intr_conf_handler, irqname, vdev,
			vdev->virtio_conf_db);
	if (IS_ERR(vdev->virtio_conf_db_cookie)) {
		err= PTR_ERR(vdev->virtio_conf_db_cookie);
		dev_dbg(&vpdev->dev, "request irq conf failed\n");
		goto exit;
	}
	vdev->dc->c2h_vdev_conf_db = vdev->virtio_conf_db;

	vdev->virtio_avail_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_avail_db_cookie = vpdev->hw_ops->request_irq(vpdev,
			_vop_virtio_intr_avail_handler, irqname, vdev,
			vdev->virtio_avail_db);
	if (IS_ERR(vdev->virtio_avail_db_cookie)) {
		err = PTR_ERR(vdev->virtio_avail_db_cookie);
		dev_dbg(&vpdev->dev, "request irq avail failed\n");
		goto free_conf_irq;
	}
	vdev->dc->c2h_vdev_avail_db = vdev->virtio_avail_db;

	vdev->virtio_used_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_used_db_cookie = vpdev->hw_ops->request_irq(vpdev,
			_vop_virtio_intr_used_handler, irqname, vdev,
			vdev->virtio_used_db);
	if (IS_ERR(vdev->virtio_used_db_cookie)) {
		err = PTR_ERR(vdev->virtio_used_db_cookie);
		dev_dbg(&vpdev->dev, "request irq used failed\n");
		goto free_avail_irq;
	}
	vdev->dc->c2h_vdev_used_db = vdev->virtio_used_db;

	return 0;

free_avail_irq:
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_avail_db_cookie, vdev);
free_conf_irq:
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_conf_db_cookie, vdev);
exit:
	return err;
}

static bool DontUseDmaAtNode= false;
module_param( DontUseDmaAtNode, bool, 0644);
MODULE_PARM_DESC( DontUseDmaAtNode, "Prevent use DMA from a node side");

static int vop_setup_virtio_device(struct vop_card_virtio_dev *vdev,
				 struct vca_device_desc *argp)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vi->vpdev;
	struct vca_device_desc *dd = NULL;
	int ret;
	u8 type = 0;
	s8 db = -1;
	struct vca_bootparam *bootparam;

	bootparam = vpdev->hw_ops->get_dp(vpdev);
	init_waitqueue_head(&vdev->waitq);
	INIT_LIST_HEAD(&vdev->list);
	vdev->vpdev = vpdev;

	dev_dbg(vop_dev(vdev), "%s %d\n", __func__, __LINE__);

	ret = vop_copy_dp_entry(vdev, argp, &type, &dd);
	if (ret) {
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, ret);
		return ret;
	}

	vdev->dc = (void *)dd + vca_aligned_desc_size(dd);
	vop_init_device_ctrl(vdev->dc);

	vdev->dd = dd;
	vdev->virtio_id = type;
	INIT_WORK(&vdev->virtio_bh_work, vop_bh_handler);

	if (!vca_vq_config(dd)->num ||
	    !vca_vq_config(vca_host_device_desc(dd))->num) {
		dev_err(vop_dev(vdev), "0 descriptors for device %d\n", type);
		return -EINVAL;
	}

	/* allocate host vring memory before creating card device */
	ret = vop_allocate_host_vrings(vdev);
	if (ret) {
		return ret;
	}

	/* initialize common device */
	ret = vop_common_dev_init(vdev);
	if (ret) {
		dev_err(vop_dev(vdev), "cdev setup error device %d\n", type);
		goto destroy_host_vrings;
	}

	/* initialize interrupts sent from card to host */
	ret = vop_setup_irqs(vdev);
	if (ret) {
		dev_err(vop_dev(vdev), "IRQ setup error device %d\n", type);
		goto destroy_common_dev;
	}

	/*
	 * Order the type update with previous stores. This write barrier
	 * is paired with the corresponding read barrier before the uncached
	 * system memory read of the type, on the card while scanning the
	 * device page.
	 */
	smp_wmb();
	dd->type = type;
	argp->type = type;

	if (bootparam) {
		if( DontUseDmaAtNode) {
			bootparam->flags.DontUseDma=1;
		}
		db = bootparam->h2c_config_db;
		if (db != -1)
			vpdev->hw_ops->send_intr(vpdev, db);
	}
	dev_dbg(&vpdev->dev, "Added virtio id %d db %d\n", dd->type, db);
	return 0;

destroy_common_dev:
	common_dev_deinit(&vdev->cdev, vpdev);
destroy_host_vrings:
	vop_deallocate_host_vrings(vdev);
	return ret;
}

static int vop_card_dev_remove(struct vop_info *vi, struct vca_device_ctrl *devp,
			   struct vop_device *vpdev)
{
	struct vca_bootparam *bootparam = vpdev->hw_ops->get_dp(vpdev);
	s8 db;
	int retry;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wake);
	bool received = false;
	int err = 0;

	devp->config_change = VCA_VIRTIO_PARAM_DEV_REMOVE;
	db = bootparam->h2c_config_db;
	if (db != -1) {
		vpdev->hw_ops->send_intr(vpdev, db);
	} else {
		err = -ENODEV;
		goto done;
	}

	/* in that case we have to use wait_event_timeout instead of
	wait_event_interruptible_timeout, because this function is used
	to clean up and no matter which signal will be catched function
	needs time to release vop handlers */
	for (retry = 0; retry < 10; retry++) {
		wait_event_timeout(wake, devp->guest_ack == GUEST_ACK_DONE,
				msecs_to_jiffies(1000));
		if (devp->guest_ack == GUEST_ACK_DONE) {
			break;
		}
		/* Check if card received dev_remove request. */
		if (devp->guest_ack == GUEST_ACK_RECEIVED) {
			received = true;
		} else if (!received && retry > 2) {
			err = -ENODEV;
			break;
		}
	}
done:
	devp->config_change = 0;
	devp->guest_ack = GUEST_ACK_NONE;
	return err;
}

static void vop_virtio_del_card_device(struct vop_card_virtio_dev *vdev)
{
	struct vop_info *vi = vdev->vi;
	struct vop_device *vpdev = vdev->vpdev;
	struct vca_bootparam *bootparam = vpdev->hw_ops->get_dp(vpdev);

	common_dev_stop(&vdev->cdev);

	if (!bootparam)
		goto skip_hot_remove;

	if (vop_card_dev_remove(vi, vdev->dc, vpdev)) {
		dev_err(&vdev->vpdev->dev, "%s Node not responding!\n", __func__);
	}

skip_hot_remove:
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_conf_db_cookie, vdev);
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_avail_db_cookie, vdev);
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_used_db_cookie, vdev);

	flush_work(&vdev->virtio_bh_work);

	vop_kvec_unmap_buf(&vdev->cdev.kvec_buff, vpdev);
	common_dev_deinit(&vdev->cdev, vpdev);

	/*
	 * Order the type update with previous stores. This write barrier
	 * is paired with the corresponding read barrier before the uncached
	 * system memory read of the type, on the card while scanning the
	 * device page.
	 */
	smp_wmb();
	vdev->dd->type = -1;

	vop_unregister_host_device(vdev);
}

#define VCA_VRINGH_READ true

static inline int vop_verify_copy_args(struct vop_card_virtio_dev *vdev,
				       struct vca_copy_desc *copy)
{
	if (!vdev) {
		pr_err("%s %d NULL pointer err %d\n",
				__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}

	if (copy->vr_idx >= vdev->dd->num_vq) {
		dev_err(vop_dev(vdev), "%s %d err %d\n",
			__func__, __LINE__, -EINVAL);
		return -EINVAL;
	}
	return 0;
}

static int vop_open(struct inode *inode, struct file *f)
{
	struct vop_card_virtio_dev *vdev;
	struct vop_info *vi = container_of(f->private_data,
		struct vop_info, miscdev);

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev)
		return -ENOMEM;
	vdev->vi = vi;
	mutex_init(&vdev->vdev_mutex);
	f->private_data = vdev;
	init_completion(&vdev->destroy);
	complete(&vdev->destroy);
	return 0;
}

/* Stop network traffic from host side to protect dma_hang, example after crash node. */
void vop_stop_network_traffic(struct vop_device *vpdev)
{
	struct vop_info *vi = vpdev->priv;
	struct vop_card_virtio_dev *vdev;
	struct list_head *pos, *tmp;

	mutex_lock(&vi->vop_mutex);
	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev = list_entry(pos, struct vop_card_virtio_dev, list);
		common_dev_stop(&vdev->cdev);
	}
	mutex_unlock(&vi->vop_mutex);
}
EXPORT_SYMBOL_GPL(vop_stop_network_traffic);

static int vop_release(struct inode *inode, struct file *f)
{
	struct vop_card_virtio_dev *vdev = f->private_data, *vdev_tmp;
	struct vop_info *vi = vdev->vi;
	struct list_head *pos, *tmp;
	bool found = false;

	mutex_lock(&vdev->vdev_mutex);
	pr_debug("%s %d: Mutex locked\n", __func__, __LINE__);
	if (vdev->deleted)
		goto unlock;
	mutex_lock(&vi->vop_mutex);
	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev_tmp = list_entry(pos, struct vop_card_virtio_dev, list);
		if (vdev == vdev_tmp) {
			vop_virtio_del_card_device(vdev);
			list_del(pos);
			found = true;
			break;
		}
	}
	mutex_unlock(&vi->vop_mutex);
unlock:
	mutex_unlock(&vdev->vdev_mutex);
	if (!found)
		wait_for_completion_interruptible(&vdev->destroy);
	f->private_data = NULL;
	kfree(vdev);
	return 0;
}

static long vop_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct vop_card_virtio_dev *vdev = f->private_data;
	struct vop_info *vi = vdev->vi;
	void __user *argp = (void __user *)arg;
	int ret;

	switch (cmd) {
	case VCA_VIRTIO_ADD_DEVICE:
	{
		struct vca_device_desc dd, *dd_config;

		if (vdev->virtio_id != 0) {
			dev_warn(&vdev->vpdev->dev, "device already exist\n");
			return -EEXIST;
		}

		if (copy_from_user(&dd, argp, sizeof(dd)))
			return -EFAULT;

		/* check device description section sizes and number of elements
		   to not depend on maximum value of u8 type */
		if (dd.num_vq > VCA_MAX_VRINGS ||
		    dd.config_len > VCA_MAX_CONFIG_LEN ||
		    dd.feature_len > VCA_MAX_FEATURE_LEN) {
			dev_err(&vdev->vpdev->dev, "invalid device parameters: num_vq:%uc "
				" config_len:%uc feature_len:%uc\n",
				dd.num_vq, dd.config_len, dd.feature_len);
			return -EINVAL;
		}

		if (vca_aligned_desc_size(&dd) > VCA_MAX_DESC_BLK_SIZE) {
			dev_err(&vdev->vpdev->dev, "device description size exceedes"
				" maximum value\n");
			return -EINVAL;
		}

		dd_config = kzalloc(vca_desc_size(&dd), GFP_KERNEL);
		if (!dd_config)
			return -ENOMEM;
		if (copy_from_user(dd_config, argp, vca_desc_size(&dd))) {
			ret = -EFAULT;
			goto free_ret;
		}
		mutex_lock(&vdev->vdev_mutex);
		mutex_lock(&vi->vop_mutex);

		ret = vop_setup_virtio_device(vdev, dd_config);
		if (ret)
			goto unlock_ret;

		list_add_tail(&vdev->list, &vi->vdev_list);
unlock_ret:
		mutex_unlock(&vi->vop_mutex);
		mutex_unlock(&vdev->vdev_mutex);
free_ret:
		kzfree(dd_config);
		return ret;
	}
	case VCA_VIRTIO_COPY_DESC:
	{
		return -EFAULT;
	}
	default:
		return -ENOIOCTLCMD;
	};
	return 0;
}

static const struct file_operations vop_fops = {
	.open = vop_open,
	.release = vop_release,
	.unlocked_ioctl = vop_ioctl,
	.owner = THIS_MODULE,
};

int vop_host_init(struct vop_info *vi)
{
	int rc;
	u8 card_id, cpu_id;
	struct miscdevice *mdev;
	struct vop_device *vpdev = vi->vpdev;

	INIT_LIST_HEAD(&vi->vdev_list);
	vi->dma_ch = vpdev->dma_ch;
	mdev = &vi->miscdev;
	mdev->minor = MISC_DYNAMIC_MINOR;
	vpdev->hw_ops->get_card_and_cpu_id(vpdev, &card_id, &cpu_id);
	snprintf(vi->name, sizeof(vi->name), "vop_virtio%u%u", card_id, cpu_id);
	mdev->name = vi->name;
	mdev->fops = &vop_fops;
	mdev->parent = &vpdev->dev;

	rc = misc_register(mdev);
	// Workeround to run 8 cards (missing misc devices)
	if (rc) {
		unsigned char minor = 64; // see miscdevice.h
		do {
			mdev->minor = minor;
			rc = misc_register(mdev);
			if( !rc)
				return rc;
		} while(MISC_DYNAMIC_MINOR != ++minor);
		dev_err(&vpdev->dev, "%s failed misc_register %d\n", __func__, rc);
	}
	return rc;
}


void vop_host_uninit(struct vop_info *vi)
{
	struct list_head *pos, *tmp;
	struct vop_card_virtio_dev *vdev;

	mutex_lock(&vi->vop_mutex);
	vop_virtio_reset_devices(vi);

	/*for (pos = (&vi->vdev_list)->next, tmp = pos->next; pos != (&vi->vdev_list); \
		pos = tmp, tmp = pos->next)*/
	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev = list_entry(pos, struct vop_card_virtio_dev, list);
		list_del(pos);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		reinit_completion(&vdev->destroy);
#else
		INIT_COMPLETION(vdev->destroy);
#endif
		mutex_unlock(&vi->vop_mutex);
		mutex_lock(&vdev->vdev_mutex);
		vop_deallocate_host_vrings(vdev);
		vop_virtio_del_card_device(vdev);
		vdev->deleted = true;
		mutex_unlock(&vdev->vdev_mutex);
		complete(&vdev->destroy);
		mutex_lock(&vi->vop_mutex);
	}
	mutex_unlock(&vi->vop_mutex);
	misc_deregister(&vi->miscdev);
}

static int vop_vringh_reset(struct vop_card_virtio_dev *vdev, bool start)
{
	int ret;

	dev_dbg(&vdev->vpdev->dev, "%s start:%s\n", __func__, start ? "yes" : "no");

	common_dev_stop(&vdev->cdev);
	vop_kvec_unmap_buf(&vdev->cdev.kvec_buff, vdev->vpdev);
	vop_unregister_host_device(vdev);

	if (!start)
		return 0;

	ret = vop_register_host_device(vdev);
	if (ret) {
		dev_err(&vdev->vpdev->dev, "%s failed to register virtio dev %d\n",
			__func__, ret);
		return ret;
	}

	ret = vop_kvec_map_buf(&vdev->cdev.kvec_buff, vdev->vpdev,
				vdev->host.dc->kvec_buf_elems,
				vdev->host.dc->kvec_buf_address);
	if (ret) {
		dev_err(&vdev->vpdev->dev, "%s failed to map kvec buf %d\n",
			__func__, ret);
		goto unregister_device;
	}

	descriptor_read_notification(&vdev->cdev);

	dev_dbg(&vdev->vpdev->dev, "%s finished\n", __func__);
	return 0;

unregister_device:
	vop_unregister_host_device(vdev);
	return ret;
}
