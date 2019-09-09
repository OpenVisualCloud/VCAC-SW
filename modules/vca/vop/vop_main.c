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
 * Adapted from:
 *
 * virtio for kvm on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 *
 * Intel Virtio Over PCIe (VOP) driver.
 *
 */
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include "../vca_virtio/uapi/vca_virtio_net.h"
#include "../vca_device/vca_device.h"
#include "vop_main.h"
#include "vop_common.h"
#include "vop_kvec_buff.h"
#include "../common/vca_common.h"

#define to_vopvdev(vd) container_of(vd, struct _vop_vdev, vdev)

#define _vop_aligned_desc_size(d) __vca_align(_vop_desc_size(d), 8)


/* Helper API to obtain the parent of the virtio device */
static inline struct device *_vop_dev(struct _vop_vdev *vdev)
{
	return vdev->vdev.dev.parent;
}

static inline unsigned _vop_desc_size(struct vca_device_desc __iomem *desc)
{
	return sizeof(*desc)
		+ ioread8(&desc->num_vq) * sizeof(struct vca_vqconfig)
		+ ioread8(&desc->feature_len) * 2
		+ ioread8(&desc->config_len);
}

static inline struct vca_vqconfig __iomem *
_vop_vq_config(struct vca_device_desc __iomem *desc)
{
	return (struct vca_vqconfig __iomem *)(desc + 1);
}

static inline u8 __iomem *
_vop_vq_features(struct vca_device_desc __iomem *desc)
{
	return (u8 __iomem *)(_vop_vq_config(desc) +  ioread8(&desc->num_vq));
}

static inline u8 __iomem *
_vop_vq_configspace(struct vca_device_desc __iomem *desc)
{
	return _vop_vq_features(desc) + ioread8(&desc->feature_len) * 2;
}

static inline struct vca_device_desc __iomem *
_vop_host_device_desc(struct vca_device_desc __iomem  *desc)
{
	return  (void*)desc + (_vop_aligned_desc_size(desc) + sizeof(struct vca_device_ctrl));
}

static inline unsigned
_vop_total_desc_size(struct vca_device_desc __iomem *desc)
{
	return 2* (_vop_aligned_desc_size(desc) + sizeof(struct vca_device_ctrl));
}

/* This gets the device's feature bits. */
static u64 vop_get_features(struct virtio_device *vdev)
{
	unsigned int i, bits;
	u64 features = 0;
	struct vca_device_desc __iomem *desc = to_vopvdev(vdev)->desc;
	u8 __iomem *in_features = _vop_vq_features(desc);
	int feature_len = ioread8(&desc->feature_len);

	bits = min_t(unsigned, feature_len,
		     sizeof(vdev->features)) * 8;
	for (i = 0; i < bits; i++)
		if (ioread8(&in_features[i / 8]) & (BIT(i % 8)))
			features |= BIT(i);

	return features;
}

static int vop_finalize_features(struct virtio_device *vdev)
{
	unsigned int i, bits;
	struct vca_device_desc __iomem *desc = to_vopvdev(vdev)->desc;
	u8 feature_len = ioread8(&desc->feature_len);
	/* Second half of bitmap is features we accept. */
	u8 __iomem *out_features =
		_vop_vq_features(desc) + feature_len;

	/* Give virtio_ring a chance to accept features. */
	vca_vring_transport_features(vdev);

#ifdef VIRTIO_NET_F_OFFSET_RXBUF
	/* Give virtio_net a chance to accept features. */
	vca_virtio_net_features(vdev);

	dev_dbg(&vdev->dev, "%s feature VIRTIO_NET_F_OFFSET_RXBUF %s\n", __func__,
		    virtio_has_feature(vdev, VIRTIO_NET_F_OFFSET_RXBUF) ? "Enabled" :
		    "Disabled");
#else
#pragma message "Feature VIRTIO_NET_F_OFFSET_RXBUF not implemented in kernel"
	/* Unknown features should be cleaned by kernel */
	BUG_ON(virtio_has_feature(vdev, __VIRTIO_NET_F_OFFSET_RXBUF));
#endif

	memset_io(out_features, 0, feature_len);
	bits = min_t(unsigned, feature_len,
		     sizeof(vdev->features)) * 8;
	for (i = 0; i < bits; i++) {
		if (__virtio_test_bit(vdev, i))
			iowrite8(ioread8(&out_features[i / 8]) | (BIT(i % 8)),
				 &out_features[i / 8]);
	}
	return 0;
}

