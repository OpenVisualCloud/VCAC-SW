/*
 * Intel VCA Software Stack (VCASS)
 *
 * Copyright(c) 2016-2017 Intel Corporation.
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
#ifndef __VCA_IOCTL_IMPL_H_
#define __VCA_IOCTL_IMPL_H_

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>

/* Some VCA ioctls send data via pointer to memory where header is followed by
 * variable length data buffer. This macro alocates temporary buffer for use
 * in kernel and copies user data to this temporary buffer. Caller is responsible
 * for freeing the allocation. In case of error, error pointer is returned.
 *
 * This macro is designed to work with user data @usr_ptr in format of pointer to
 * structure. This structure is expected to conform to below requirements:
 * 	* type of *usr_ptr is IOCTL header
 * 	* header structure contains field
 * 		size_t buf_size
 * 	  storing size of IOCTL data buffer
 * 	* By convention the last field of header structure is
 * 	         char buf[];
 * 	  to indicate that ...
 * 	* Header immediatelly followed by data buffer of size buf_size
 *
 * So the layout of memory pointed with @usr_ptr goes like this:
 * {
 * 	field_1;
 * 	...
 * 	size_t buf_size;
 * 	...
 * 	field_n;
 * 	char buf[buf_size]; // array size is determined in runtime
 * }
 *
 * @usr_ptr: IN IOCTL argument (userspace pointer to IOCTL data structure)
 * @max_buf_size: IN maximum allowed size of data
 * @input_buf_size: IN how much data to copy from user b
 * @total_size: OUT size of IOCTL user data memory (header + buffer)
 *
 * */
#define vca_get_ioctl_data(usr_ptr, max_buf_size, input_buf_size, total_size) \
	(sizeof(usr_ptr->buf_size) == sizeof(size_t)) \
		? _vca_get_ioctl_data(usr_ptr, &usr_ptr->buf_size, sizeof(*usr_ptr), \
			max_buf_size, input_buf_size, total_size) \
		: ERR_PTR(-EINVAL)

/*
 * _vca_get_ioctl_data
 *
 * Implementation of vca_get_ioctl_data macro. Not to be used directly, call vca_get_ioctl_data
 * instead
 *
 * @argp: IN maximum allowed size of data buffer
 * @buf_size_usr: IN pointer to buffer size field in argp pointer
 * @header_size: IN size ofheader (preceding the data buffer)
 * @max_buf_size: IN maximum allowed size of data buffer
 * @input_buf_size: IN how much data to copy from user buffer to kernel (maximum copy size, actual
 *			copy might be smaller in case input buffer is short)
 * @total_size: OUT returned size of whole memory (data + buffer)
 *
 * */
static void*  _vca_get_ioctl_data(void __user *argp, size_t __user *buf_size_usr, size_t header_size,
	size_t max_buf_size, size_t input_buf_size, size_t *total_size)
{
	size_t buf_size;
	size_t copy_size;
	void *data;
	bool accessible;

	if (copy_from_user(&buf_size, buf_size_usr, sizeof(buf_size)))
		return ERR_PTR(-EFAULT);

	if (buf_size > max_buf_size)
		return ERR_PTR(-E2BIG);

	if (input_buf_size > max_buf_size) {
		pr_err("invalid input buffer size\n");
		return ERR_PTR(-E2BIG);
	}

	*total_size = header_size + buf_size;
	copy_size =  header_size + min(input_buf_size, buf_size);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
	accessible = access_ok(argp, *total_size);
#else
	accessible = access_ok(VERIFY_WRITE, argp, *total_size);
#endif
	if (!accessible) {
		pr_err("access verification failed\n");
		return ERR_PTR(-EFAULT);
	}

	data = kzalloc(*total_size, GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);

	if (copy_from_user(data, argp, copy_size)) {
		kfree(data);
		return ERR_PTR(-EFAULT);
	}

	return data;
}
#endif


