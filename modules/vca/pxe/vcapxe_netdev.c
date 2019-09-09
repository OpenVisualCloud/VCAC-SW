/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2019 Intel Corporation.
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
 * Intel VCA PXE support code
 */

#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/version.h>

#include "vcapxe.h"
#include "vcapxe_register.h"

static int vcapxe_ring_put_skb(struct vcapxe_ring *ring, struct sk_buff *skb)
{
	// Read the write ptr - we, the writer are the only one writing it.
	u32 current_write = ioread32(&ring->write_index);
	u32 possible_next_write = (current_write + 1) % VCAPXE_RING_SIZE;
	u32 read_snapshot = ioread32(&ring->read_index);
	if (possible_next_write == read_snapshot)
		return 0; // we would overfill the ring - cancelling
	else {
		struct vcapxe_frame *frame = &ring->frames[current_write];
		memcpy_toio(frame->frame_data, skb->data, skb->len);
		frame->frame_len = skb->len;
		wmb(); // make sure the write_index isn't incremented before writing the frame is finished
		iowrite32(possible_next_write, &ring->write_index);
		return 1;
	}
}

static int vcapxe_ring_get_skb(struct vcapxe_ring *ring, struct net_device *netdev)
{
	// Read the read - we, the reader are the only one writing it.
	u32 current_read = ioread32(&ring->read_index);
	u32 current_write = ioread32(&ring->write_index);

	if (current_read == current_write)
		return 0; // ring is empty, can't do anything
	else {
		struct vcapxe_frame *recvd_frame = &ring->frames[current_read];
		struct sk_buff *skb;
		u32 next_read;
		u32 recvd_size;

		recvd_size = recvd_frame->frame_len;
		skb = netdev_alloc_skb(netdev, recvd_size + 2);
		if (skb != NULL) {
			memcpy_fromio(skb_put(skb, recvd_size), recvd_frame->frame_data, recvd_size);

			rmb(); // make sure read isn't scheduled after ACK

			next_read = (current_read + 1) % VCAPXE_RING_SIZE;
			iowrite32(next_read, &ring->read_index);

			skb->protocol = eth_type_trans(skb, netdev);

			netif_rx(skb);
		} else {
			netdev_warn(netdev, "can't alloc skb for incoming packet\n");
		}
		return 1;
	}
}

static netdev_tx_t vcapxe_start_xmit(struct sk_buff *skb, struct net_device *netdev) {
	struct vcapxe_private* priv = (struct vcapxe_private*) netdev_priv(netdev);
	struct vcapxe_shared* shared = priv->shared_area;
	int status = NETDEV_TX_OK;

	if (shared != NULL) {
		if (!vcapxe_ring_put_skb(&shared->h2n, skb))
			netdev_warn(netdev, "trying to xmit but host to node buffer is full\n");
	} else netdev_warn(netdev, "trying to xmit but the PXE netcard has no shared buffer\n");

	dev_kfree_skb(skb);

	return status;
}

static const struct net_device_ops vcapxe_ops = {
    .ndo_start_xmit = vcapxe_start_xmit,
};

irqreturn_t vcapxe_doorbell_irq(int irq, void *data)
{
	struct vcapxe_private* priv = (struct vcapxe_private*) data;
	struct vcapxe_shared* shared = priv->shared_area;
	if (ioread32(&shared->shutdown) == 1)
	{
		// Kill the iface
		schedule_work(&priv->shutdown_worker);
	} else {
		// Try to get as many packets as there are in the buffer.
		while (vcapxe_ring_get_skb(&shared->n2h, priv->netdev)) ;
	}
	return IRQ_HANDLED;
}

static void vcapxe_shutdown_handler(struct work_struct* work)
{
	struct vcapxe_private *priv = container_of(work, struct vcapxe_private, shutdown_worker);
	struct vcapxe_shared *shared = priv->shared_area;
	unsigned long flags;

	// ACK the shutdown
	iowrite32(2, &shared->shutdown);
	wmb();
	vcapxe_finalize(priv->pxe_dev, shared);

	priv->shared_area = NULL;
	spin_lock_irqsave(&priv->pxe_dev->state_lock, flags);
	priv->pxe_dev->state = VCAPXE_STATE_ACTIVE;
	spin_unlock_irqrestore(&priv->pxe_dev->state_lock, flags);
}

int vcapxe_prepare_netdev(struct net_device* netdev)
{
	struct vcapxe_private* priv = (struct vcapxe_private*) netdev_priv(netdev);

    netdev->netdev_ops = &vcapxe_ops;
    netdev->flags &= ~IFF_NOARP;
    eth_hw_addr_random(netdev);

    INIT_WORK(&priv->shutdown_worker, vcapxe_shutdown_handler);

	priv->shared_area = NULL;

    return 0;
}

