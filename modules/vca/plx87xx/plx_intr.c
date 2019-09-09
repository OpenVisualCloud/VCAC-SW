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
 *
 */
#include <linux/pci.h>
#include <linux/interrupt.h>

#include "plx_device.h"
#include "plx_hw.h"

static irqreturn_t plx_thread_fn(int irq, void *dev)
{
	struct plx_device *xdev = dev;
	struct plx_intr_info *intr_info = xdev->intr_info;
	struct plx_irq_info *irq_info = &xdev->irq_info;
	struct plx_intr_cb *intr_cb;
	struct pci_dev *pdev = container_of(&xdev->pdev->dev,
					    struct pci_dev, dev);
	int i;

	spin_lock(&irq_info->plx_thread_lock);
	for (i = intr_info->intr_start_idx;
			i < intr_info->intr_len; i++)
		if (test_and_clear_bit(i, &irq_info->mask)) {
			list_for_each_entry(intr_cb, &irq_info->cb_list[i],
					    list)
				if (intr_cb->thread_fn)
					intr_cb->thread_fn(pdev->irq,
							 intr_cb->data);
		}
	spin_unlock(&irq_info->plx_thread_lock);
	return IRQ_HANDLED;
}

/**
 * plx_interrupt - Generic interrupt handler for
 * MSI and INTx based interrupts.
 */
static irqreturn_t plx_interrupt(int irq, void *dev)
{
	struct plx_device *xdev = dev;
	struct plx_intr_info *intr_info = xdev->intr_info;
	struct plx_irq_info *irq_info = &xdev->irq_info;
	struct plx_intr_cb *intr_cb;
	struct pci_dev *pdev = container_of(&xdev->pdev->dev,
					    struct pci_dev, dev);
	u32 mask;
	int i;

	mask = plx_ack_interrupt(xdev);

	dev_dbg(&xdev->pdev->dev, "%s mask 0x%x\n", __func__, mask);

	if (!mask) {
		/* Because after send few DB, only firsts have some mask,
		 * others are called, when mask is cleared. It can increase IRQ unhandled counter,
		 * and disables IRQ, during heavy workload.
		 * Never more return IRQ_NONE; in this case. */
		return IRQ_HANDLED;
	}

	spin_lock(&irq_info->plx_intr_lock);
	for (i = intr_info->intr_start_idx;
			i < intr_info->intr_len; i++)
		if (mask & BIT(i)) {

			dev_dbg(&xdev->pdev->dev, "%s bit %d in mask set\n", __func__, i);
			list_for_each_entry(intr_cb, &irq_info->cb_list[i],
					    list)
				if (intr_cb->handler) {
					dev_dbg(&xdev->pdev->dev, "%s calling cb handler\n",
						__func__);
					intr_cb->handler(pdev->irq,
							 intr_cb->data);
				} else {
					dev_dbg(&xdev->pdev->dev, "%s no cb handler\n", __func__);
				}

			set_bit(i, &irq_info->mask);
		}
	spin_unlock(&irq_info->plx_intr_lock);
	return IRQ_WAKE_THREAD;
}

/* Return the interrupt offset from the index. Index is 0 based. */
static u16 plx_map_src_to_offset(struct plx_device *xdev, int intr_src)
{
	if (intr_src >= xdev->intr_info->intr_len)
		return PLX_NUM_OFFSETS;

	return xdev->intr_info->intr_start_idx + intr_src;
}

/**
 * plx_register_intr_callback - Register a callback handler for the
 * given source id.
 *
 * @xdev: pointer to the plx_device instance
 * @idx: The source id to be registered.
 * @handler: The function to be called when the source id receives
 * the interrupt.
 * @thread_fn: thread fn. corresponding to the handler
 * @data: Private data of the requester.
 * Return id of the registered callback, or -ENOMEM, or the (negative) error returned by ida_simple_get().
 */
