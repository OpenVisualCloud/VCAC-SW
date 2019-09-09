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
 * Intel PLX87XX VCA PCIe driver
 */
#ifndef _PLX_INTR_H_
#define _PLX_INTR_H_

#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/idr.h>

#define PLX_NUM_OFFSETS 32
/**
 * struct plx_intr_info - Contains h/w specific interrupt sources
 * information.
 *
 * @intr_start_idx: Contains the starting indexes of the
 * interrupt types.
 * @intr_len: Contains the length of the interrupt types.
 */
struct plx_intr_info {
	u16 intr_start_idx;
	u16 intr_len;
};

/**
 * struct plx_irq_info - OS specific irq information
 *
 * @next_avail_src: next available doorbell that can be assigned.
 * @plx_msi_map: The MSI/MSI-x mapping information.
 * @cb_ida: callback ID allocator to track the callbacks registered.
 * @plx_intr_lock: spinlock to protect the interrupt callback list.
 * @plx_thread_lock: spinlock to protect the thread callback list.
 *		   This lock is used to protect against thread_fn while
 *		   plx_intr_lock is used to protect against interrupt handler.
 * @cb_list: Array of callback lists one for each source.
 * @mask: Mask used by the main thread fn to call the underlying thread fns.
 */
struct plx_irq_info {
	int next_avail_src;
	u32 *plx_msi_map;
	struct ida cb_ida;
	spinlock_t plx_intr_lock;
	spinlock_t plx_thread_lock;
	struct list_head *cb_list;
	unsigned long mask;
};

/**
 * struct plx_intr_cb - Interrupt callback structure.
 *
 * @handler: The callback function
 * @thread_fn: The thread_fn.
 * @data: Private data of the requester.
 * @cb_id: The callback id. Identifies this callback.
 * @list: list head pointing to the next callback structure.
 */
struct plx_intr_cb {
	irq_handler_t handler;
	irq_handler_t thread_fn;
	void *data;
	int cb_id;
	struct list_head list;
	char *name;
};

/* Forward declaration */
struct plx_device;

int plx_next_db(struct plx_device *xdev);
struct vca_irq *
plx_request_threaded_irq(struct plx_device *xdev,
			 irq_handler_t handler, irq_handler_t thread_fn,
			 const char *name, void *data, int intr_src);
void plx_free_irq(struct plx_device *xdev,
		  struct vca_irq *cookie, void *data);
int plx_setup_interrupts(struct plx_device *xdev, struct pci_dev *pdev);
void plx_free_interrupts(struct plx_device *xdev, struct pci_dev *pdev);

/**
 * plx_irq_set_affinity_node_hint -
 * Assign PCI device interrupt to CPU owning the PCI bus.
 *
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static inline int plx_irq_set_affinity_node_hint (struct pci_dev *pdev)
{
	int rc = 0;
	const struct cpumask *irqmask;
// #pragma message "FIXME pcibus to node mapping"
	/* pcibus_to_node does not work on GZP systems. Need to investigate this
	 or implement workaround rather than assume two CPU system */
	int node = (pdev->bus->number > 0x7f) ? 1 : 0;

	if (node != -1) {
		irqmask = cpumask_of_node(node);
		rc = irq_set_affinity_hint(pdev->irq, irqmask);
	}
	return rc;
}

/**
 * plx_irq_clean_affinity_node_hint -
 * Remove PCI device interrupt assignment to CPU.
 *
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static inline int plx_irq_clean_affinity_node_hint (struct pci_dev *pdev)
{
	return irq_set_affinity_hint(pdev->irq, NULL);
}

#endif