/*
 * Reading and writing elements in config space
 */
static void vop_get(struct virtio_device *vdev, unsigned int offset,
		    void *buf, unsigned len)
{
	struct vca_device_desc __iomem *desc = to_vopvdev(vdev)->desc;

	if (offset + len > ioread8(&desc->config_len))
		return;
	memcpy_fromio(buf, _vop_vq_configspace(desc) + offset, len);
}

static void vop_set(struct virtio_device *vdev, unsigned int offset,
		    const void *buf, unsigned len)
{
	struct vca_device_desc __iomem *desc = to_vopvdev(vdev)->desc;

	if (offset + len > ioread8(&desc->config_len))
		return;
	memcpy_toio(_vop_vq_configspace(desc) + offset, buf, len);
}

/*
 * The operations to get and set the status word just access the status
 * field of the device descriptor. set_status also interrupts the host
 * to tell about status changes.
 */
static u8 vop_get_status(struct virtio_device *vdev)
{
	return ioread8(&to_vopvdev(vdev)->desc->status);
}

static void vop_set_status(struct virtio_device *dev, u8 status)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vop_device *vpdev = vdev->vpdev;

	dev_dbg(&vpdev->dev, "%s: %x\n", __func__, status);

	vdev->status = status;
	if (!status)
		return;
	iowrite8(status, &vdev->desc->status);

	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_conf_db);
}

/* Inform host on a virtio device reset and wait for ack from host */
static void vop_reset_inform_host(struct virtio_device *dev)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vca_device_ctrl __iomem *dc = vdev->dc;
	struct vop_device *vpdev = vdev->vpdev;
	int retry;

	iowrite8(0, &dc->host_ack);
	iowrite8(1, &dc->vdev_reset);

	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_conf_db);

	/* Wait till host completes all card accesses and acks the reset */
	for (retry = 100; retry--;) {
		if (ioread8(&dc->host_ack))
			break;
		msleep(100);
	};

	dev_dbg(_vop_dev(vdev), "%s: retry: %d\n", __func__, retry);

	/* Reset status to 0 in case we timed out */
	iowrite8(0, &vdev->desc->status);
}

static void vop_reset(struct virtio_device *dev)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);

	dev_dbg(_vop_dev(vdev), "%s: virtio id %d\n",
		__func__, dev->id.device);

	vop_reset_inform_host(dev);
	complete_all(&vdev->reset_done);
}

static bool card_card_device_ready(struct _vop_vdev *vdev)
{
	return (READ_ONCE(vdev->status) & VIRTIO_CONFIG_S_DRIVER_OK) != 0;
}

/*
 * The virtio_ring code calls this API when it wants to notify the Host.
 * Some descriptors wait to read and send.
 */
static bool vop_notify_self(struct virtqueue *vq)

{
	struct _vop_vdev *vdev = vq ? vq->priv : NULL;
	struct vop_device *vpdev = vdev ? vdev->vpdev : NULL;

	if (!vpdev) {
		pr_err("NULL ptr %s %d\n", __func__, __LINE__);
		return false;

	}

	if (vq != vdev->vop_vringh[VRING_INDEX_SEND].vq) {
		pr_err("%s unexpected vring \n", __func__);
		 return false;
	}

	// transmit queue
	pr_debug("%s index %d\n", __func__, VRING_INDEX_SEND);
	descriptor_read_notification(&vdev->cdev);

	return true;
}



/*
 * The virtio_ring code calls this API when it wants to notify the Host.
 * Some descriptors wait to write.
 */
static bool vop_notify_peer(struct virtqueue *vq)
{
	struct _vop_vdev *vdev = vq ? vq->priv : NULL;
	struct vop_device *vpdev = vdev ? vdev->vpdev : NULL;

	if (!vpdev) {
		pr_err("NULL ptr %s %d\n", __func__, __LINE__);
		return false;
	}

	if (vq != vdev->vop_vringh[VRING_INDEX_RECV].vq) {
		 pr_err("%s unexpected vring \n", __func__);
		 return false;
	}

	dev_dbg(&vpdev->dev, "%s: virtio notification for ring \n", __func__);
	pr_debug("%s index %d\n", __func__, VRING_INDEX_RECV);

	if (card_card_device_ready(vdev)) {
		if (vdev->cdev.kvec_buff.remote_write_kvecs.mapped) {
			vop_kvec_buf_update(&vdev->cdev);
		}
	}

	return true;
}