int vcapxe_enable_netdev(struct vcapxe_device *dev)
{
	int err;
	struct net_device *netdev;
	struct vcapxe_private *priv;

	unsigned long flags;

	spin_lock_irqsave(&dev->state_lock, flags);
	if (dev->state != VCAPXE_STATE_INACTIVE) {
		spin_unlock_irqrestore(&dev->state_lock, flags);
		return -EEXIST;
	}
	spin_unlock_irqrestore(&dev->state_lock, flags);

	// This is required so we can compile on both CentOS and Ubuntu.
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	netdev = alloc_netdev(sizeof(struct vcapxe_private), dev->mdev_name, ether_setup);
#else
	netdev = alloc_netdev(sizeof(struct vcapxe_private), dev->mdev_name, NET_NAME_PREDICTABLE, ether_setup);
#endif

	if (netdev < 0)
		return PTR_ERR(netdev);

	vcapxe_prepare_netdev(netdev);

	priv = (struct vcapxe_private*) netdev_priv(netdev);

	dev->netdev = netdev;

	priv->netdev = netdev;
	priv->hw_ops = dev->hw_ops;
	priv->xdev = dev->xdev;
	priv->pxe_dev = dev;

	err = register_netdev(priv->netdev);
	if (err == 0) {
		netif_carrier_off(priv->netdev);
		/* This procedure is the only thing that can change
		 * the state from INACTIVE to ACTIVE, and it should
		 * protected by the dev->lock acquired by the caller,
		 * so spinlock here is only to avoid a partial write
		 * being seen.
		 */
		spin_lock_irqsave(&dev->state_lock, flags);
		dev->state = VCAPXE_STATE_ACTIVE;
		spin_unlock_irqrestore(&dev->state_lock, flags);
	} else {
		free_netdev(netdev);
	}

	return err;
}

int vcapxe_disable_netdev(struct vcapxe_device *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->state_lock, flags);
	if (dev->state == VCAPXE_STATE_INACTIVE) {
		spin_unlock_irqrestore(&dev->state_lock, flags);
		return -ENOENT;
	} else if (dev->state == VCAPXE_STATE_RUNNING) {
		spin_unlock_irqrestore(&dev->state_lock, flags);
		return -EBUSY;
	}
	/* Card isn't PXE booting, nobody will try to get up interfaces, we can set
	 * the state and proceed with the teardown.*/
	dev->state = VCAPXE_STATE_INACTIVE;
	spin_unlock_irqrestore(&dev->state_lock, flags);

	unregister_netdev(dev->netdev);
	free_netdev(dev->netdev);
	dev->netdev = NULL;

	return 0;
}

int vcapxe_query_netdev(struct vcapxe_device *dev)
{
	unsigned long flags;
	int saved_state;

	spin_lock_irqsave(&dev->state_lock, flags);
	saved_state = dev->state;
	spin_unlock_irqrestore(&dev->state_lock, flags);

	return saved_state;
}

void vcapxe_start_netdev(struct vcapxe_device *dev, void *shared)
{
	struct vcapxe_private *priv = (struct vcapxe_private*) netdev_priv(dev->netdev);
	unsigned long flags;

	spin_lock_irqsave(&dev->state_lock, flags);
	dev->state = VCAPXE_STATE_RUNNING;
	spin_unlock_irqrestore(&dev->state_lock, flags);

	priv->shared_area = (struct vcapxe_shared*) shared;

	netif_carrier_on(priv->netdev);
	netif_wake_queue(priv->netdev);

	mutex_unlock(&priv->pxe_dev->lock);
}

void vcapxe_stop_netdev(struct vcapxe_device *dev)
{
	struct vcapxe_private *priv = (struct vcapxe_private*) netdev_priv(dev->netdev);
	unsigned long flags;

	netif_stop_queue(priv->netdev);
	netif_carrier_off(priv->netdev);

	priv->shared_area = NULL;

	spin_lock_irqsave(&dev->state_lock, flags);
	dev->state = VCAPXE_STATE_ACTIVE;
	spin_unlock_irqrestore(&dev->state_lock, flags);
}

void vcapxe_teardown_netdev(struct vcapxe_device *dev) {
	// Shutdown, but more brutal.
	struct vcapxe_private *priv;
	struct vcapxe_shared *shared;
	unsigned long flags;
	int state;

	spin_lock_irqsave(&dev->state_lock, flags);
	state = dev->state;
	spin_unlock_irqrestore(&dev->state_lock, flags);

	if (state != VCAPXE_STATE_RUNNING) return; // no teardown required, actually

	priv = (struct vcapxe_private *) netdev_priv(dev->netdev);
	shared = priv->shared_area;

	vcapxe_finalize(dev, shared);

	priv->shared_area = NULL;
	spin_lock_irqsave(&dev->state_lock, flags);
	priv->pxe_dev->state = VCAPXE_STATE_ACTIVE;
	spin_unlock_irqrestore(&dev->state_lock, flags);
}
