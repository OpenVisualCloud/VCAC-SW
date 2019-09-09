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
 * Implements ALM(A-LUT Manager).
 *
 */

#include "plx_alm.h"

#include<linux/slab.h>
#include<linux/pci.h>

/* Not use in release version, from performance reasons. */
//#define DEBUG_ALM_CHECK

#ifdef DEBUG_ALM_CHECK
#define PLX_ALM_CHECK(alm, pdev) plx_alm_check(alm, pdev)
#else /* DEBUG_ALM_VALIDATE */
#define PLX_ALM_CHECK(alm, pdev)
#endif /* DEBUG_ALM_VALIDATE */

#define ALUT_UNMAP_LAZY

/*
 * plx_alm_entries_dmesg - print to dmesg output from check A-LUT entries.
 *
 * @alm: pointer to plx_a_lut_manager instance
 * @pdev: The PCIe device
 * @begin: first entry
 * @begin: last entry - 1
 *
 * RETURNS: nothing
 */
static void plx_alm_entries_dmesg(struct plx_alm* alm, struct pci_dev* pdev,
				    int begin, int num)
{
	unsigned int i;
	if (begin < 0) {
		begin = 0;
	}

	if (begin + num > alm->segments_num) {
		num = alm->segments_num - begin;
	}

	for (i = 0; i < num; i++) {
		int id = i + begin;
		dev_err(&pdev->dev,"[%02x] to:%016llx ref_cnt:%x start:%x segments_count:%x\n",
			/* ID */                id,
			/* to */                alm->entries[id].value,
			/* ref_cnt*/            alm->entries[id].counter,
			/* start */             alm->entries[id].start,
			/* segments_count */    alm->entries[id].segments_count);
	}
}

/*
 * plx_alm_check - check A-LUT entries cohesion, use in debug check.
 *
 * @mngr: pointer to plx_a_lut_manager instance
 * @pdev: The PCIe device
 *
 * RETURNS: not 0 if error
 */
static inline int plx_alm_check(struct plx_alm* alm, struct pci_dev* pdev)
{
	int err = 0;
	u32 i = 0;
	u32 j;
	u16 segments;
	while(i < alm->segments_num) {
		if (alm->entries[i].counter == 0) {
			/* Check empty entry */
			if (alm->entries[i].segments_count != 0 ||
					alm->entries[i].start != 0 || alm->entries[i].value != 0) {
				dev_err(&pdev->dev, "A-LUT: Invalid segment: [%02x]\n", i);
				plx_alm_entries_dmesg(alm, pdev, i, 1);
				err = -EINVAL;
			}
			++i;
			continue;
		}

		/* alm->entries[i].counter != 0 */
		/* Check group */
		segments = alm->entries[i].segments_count;

		if (segments == 0) {
			dev_err(&pdev->dev, "A-LUT: Invalid segment zero: [%02x]\n", i);
			plx_alm_entries_dmesg(alm, pdev, i, 1);
			err = -EINVAL;
			++i;
			continue;
		}

		if (segments + i > alm->segments_num) {
			dev_err(&pdev->dev, "A-LUT: Invalid segment too big: [%02x]\n", i);
			plx_alm_entries_dmesg(alm, pdev, i, segments);
			err = -EINVAL;
			++i;
			continue;
		}

		for(j = i; j < i + segments;++j) {
			if (alm->entries[j].start != i ||
				((j != i) && ( alm->entries[j].counter != 0 ||
						alm->entries[j].segments_count != 0))) {
				dev_err(&pdev->dev, "A-LUT: Detect Invalid block parent "
						"%02x: id: [%02x]\n", i, j);
				plx_alm_entries_dmesg(alm, pdev, i, segments);
				err = -EINVAL;
				break;
			}
		}
		i += segments;
	}
	BUG_ON(err != 0);
	return err;
}

/*
 * plx_alm_init - Initialize a plx_a_lut_manager instance
	(ie. allocates memory etc.)
 *
 * @mngr: pointer to plx_a_lut_manager instance
 * @pdev: The PCIe device
 * @num_ntbs: used to compute number of a-lut segments
 * @aper_len: used to compute size of a signel a-lut segment
 *
 * RETURNS: 0 on success and -ENOMEM on failure
 */