static void vop_del_vq(struct virtqueue *vq, int n)
{
	struct _vop_vdev *vdev = to_vopvdev(vq->vdev);
	struct vop_device *vpdev = vdev->vpdev;

	dma_unmap_single(&vpdev->dev, vdev->pa[n],
		vdev->pa_size[n], DMA_BIDIRECTIONAL);
	vca_vring_del_virtqueue(vq);
	free_pages((unsigned long)vdev->vr[n], get_order(vdev->pa_size[n]));
	vdev->vr[n] = NULL;
}


static void vop_del_vqs(struct virtio_device *dev)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct virtqueue *vq, *n;
	int idx = 0;

	dev_dbg(_vop_dev(vdev), "%s\n", __func__);

	list_for_each_entry_safe(vq, n, &dev->vqs, list)
		vop_del_vq(vq, idx++);

	if(vdev->desc->type == VIRTIO_ID_NET)
		vdev->vpdev->hw_ops->set_net_dev_state(vdev->vpdev, false);
}

/* send irq to remote peer informing that new available/used descriptors
   are expected become available soon  soon */
static void vop_send_head_up_irq(
	struct vop_dev_common *cdev,
	enum vop_heads_up_notification op)
{
	struct _vop_vdev *vdev = container_of(cdev,
		struct _vop_vdev, cdev);
	int db = -1;

	switch (op) {
	case vop_notify_used:
		db = vdev->c2h_vdev_used_db;
		break;
	case vop_notify_available:
		db = vdev->c2h_vdev_avail_db;
		break;
	default:
		BUG_ON(true);
		return;
	}

	vdev->vpdev->hw_ops->send_intr(vdev->vpdev, db);
}


static void vop_consumed_from_card_notification(struct vringh *vrh)
{
	struct vop_vringh *vvrh = container_of(vrh, struct vop_vringh, vrh);
	struct _vop_vdev *vdev = vvrh->_vop_vdev;
	struct vop_device *vpdev = vdev ? vdev->vpdev : NULL;


	if (!vpdev) {
		pr_err("%s NULL vdev \n", __func__);
		return;
	}

	BUG_ON(!vvrh->vq);

	// we've finished using (read) descriptors from local device, notify
	// virtqueue
	dev_dbg(&vpdev->dev, "%s kicking vring no  %d, used_idx:%d\n",
			__func__, vvrh->index, vvrh->vring.vr.used->idx);
	vca_vring_interrupt(0, vvrh->vq);
}

/*
 * This routine will assign vring's allocated in host/io memory. Code in
 * virtio_ring.c however continues to access this io memory as if it were local
 * memory without io accessors.
 */
static struct virtqueue *vop_find_vq(struct virtio_device *dev,
				     unsigned index,
				     void (*callback)(struct virtqueue *vq),
				     const char *name)
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vop_device *vpdev = vdev->vpdev;
	struct vca_vqconfig __iomem *vqconfig;
	struct vca_vqconfig config;
	struct virtqueue *vq;
	void __iomem *va;
	struct _vca_vring_info __iomem *info;
	int vr_size, _vr_size, err;
	u32 num;
	bool (*notify)(struct virtqueue *);

	if (index >= ioread8(&vdev->desc->num_vq) || index >= VCA_MAX_VRINGS )
		return ERR_PTR(-ENOENT);

	if (!name)
		return ERR_PTR(-ENOENT);

	/* allocate and map vring memory */
	vqconfig = _vop_vq_config(vdev->desc) + index;
	memcpy_fromio(&config, vqconfig, sizeof(config));
	num = le16_to_cpu(config.num);
	_vr_size = vring_size(num, VCA_VIRTIO_RING_ALIGN);
	vr_size = PAGE_ALIGN(_vr_size + sizeof(struct _vca_vring_info));

	dev_dbg(&vpdev->dev, "allocating card vring idx%d num:%x size %x\n",
		index, num, (u32)vr_size);
	va =  (void*)__get_free_pages(GFP_KERNEL | __GFP_ZERO | GFP_DMA,
				      get_order(vr_size));
	if (!va) {
		dev_err(&vpdev->dev, "error allocating vring\n");
		return ERR_PTR(-ENOMEM);
	}

	vdev->pa_size[index] = vr_size;

	vdev->pa[index] = dma_map_single(&vpdev->dev, va,
		vr_size,
		DMA_BIDIRECTIONAL);
	if (dma_mapping_error(&vpdev->dev, vdev->pa[index])) {
		err = -ENOMEM;
		dev_err(&vpdev->dev, "error dma-mapping vring :%d\n", err);
		return ERR_PTR(-ENOMEM);
	}
	CHECK_DMA_ZONE(&vpdev->dev, vdev->pa[index]);

	dev_dbg(&vpdev->dev, "host vring: va:%p pa:%llx\n", va,
		vdev->pa[index]);

	vdev->vr[index] = va;