static int plx_register_intr_callback(struct plx_device *xdev,
			   u8 idx, irq_handler_t handler,
			   irq_handler_t thread_fn,
			   const char *name,
			   void *data)
{
	struct plx_intr_cb *intr_cb, *existing_cb;
	unsigned long flags;
	int rc;
	int name_len;

	intr_cb = kmalloc(sizeof(*intr_cb), GFP_KERNEL);
	if (!intr_cb)
		return -ENOMEM;

	intr_cb->handler = handler;
	intr_cb->thread_fn = thread_fn;
	intr_cb->data = data;
	intr_cb->name = NULL;
	intr_cb->cb_id = ida_simple_get(&xdev->irq_info.cb_ida,
		0, 0, GFP_KERNEL);
	if (intr_cb->cb_id < 0) {
		dev_err(&xdev->pdev->dev, "No available callback entries for use\n");
		rc = intr_cb->cb_id;
		goto ida_fail;
	}
	if (name) {
		name_len = strlen(name);
		intr_cb->name = kmalloc(name_len + 1, GFP_KERNEL);
		if (intr_cb->name) {
			strncpy(intr_cb->name, name, name_len + 1);
			intr_cb->name[name_len] = '\0';
		}
	}

	spin_lock(&xdev->irq_info.plx_thread_lock);
	spin_lock_irqsave(&xdev->irq_info.plx_intr_lock, flags);
	if (!list_empty(&xdev->irq_info.cb_list[idx])) {
		dev_warn(&xdev->pdev->dev,"Interrupt %d shared\n", idx);
		if(name)
			dev_warn(&xdev->pdev->dev, "Adding %s element to list. Existing elements:\n", name);
		else
			dev_warn(&xdev->pdev->dev, "Adding function 0x%llx to list. Existing elements:\n", (u64)(void *) thread_fn);
		list_for_each_entry(existing_cb, &xdev->irq_info.cb_list[idx], list) {
			if(existing_cb->name)
				dev_warn(&xdev->pdev->dev, "IRQ name:%s\n", existing_cb->name);
			else
				dev_warn(&xdev->pdev->dev, "IRQ function address: 0x%llx\n", (u64)(void *) existing_cb->thread_fn);
		}

	}
	list_add_tail(&intr_cb->list, &xdev->irq_info.cb_list[idx]);
	spin_unlock_irqrestore(&xdev->irq_info.plx_intr_lock, flags);
	spin_unlock(&xdev->irq_info.plx_thread_lock);

	return intr_cb->cb_id; // ignore KW: "Possible memory leak. Dynamic memory stored in 'intr_cb' allocated through function 'kmalloc' can be lost". The allocated 'intr_cb' structure has been stored in kernel list with 'list_add_tail()' and is accessible with 'list_entry()' macro. It is freed later by 'plx_unregister_intr_callback()'
ida_fail:
	kfree(intr_cb);
	return rc;
}

/**
 * plx_unregister_intr_callback - Unregister the callback handler
 * identified by its callback id.
 *
 * @xdev: pointer to the plx_device instance
 * @idx: The callback structure id to be unregistered.
 * Return the source id that was unregistered or PLX_NUM_OFFSETS if no
 * such callback handler was found.
 */
static u8 plx_unregister_intr_callback(struct plx_device *xdev, u32 idx)
{
	struct list_head *pos, *tmp;
	struct plx_intr_cb *intr_cb;
	unsigned long flags;
	int i;

	spin_lock(&xdev->irq_info.plx_thread_lock);
	spin_lock_irqsave(&xdev->irq_info.plx_intr_lock, flags);
	for (i = 0;  i < PLX_NUM_OFFSETS; i++) {
		list_for_each_safe(pos, tmp, &xdev->irq_info.cb_list[i]) {
			intr_cb = list_entry(pos, struct plx_intr_cb, list);
			if (intr_cb->cb_id == idx) {
				list_del(pos);
				ida_simple_remove(&xdev->irq_info.cb_ida,
						  intr_cb->cb_id);
				if (intr_cb->name)
					kfree(intr_cb->name);
				kfree(intr_cb);
				spin_unlock_irqrestore(
					&xdev->irq_info.plx_intr_lock, flags);
				spin_unlock(&xdev->irq_info.plx_thread_lock);
				return i;
			}
		}
	}
	spin_unlock_irqrestore(&xdev->irq_info.plx_intr_lock, flags);
	spin_unlock(&xdev->irq_info.plx_thread_lock);
	return PLX_NUM_OFFSETS;
}

/**
 * plx_setup_callbacks - Initialize data structures needed
 * to handle callbacks.
 *
 * @xdev: pointer to plx_device instance
 */
static int plx_setup_callbacks(struct plx_device *xdev)
{
	int i;

	xdev->irq_info.cb_list = kmalloc_array(PLX_NUM_OFFSETS,
					       sizeof(*xdev->irq_info.cb_list),
					       GFP_KERNEL);
	if (!xdev->irq_info.cb_list)
		return -ENOMEM;

	for (i = 0; i < PLX_NUM_OFFSETS; i++)
		INIT_LIST_HEAD(&xdev->irq_info.cb_list[i]);
	ida_init(&xdev->irq_info.cb_ida);
	spin_lock_init(&xdev->irq_info.plx_intr_lock);
	spin_lock_init(&xdev->irq_info.plx_thread_lock);
	return 0;
}

/**
 * plx_release_callbacks - Uninitialize data structures needed
 * to handle callbacks.
 *
 * @xdev: pointer to plx_device instance
 */