int plx_alm_init(struct plx_alm* mngr, 
		struct pci_dev *pdev, int segments_num, u64 aper_len)
{
	int rc = 0;

	mngr->segments_num = segments_num;
	mngr->segment_size = aper_len / mngr->segments_num;

	mngr->entries = (struct plx_alm_arr_entry*)kzalloc(
		sizeof(struct plx_alm_arr_entry) * mngr->segments_num, 0);
	
	if (!mngr->entries) {
		dev_info(&pdev->dev, "Could not allocate memory for a_lut_array!\n");
		rc = -ENOMEM;
	}

	dev_info(&pdev->dev,
		"A-LUT manager initialized!"
		"aper len: %llx, segments_num: %x, segment_size: %llx\n",
		aper_len, mngr->segments_num, mngr->segment_size);

#ifdef DEBUG_ALM_CHECK
	dev_err(&pdev->dev, "A-LUT manager DEBUG MODE: DEBUG_ALM_CHECK!\n");
#endif

	plx_alm_check(mngr, pdev);
	return rc;
}

/*
 * plx_alm_release - releases plx_a_lut_manager instance
 *
 * @mngr: pointer to plx_a_lut_manager instance
 *
 * RETURNS: nothing
 */
void plx_alm_release(struct plx_alm* mngr, struct pci_dev *pdev)
{
	plx_alm_check(mngr, pdev);
	kfree(mngr->entries);
	mngr->segments_num = 0;
	mngr->segment_size = 0;
}

/*
 * clear_entries - clear multiple entries in A-LUT array
 *
 * @records: pointer to A-LUT array
 * @id_begin: first entry to be cleared
 * @num: number entries to be cleared
 *
 * RETURNS: nothing
 */
static inline void clear_entries(struct plx_alm_arr_entry * entries,
					    int id_begin, u32 num)
{
	memset(entries + id_begin, 0, sizeof(*entries) * num);
}

/*
 * plx_alm_reset - clear A-LUT entries
 *
 * @mngr: pointer to plx_a_lut_manager instance
 *
 * RETURNS: nothing
 */
void plx_alm_reset(struct plx_alm* mngr, struct pci_dev *pdev)
{
	plx_alm_check(mngr, pdev);
	clear_entries(mngr->entries, 0, mngr->segments_num);
}

#ifdef ALUT_UNMAP_LAZY
/**
 * plx_alm_cleanup_unused_entry() - clean entry in A-LUT array if not used
 * @mngr: pointer to plx_a_lut_manager instance
 * @pdev: The PCIe device
 *
 * Work only with ALUT_UNMAP_LAZY, Cleanup unused entries to
 * prepare places for new allocations.
 *
 * RETURNS: number of released entries.
 *
 * */
static int plx_alm_cleanup_unused_entry(struct plx_alm* mngr,
		struct pci_dev* pdev)
{
	u32 nums = 0;
	u32 i = 0;
	u32 ignore;

	dev_dbg(&pdev->dev, "Cleanup A-LUT segments\n");
	while (i < mngr->segments_num) {
		/* With alut lazy for counter 1 entry is not used*/
		if (mngr->entries[i].counter == 1) {
			plx_alm_del_entry(mngr, pdev, i, &ignore, &ignore);
			++nums;
		}
		++i;
	}
	return nums;
}
#endif /* ALUT_UNMAP_LAZY */

/**
 * plx_alm_add_entry() - add entry to A-LUT array
 * @mngr: pointer to plx_a_lut_manager instance
 * @pdev: The PCIe device
 * @addr: DMA address to access memory
 * @size: size of allocation
 *
 * This function allows other side of NTB to access address @addr by adding
 * a lookup entry to A LUT array.
 *
 * RETURNS: 
 * @out_segment_id: id of a segment in A-LUT array this
	allocation is to be written
 * @out_segments_num: number of continous segments to be written in A-LUT array
 *
 * */