#ifdef VIRTIO_RING_F_DMA_MAP
	if (index == VRING_INDEX_SEND) {
		__virtio_clear_bit(dev, VIRTIO_RING_F_DMA_MAP);
	} else if (index == VRING_INDEX_RECV) {
		__virtio_set_bit(dev, VIRTIO_RING_F_DMA_MAP);
	}
	dev_dbg(&vpdev->dev, "creating VQ no %d DMA mapping %s\n", index,
		virtio_has_feature(dev, VIRTIO_RING_F_DMA_MAP) ? "yes" : "no");
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
					num, VCA_VIRTIO_RING_ALIGN,
					dev,
					false,
					(void __force *)va, notify, callback, name);

	if (!vq) {
		err = -ENOMEM;
		goto unmap;
	}
	info = va + _vr_size;

	writeq(cpu_to_le64(vdev->pa[index]), &vqconfig->address);

	vq->priv = vdev;

	/* initialize vringh for accessing local device */
	spin_lock_init(&vdev->vop_vringh[index].vr_spinlock);
	vdev->vop_vringh[index].vq = vq;
	vdev->vop_vringh[index]._vop_vdev = vdev;
	vdev->vop_vringh[index].index = index;
	vring_init(&vdev->vop_vringh[index].vring.vr,
		num, va, VCA_VIRTIO_RING_ALIGN);
	vca_vringh_init_kern(&vdev->vop_vringh[index].vrh,
		*(u32*)vca_vq_features(vdev->desc),
		num,
		false,
		vdev->vop_vringh[index].vring.vr.desc,
		vdev->vop_vringh[index].vring.vr.avail,
		vdev->vop_vringh[index].vring.vr.used);
	vdev->vop_vringh[index].vring.info = info;
	vdev->vop_vringh[index].vrh.notify =
		vop_consumed_from_card_notification;

	return vq;
unmap:
	vpdev->hw_ops->iounmap(vpdev, vdev->vr[index]);
	return ERR_PTR(err);
}

