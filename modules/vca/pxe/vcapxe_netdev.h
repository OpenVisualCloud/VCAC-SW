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

#ifndef _VCAPXE_NETDEV_H_
#define _VCAPXE_NETDEV_H_

int vcapxe_prepare_netdev(struct net_device* netdev);

int vcapxe_enable_netdev(struct vcapxe_device *dev);
int vcapxe_disable_netdev(struct vcapxe_device *dev);
int vcapxe_query_netdev(struct vcapxe_device *dev);

void vcapxe_start_netdev(struct vcapxe_device *dev, void *shared);
void vcapxe_stop_netdev(struct vcapxe_device *dev);

irqreturn_t vcapxe_doorbell_irq(int irq, void *data);

void vcapxe_teardown_netdev(struct vcapxe_device *dev);

#endif /* _VCAPXE_NETDEV_H_ */