int plx_alm_add_entry(struct plx_alm* mngr,
		struct pci_dev* pdev, dma_addr_t addr, size_t size,
		u32 *out_segment_id, u32 * out_segments_num)
{
	int i = 0;
	int rc = 0;

	u64 translation_mask = (u64)mngr->segment_size - 1;
	dma_addr_t addr_masked = addr & ~translation_mask;
	u64 offset_in_segment = addr & translation_mask;
	u32 segments = (u32)DIV_ROUND_UP(offset_in_segment + size,
		mngr->segment_size);
	u32 free_slots = 0;
	u32 match = -1;
	u64 idx = 0;

	PLX_ALM_CHECK(mngr, pdev);

	if (!size || size > mngr->segment_size * mngr->segments_num) {
		dev_err(&pdev->dev, "request for allocation %s\n",
			!size ? "with zero size" : "too big for translation");
		WARN_ONCE(!size, "%s invalid size==0 requested\n", __func__);
		rc = -ENOMEM;
		goto finish;
	}

	while (i < mngr->segments_num) {
		idx = 0;

		/* check if there is enough of free consecutive blocks
		 * to store this mapping */
		if (mngr->entries[i].counter == 0) {
			if (match == -1) {
				free_slots++;
				if (free_slots == segments)
					match = i - segments + 1;
			}
			i++;
		}
		else {
			free_slots = 0;

			/* we're in the first block of some mapping - check
			 * if new mapping can be embedded into this one */
			if(addr_masked >= mngr->entries[i].value)
			{
				idx = (addr_masked - mngr->entries[i].value)
					  / mngr->segment_size;
				if (idx < mngr->segments_num &&
				    idx + segments <=
					mngr->entries[i].segments_count) {
					match = i;
					break;
				}
				else {
					idx = 0;
				}
			}

			i += mngr->entries[i].segments_count;
		}
	}

	if (match != -1) {
		if (mngr->entries[match].counter == 0) {
			mngr->entries[match].value = addr_masked;
			mngr->entries[match].segments_count = segments;
			for(i=0; i<segments; i++)
				mngr->entries[match + i].start = match;
		}
		else {
			rc = -EEXIST;
		}

#ifdef ALUT_UNMAP_LAZY
		if (!mngr->entries[match].counter) {
			/* For lazy unmap, counter 1 is keep to alut not used,
			 * and can be cleanup.
			 * For first used allocation increment counter to 2. */
			mngr->entries[match].counter = 1;
		}
#endif /* ALUT_UNMAP_LAZY */
		mngr->entries[match].counter++;

	} else {

#ifdef ALUT_UNMAP_LAZY
		if (plx_alm_cleanup_unused_entry(mngr, pdev)) {
			return plx_alm_add_entry(mngr, pdev, addr, size, out_segment_id,
					out_segments_num);
		}
#endif /* ALUT_UNMAP_LAZY */

		dev_err(&pdev->dev, "out of A-LUT segments\n");
		rc = -ENOMEM;
		goto finish;
	}

	*out_segment_id = (u32)match + (u32)idx;
	*out_segments_num = segments;

finish:
	PLX_ALM_CHECK(mngr, pdev);
	return rc;
}

/**
 * plx_alm_del_entry() - delete entry from A-LUT array
 * @plx_a_lut_manager: pointer to plx_device instance
 * @pdev: The PCIe device
 * @segment_id: id of an A-LUT segment
 *
 * RETURNS:
 * @out_segment_id: id of a starting segment,
 from which we need to start clearing
 * @out_segments_num: number of continous segments to be written in A-LUT array
 *
 * */
void plx_alm_del_entry(struct plx_alm* mngr, struct pci_dev* pdev, u32 segment_id,
					   u32 * out_segment_id, u32 * out_segments_num)
{
	u32 start_id = mngr->entries[segment_id].start;

	PLX_ALM_CHECK(mngr, pdev);
	BUG_ON(mngr->entries[start_id].counter == 0);

	mngr->entries[start_id].counter--;

	if (mngr->entries[start_id].counter == 0) {
		u32 segments = mngr->entries[start_id].segments_count;

		*out_segment_id = start_id;
		*out_segments_num = segments;
		clear_entries(mngr->entries, start_id, segments);
	}
	PLX_ALM_CHECK(mngr, pdev);
}