static int vop_find_vqs(struct virtio_device *dev, unsigned nvqs,
			struct virtqueue *vqs[],
			vq_callback_t *callbacks[],
			const char *names[])
{
	struct _vop_vdev *vdev = to_vopvdev(dev);
	struct vop_device *vpdev = vdev->vpdev;
	struct vca_device_ctrl __iomem *dc = vdev->dc;
	int i, err, retry;

	/* We must have this many virtqueues. */
	if (nvqs > ioread8(&vdev->desc->num_vq) || nvqs >= VCA_MAX_VRINGS) {
		dev_err(_vop_dev(vdev), "%s: error invalid vqs number %i\n",
				__func__, nvqs);
		return -ENOENT;
	}

	for (i = 0; i < nvqs; ++i) {
		dev_dbg(_vop_dev(vdev), "%s: %d: %s\n",
			__func__, i, names[i]);
		vqs[i] = vop_find_vq(dev, i, callbacks[i], names[i]);
		if (IS_ERR(vqs[i])) {
			err = PTR_ERR(vqs[i]);
			goto error;
		}
	}

	iowrite8(1, &dc->used_address_updated);
	/*
	 * Send an interrupt to the host to inform it that used
	 * rings have been re-assigned.
	 */

	vpdev->hw_ops->send_intr(vpdev, vdev->c2h_vdev_conf_db);
	for (retry = 100; --retry;) {
		if (!ioread8(&dc->used_address_updated))
			break;
		msleep(100);
	};

	dev_dbg(_vop_dev(vdev), "%s: retry: %d\n", __func__, retry);
	if (!retry) {
		err = -ENODEV;
		goto error;
	}

	err = vop_kvec_map_buf(&vdev->cdev.kvec_buff, vpdev,
				vdev->dc->kvec_buf_elems,
				vdev->dc->kvec_buf_address);

	if (err) {
		dev_err(_vop_dev(vdev), "%s: error mapping kvec buf\n", __func__);
		goto error;
	}

	err = common_dev_start(&vdev->cdev);
	if (err) {
		dev_err(_vop_dev(vdev), "%s: error starting common device\n", __func__);
		goto unmap_cdev;
	}


	if(vdev->desc->type == VIRTIO_ID_NET)
		vpdev->hw_ops->set_net_dev_state(vpdev, true);

	return 0;
unmap_cdev:
	vop_kvec_unmap_buf(&vdev->cdev.kvec_buff, vpdev);
error:
	vop_del_vqs(dev);
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

static irqreturn_t vop_virtio_intr_conf_handler(int irq, void *data)
{
	struct _vop_vdev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	pr_debug("%s %d in vop_main.c\n", __func__, __LINE__);

	vpdev->hw_ops->ack_interrupt(vpdev, vdev->h2c_vdev_conf_db);

	return IRQ_HANDLED;
}

static irqreturn_t vop_virtio_intr_avail_handler(int irq, void *data)
{
	struct _vop_vdev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	pr_debug("%s %d in vop_main.c\n", __func__, __LINE__);

	common_dev_heads_up_avail_irq(&vdev->cdev);
	vpdev->hw_ops->ack_interrupt(vpdev, vdev->h2c_vdev_avail_db);

	return IRQ_HANDLED;
}


static irqreturn_t vop_virtio_intr_used_handler(int irq, void *data)
{
	struct _vop_vdev *vdev = data;
	struct vop_device *vpdev = vdev->vpdev;

	common_dev_heads_up_used_irq(&vdev->cdev);
	vpdev->hw_ops->ack_interrupt(vpdev, vdev->h2c_vdev_used_db);

	return IRQ_HANDLED;
}

static void vop_virtio_release_dev(struct device *_d)
{
	/*
	 * No need for a release method similar to virtio PCI.
	 * Provide an empty one to avoid getting a warning from core.
	 */
}

static int _vop_common_dev_init(
		struct _vop_vdev *vdev,
		int num_descs,
		struct vca_device_desc __iomem *host_dd)

{
	struct vop_vringh *vrings = vdev->vop_vringh;
	struct vop_vringh *vringh_tx = vrings + VRING_INDEX_SEND;
	struct vop_vringh *vringh_rcv = vrings + VRING_INDEX_RECV;

	return common_dev_init(&vdev->cdev, vdev->vpdev,
			vringh_tx, vringh_rcv,
			num_descs, vop_send_head_up_irq,
			vdev->desc, host_dd);
}

/*
 * adds a new device and register it with virtio
 * appropriate drivers are loaded by the device model
 */
static int _vop_add_device(struct vca_device_desc __iomem *d,
			   unsigned int offset, struct vop_device *vpdev,
			   int dnode)
{
	struct _vop_vdev *vdev;
	struct vop_info *vi = vpdev->priv;
	int ret;
	u8 type = ioread8(&d->type);
	struct vca_vqconfig config;
	int num_rcv_descs;
	struct vca_device_desc __iomem *host_dd = _vop_host_device_desc(d);
	struct vca_device_ctrl __iomem *host_dc =
		(void __iomem *)host_dd + _vop_aligned_desc_size(d);


	if (d->num_vq != 2) {
		dev_err(&vpdev->dev, "%s device not supported\n", __func__);
		return -ENODEV;
	}

	memcpy_fromio(&config, _vop_vq_config(d) + VRING_INDEX_RECV, sizeof(config));
	num_rcv_descs = config.num;

	vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
	if (!vdev) {
		dev_err(&vpdev->dev, "Cannot allocate vop dev %u type %u\n",
			offset, type);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vdev->list);
	vdev->vpdev = vpdev;
	vdev->vdev.dev.parent = &vpdev->dev;
	vdev->vdev.dev.release = vop_virtio_release_dev;
	vdev->vdev.id.device = type;
	vdev->vdev.config = &vop_vq_config_ops;
	vdev->desc = d;
	vdev->dc = (void __iomem *)d + _vop_aligned_desc_size(d);
	vdev->dnode = dnode;
	vdev->vdev.priv = (void *)(u64)dnode;
	init_completion(&vdev->reset_done);

	vdev->host.desc = host_dd;
	vdev->host.dc = host_dc;

	vdev->h2c_vdev_conf_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_conf_db_cookie = vpdev->hw_ops->request_irq(vpdev,
			vop_virtio_intr_conf_handler, "virtio intr conf",
			vdev, vdev->h2c_vdev_conf_db);
	if (IS_ERR(vdev->virtio_conf_db_cookie)) {
		ret = PTR_ERR(vdev->virtio_conf_db_cookie);
		goto kfree;
	}
	iowrite8((u8)vdev->h2c_vdev_conf_db, &vdev->dc->h2c_vdev_conf_db);
	vdev->c2h_vdev_conf_db = ioread8(&vdev->dc->c2h_vdev_conf_db);


	vdev->h2c_vdev_avail_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_avail_db_cookie = vpdev->hw_ops->request_irq(vpdev,
			vop_virtio_intr_avail_handler, "virtio intr avail",
			vdev, vdev->h2c_vdev_avail_db);
	if (IS_ERR(vdev->virtio_avail_db_cookie)) {
		ret = PTR_ERR(vdev->virtio_avail_db_cookie);
		goto kfree;
	}
	iowrite8((u8)vdev->h2c_vdev_avail_db, &vdev->dc->h2c_vdev_avail_db);
	vdev->c2h_vdev_avail_db = ioread8(&vdev->dc->c2h_vdev_avail_db);



	vdev->h2c_vdev_used_db = vpdev->hw_ops->next_db(vpdev);
	vdev->virtio_used_db_cookie = vpdev->hw_ops->request_irq(vpdev,
			vop_virtio_intr_used_handler, "virtio intr used",
			vdev, vdev->h2c_vdev_used_db);
	if (IS_ERR(vdev->virtio_used_db_cookie)) {
		ret = PTR_ERR(vdev->virtio_used_db_cookie);
		goto kfree;
	}
	iowrite8((u8)vdev->h2c_vdev_used_db, &vdev->dc->h2c_vdev_used_db);
	vdev->c2h_vdev_used_db = ioread8(&vdev->dc->c2h_vdev_used_db);

	ret = _vop_common_dev_init(vdev, num_rcv_descs, host_dd);
	if (ret)
		return ret;
	/* this is written to HOST device control */
	host_dc->kvec_buf_address = vdev->cdev.kvec_buff.local_write_kvecs.pa;
	host_dc->kvec_buf_elems  = num_rcv_descs;

	dev_dbg(&vpdev->dev, "card side KVEC buffer pa %llx elems %x\n",
			       host_dc->kvec_buf_address,
			       host_dc->kvec_buf_elems);

	dev_dbg(&vpdev->dev, "host allocated  KVEC at %llx elems %x\n",
			       vdev->dc->kvec_buf_address,
			       vdev->dc->kvec_buf_elems);

	ret = vca_register_virtio_device(&vdev->vdev);
	if (ret) {
		dev_err(_vop_dev(vdev),
			"Failed to register vop device %u type %u\n",
			offset, type);
		goto free_irq;
	}
	writeq((u64)vdev, &vdev->dc->vdev);
	dev_dbg(_vop_dev(vdev), "%s: registered vop device %u type %u vdev %p\n",
		__func__, offset, type, vdev);

	vdev->virtio_id = type;

	list_add_tail(&vdev->list, &vi->vdev_list);

	return 0;

free_irq:
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_conf_db_cookie, vdev);
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_avail_db_cookie, vdev);
	vpdev->hw_ops->free_irq(vpdev, vdev->virtio_used_db_cookie, vdev);