static void plx_release_callbacks(struct plx_device *xdev)
{
	unsigned long flags;
	struct list_head *pos, *tmp;
	struct plx_intr_cb *intr_cb;
	int i;

	spin_lock(&xdev->irq_info.plx_thread_lock);
	spin_lock_irqsave(&xdev->irq_info.plx_intr_lock, flags);
	for (i = 0; i < PLX_NUM_OFFSETS; i++) {
		if (list_empty(&xdev->irq_info.cb_list[i]))
			break;

		list_for_each_safe(pos, tmp, &xdev->irq_info.cb_list[i]) {
			intr_cb = list_entry(pos, struct plx_intr_cb, list);
			list_del(pos);
			ida_simple_remove(&xdev->irq_info.cb_ida,
					  intr_cb->cb_id);
			kfree(intr_cb);
		}
	}
	spin_unlock_irqrestore(&xdev->irq_info.plx_intr_lock, flags);
	spin_unlock(&xdev->irq_info.plx_thread_lock);
	ida_destroy(&xdev->irq_info.cb_ida);
	kfree(xdev->irq_info.cb_list);
}

/**
 * plx_setup_msi - Initializes MSI interrupts.
 *
 * @xdev: pointer to plx_device instance
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int plx_setup_msi(struct plx_device *xdev, struct pci_dev *pdev)
{
	int rc;

	rc = pci_enable_msi(pdev);
	if (rc) {
		dev_dbg(&pdev->dev, "Error enabling MSI. rc = %d\n", rc);
		return rc;
	}

	xdev->irq_info.plx_msi_map = kzalloc(sizeof(u32), GFP_KERNEL);

	if (!xdev->irq_info.plx_msi_map) {
		rc = -ENOMEM;
		goto err_nomem1;
	}

	rc = plx_setup_callbacks(xdev);
	if (rc) {
		dev_err(&pdev->dev, "Error setting up callbacks\n");
		goto err_nomem2;
	}

	rc = request_threaded_irq(pdev->irq, plx_interrupt, plx_thread_fn,
				  0, "plx-msi", xdev);
	if (rc) {
		dev_err(&pdev->dev, "Error allocating MSI interrupt\n");
		goto err_irq_req_fail;
	}

	dev_dbg(&pdev->dev, "MSI irq setup\n");
	return 0;
err_irq_req_fail:
	plx_release_callbacks(xdev);
err_nomem2:
	kfree(xdev->irq_info.plx_msi_map);
err_nomem1:
	pci_disable_msi(pdev);
	return rc;
}

/**
 * plx_setup_intx - Initializes legacy interrupts.
 *
 * @xdev: pointer to plx_device instance
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int plx_setup_intx(struct plx_device *xdev, struct pci_dev *pdev)
{
	int rc;

	/* Enable intx */
	pci_intx(pdev, 1);
	rc = plx_setup_callbacks(xdev);
	if (rc) {
		dev_err(&pdev->dev, "Error setting up callbacks\n");
		goto err_nomem;
	}

	rc = request_threaded_irq(pdev->irq, plx_interrupt, plx_thread_fn,
				  IRQF_SHARED, "plx-intx", xdev);
	if (rc)
		goto err;

	rc = plx_irq_set_affinity_node_hint(pdev);

        if (rc)
            goto err;

	dev_dbg(&pdev->dev, "intx irq setup\n");
	return 0;
err:
	plx_release_callbacks(xdev);
err_nomem:
	return rc;
}

/**
 * plx_next_db - Retrieve the next doorbell interrupt source id.
 * The id is picked sequentially from the available pool of
 * doorlbell ids.
 *
 * @xdev: pointer to the plx_device instance.
 *
 * Returns the next doorbell interrupt source.
 */
int plx_next_db(struct plx_device *xdev)
{
	int next_db;
	/* doorbell 0 is used for leveraged boot protocol */
	next_db =
		(xdev->irq_info.next_avail_src % (xdev->intr_info->intr_len -1 )) + 1;
	xdev->irq_info.next_avail_src++;
	return next_db;
}

#define COOKIE_ID_SHIFT 16
#define GET_ENTRY(cookie) ((cookie) & 0xFFFF)
#define GET_OFFSET(cookie) ((cookie) >> COOKIE_ID_SHIFT)

/**
 * plx_request_threaded_irq - request an irq. plx_mutex needs
 * to be held before calling this function.
 *
 * @xdev: pointer to plx_device instance
 * @handler: The callback function that handles the interrupt.
 * The function needs to call ack_interrupts
 * (plx_ack_interrupt(xdev)) when handling the interrupts.
 * @thread_fn: thread fn required by request_threaded_irq.
 * @name: The ASCII name of the callee requesting the irq.
 * @data: private data that is returned back when calling the
 * function handler.
 * @intr_src: The source id of the requester. Its the doorbell id
 * for Doorbell interrupts and DMA channel id for DMA interrupts.
 *
 * returns: The cookie that is transparent to the caller. Passed
 * back when calling plx_free_irq. An appropriate error code
 * is returned on failure. Caller needs to use IS_ERR(return_val)
 * to check for failure and PTR_ERR(return_val) to obtained the
 * error code.
 *
 */
struct vca_irq *
plx_request_threaded_irq(struct plx_device *xdev,
			 irq_handler_t handler, irq_handler_t thread_fn,
			 const char *name, void *data, int intr_src)
{
	u16 offset;
	int cb_id = 0;
	unsigned long cookie = 0;
	u16 entry;
	struct pci_dev *pdev = container_of(&xdev->pdev->dev,
		struct pci_dev, dev);

	offset = plx_map_src_to_offset(xdev, intr_src);
	if (offset >= PLX_NUM_OFFSETS) {
		dev_err(&xdev->pdev->dev,
			"Error mapping index %d to a valid source id.\n",
			intr_src);
		return ERR_PTR(-EINVAL);
	}

	cb_id = plx_register_intr_callback(xdev, offset, handler,
					     thread_fn, name, data);
	if (cb_id >= 0) {
		entry = 0;
		if (pci_dev_msi_enabled(pdev))
			xdev->irq_info.plx_msi_map[entry] |= (1 << offset);
		cookie = entry | cb_id << COOKIE_ID_SHIFT;
		dev_dbg(&xdev->pdev->dev, "callback %d registered for src: %d\n", cb_id, intr_src);
		return (struct vca_irq *)cookie;
	}
	return ERR_PTR(cb_id);
}

/**
 * plx_free_irq - free irq. plx_mutex
 *  needs to be held before calling this function.
 *
 * @xdev: pointer to plx_device instance
 * @cookie: cookie obtained during a successful call to plx_request_threaded_irq
 * @data: private data specified by the calling function during the
 * plx_request_threaded_irq
 *
 * returns: none.
 */
void plx_free_irq(struct plx_device *xdev,
		  struct vca_irq *cookie, void *data)
{
	u32 offset;
	u32 entry;
	u8 src_id;
	unsigned int irq;
	struct pci_dev *pdev = container_of(&xdev->pdev->dev,
		struct pci_dev, dev);

	entry = GET_ENTRY((unsigned long)cookie);
	offset = GET_OFFSET((unsigned long)cookie);
	irq = pdev->irq;
	src_id = plx_unregister_intr_callback(xdev, offset);
	if (src_id >= PLX_NUM_OFFSETS) {
		dev_warn(&xdev->pdev->dev, "Error unregistering callback\n");
		return;
	}
	if (pci_dev_msi_enabled(pdev))
		xdev->irq_info.plx_msi_map[entry] &= ~(BIT(src_id));
	dev_dbg(&xdev->pdev->dev, "callback %d unregistered for src: %d\n",
		offset, src_id);
}

/**
 * plx_setup_interrupts - Initializes interrupts.
 *
 * @xdev: pointer to plx_device instance
 * @pdev: PCI device structure
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int plx_setup_interrupts(struct plx_device *xdev, struct pci_dev *pdev)
{
	int rc;

	/*
	 * Disable MSI for now as it is resulting in hangs while running
	 * SCIF unit tests.
	 */
	goto intx;
	rc = plx_setup_msi(xdev, pdev);
	if (!rc)
		goto done;
intx:
	rc = plx_setup_intx(xdev, pdev);
	if (rc) {
		dev_err(&xdev->pdev->dev, "no usable interrupts\n");
		return rc;
	}
done:
	plx_enable_interrupts(xdev);
	return 0;
}

/**
 * plx_free_interrupts - Frees interrupts setup by plx_setup_interrupts
 *
 * @xdev: pointer to plx_device instance
 * @pdev: PCI device structure
 *
 * returns none.
 */
void plx_free_interrupts(struct plx_device *xdev, struct pci_dev *pdev)
{
	int rc;

	plx_disable_interrupts(xdev);
	if (pci_dev_msi_enabled(pdev)) {
		free_irq(pdev->irq, xdev);
		kfree(xdev->irq_info.plx_msi_map);
		pci_disable_msi(pdev);
	} else {
		rc = plx_irq_clean_affinity_node_hint (pdev);
		if (rc)
			dev_err(&xdev->pdev->dev, "free irq affinity error: %i\n", rc);
		free_irq(pdev->irq, xdev);
	}
	plx_release_callbacks(xdev);
}