kfree:
	kfree(vdev);
	return ret;
}

/*
 * match for a vop device with a specific desc pointer
 */
static int vop_match_desc(struct device *dev, void *data)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 7, 0) && !defined(dev_to_virtio)
#define dev_to_virtio(dev) container_of(dev, struct virtio_device, dev)
#endif
	struct virtio_device *_dev = dev_to_virtio(dev);
	struct _vop_vdev *vdev = to_vopvdev(_dev);

	return vdev->desc == (void __iomem *)data;
}

/*
 * removes a virtio device if a hot remove event has been
 * requested by the host.
 */
static int _vop_remove_device(struct vca_device_desc __iomem *d,
			      unsigned int offset, struct vop_device *vpdev)
{
	struct vca_device_ctrl __iomem *dc
		= (void __iomem *)d + _vop_aligned_desc_size(d);
	struct _vop_vdev *vdev = (struct _vop_vdev *)readq(&dc->vdev);
	u8 status;
	int ret = -1;

	if (ioread8(&dc->config_change) == VCA_VIRTIO_PARAM_DEV_REMOVE) {
		dev_dbg(&vpdev->dev,
			"%s %d config_change %d type %d vdev %p\n",
			__func__, __LINE__,
			ioread8(&dc->config_change), ioread8(&d->type), vdev);

		/* Set flag to inform the host that card still response. */
		iowrite8(GUEST_ACK_RECEIVED, &dc->guest_ack);

		list_del(&vdev->list);
		common_dev_stop(&vdev->cdev);
		common_dev_deinit(&vdev->cdev, vdev->vpdev);

		status = ioread8(&d->status);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
		reinit_completion(&vdev->reset_done);
#else
		INIT_COMPLETION(vdev->reset_done);
#endif
		vca_unregister_virtio_device(&vdev->vdev);
		vpdev->hw_ops->free_irq(vpdev, vdev->virtio_conf_db_cookie, vdev);
		iowrite8(-1, &dc->h2c_vdev_conf_db);
		vpdev->hw_ops->free_irq(vpdev, vdev->virtio_avail_db_cookie, vdev);
		iowrite8(-1, &dc->h2c_vdev_avail_db);
		vpdev->hw_ops->free_irq(vpdev, vdev->virtio_used_db_cookie, vdev);
		iowrite8(-1, &dc->h2c_vdev_used_db);

		if (status & VIRTIO_CONFIG_S_DRIVER_OK)
			wait_for_completion_interruptible(&vdev->reset_done);
		kfree(vdev);
		iowrite8(GUEST_ACK_DONE, &dc->guest_ack);
		dev_dbg(&vpdev->dev, "%s %d guest_ack %d\n",
			__func__, __LINE__, ioread8(&dc->guest_ack));
		iowrite8(-1, &d->type);
		ret = 0;
	}
	return ret;
}

#define REMOVE_DEVICES true

static void _vop_scan_devices(void __iomem *dp, struct vop_device *vpdev,
			      bool remove, int dnode)
{
	s8 type;
	unsigned int i;
	struct vca_device_desc __iomem *d;
	struct vca_device_ctrl __iomem *dc;
	struct device *dev;
	int ret;

	for (i = sizeof(struct vca_bootparam);
			i < VCA_DP_SIZE; i += _vop_total_desc_size(d)) {
		d = dp + i;
		dc = (void __iomem *)d + _vop_aligned_desc_size(d);
		/*
		 * This read barrier is paired with the corresponding write
		 * barrier on the host which is inserted before adding or
		 * removing a virtio device descriptor, by updating the type.
		 */
		rmb();
		type = ioread8(&d->type);

		/* end of list */
		if (type == 0)
			break;

		if (type == -1)
			continue;

		/* device already exists */
		dev = device_find_child(&vpdev->dev, (void __force *)d,
					vop_match_desc);
		if (dev) {
			if (remove)
				iowrite8(VCA_VIRTIO_PARAM_DEV_REMOVE,
					 &dc->config_change);
			put_device(dev);
			ret = _vop_remove_device(d, i, vpdev);
			if (remove) {
				iowrite8(0, &dc->config_change);
				iowrite8(GUEST_ACK_NONE, &dc->guest_ack);
			}
			continue;
		}

		/* new device */
		dev_dbg(&vpdev->dev, "%s %d Adding new virtio device %p\n",
			__func__, __LINE__, d);
		if (!remove)
			_vop_add_device(d, i, vpdev, dnode);
	}
}

static void vop_scan_devices(struct vop_info *vi,
			     struct vop_device *vpdev, bool remove)
{
	void __iomem *dp = vpdev->hw_ops->get_dp(vpdev);

	if (!dp)
		return;
	mutex_lock(&vi->vop_mutex);
	_vop_scan_devices(dp, vpdev, remove, vpdev->dnode);
	mutex_unlock(&vi->vop_mutex);
}

/*
 * vop_hotplug_device tries to find changes in the device page.
 */
static void vop_hotplug_devices(struct work_struct *work)
{
	struct vop_info *vi = container_of(work, struct vop_info,
					     hotplug_work);

	vop_scan_devices(vi, vi->vpdev, !REMOVE_DEVICES);
}

/*
 * Interrupt handler for hot plug/config changes etc.
 */
static irqreturn_t vop_extint_handler(int irq, void *data)
{
	struct vop_info *vi = data;
	struct vca_bootparam __iomem *bp;
	struct vop_device *vpdev = vi->vpdev;

	bp = vpdev->hw_ops->get_dp(vpdev);
	dev_dbg(&vpdev->dev, "%s %d hotplug work\n",
		__func__, __LINE__);
	vpdev->hw_ops->ack_interrupt(vpdev, ioread8(&bp->h2c_config_db));
	schedule_work(&vi->hotplug_work);
	return IRQ_HANDLED;
}

static int vop_driver_probe(struct vop_device *vpdev)
{
	struct vop_info *vi;
	int rc;

	dev_info(&vpdev->dev, "buildinfo: build no " BUILD_NUMBER ", built on " BUILD_ONDATE ".\n");

	vi = kzalloc(sizeof(*vi), GFP_KERNEL);
	if (!vi) {
		rc = -ENOMEM;
		goto exit;
	}
	vpdev->priv = vi;
	vi->vpdev = vpdev;

	mutex_init(&vi->vop_mutex);
	INIT_WORK(&vi->hotplug_work, vop_hotplug_devices);
	if (vpdev->dnode) {
		rc = vop_host_init(vi);
		if (rc < 0)
			goto free;
	} else {
		struct vca_bootparam __iomem *bootparam;
		INIT_LIST_HEAD(&vi->vdev_list);

		vop_scan_devices(vi, vpdev, !REMOVE_DEVICES);

		vi->dma_ch = vpdev->dma_ch;
		vi->h2c_config_db = vpdev->hw_ops->next_db(vpdev);
		vi->cookie = vpdev->hw_ops->request_irq(vpdev,
							vop_extint_handler,
							"virtio_config_intr",
							vi, vi->h2c_config_db);
		if (IS_ERR(vi->cookie)) {
			rc = PTR_ERR(vi->cookie);
			goto free;
		}
		bootparam = vpdev->hw_ops->get_dp(vpdev);
		iowrite8(vi->h2c_config_db, &bootparam->h2c_config_db);
	}
	vop_init_debugfs(vi);
	return 0;
free:
	kfree(vi);
exit:
	return rc;
}

static void vop_driver_remove(struct vop_device *vpdev)
{
	struct vop_info *vi = vpdev->priv;

	if (vpdev->dnode) {
		vop_host_uninit(vi);
	} else {
		struct vca_bootparam __iomem *bootparam =
			vpdev->hw_ops->get_dp(vpdev);
		if (bootparam)
			iowrite8(-1, &bootparam->h2c_config_db);
		vpdev->hw_ops->free_irq(vpdev, vi->cookie, vi);
		flush_work(&vi->hotplug_work);
		vop_scan_devices(vi, vpdev, REMOVE_DEVICES);
	}
	vop_exit_debugfs(vi);
	kfree(vi);
}

static struct vop_device_id id_table[] = {
	{ VOP_DEV_TRNSP, VOP_DEV_ANY_ID },
	{ 0 },
};

static struct vop_driver vop_driver = {
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table = id_table,
	.probe = vop_driver_probe,
	.remove = vop_driver_remove,
};

module_vop_driver(vop_driver);

MODULE_DEVICE_TABLE(mbus, id_table);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel(R) Virtio Over PCIe (VOP) driver");
MODULE_LICENSE("GPL v2");
