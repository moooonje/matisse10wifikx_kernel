/*
 *  Copyright (C) 2012-2013 Samsung Electronics Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301, USA.
 */

/************************************************************************/
/*                                                                      */
/*  PROJECT : exFAT & FAT12/16/32 File System                           */
/*  FILE    : core_exfat.c                                              */
/*  PURPOSE : exFAT-fs core code for sdFAT                              */
/*                                                                      */
/*----------------------------------------------------------------------*/
/*  NOTES                                                               */
/*                                                                      */
/*                                                                      */
/************************************************************************/

#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/log2.h>

#include "sdfat.h"
#include "core.h"
#include <asm/byteorder.h>
#include <asm/unaligned.h>

/*----------------------------------------------------------------------*/
/*  Constant & Macro Definitions                                        */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Global Variable Definitions                                         */
/*----------------------------------------------------------------------*/

/*----------------------------------------------------------------------*/
/*  Local Variable Definitions                                          */
/*----------------------------------------------------------------------*/
static u8 free_bit[] = {
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2,/*  0 ~  19*/
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3,/* 20 ~  39*/
	0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2,/* 40 ~  59*/
	0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,/* 60 ~  79*/
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2,/* 80 ~  99*/
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3,/*100 ~ 119*/
	0, 1, 0, 2, 0, 1, 0, 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2,/*120 ~ 139*/
	0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,/*140 ~ 159*/
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2,/*160 ~ 179*/
	0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0, 3,/*180 ~ 199*/
	0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2,/*200 ~ 219*/
	0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,/*220 ~ 239*/
	0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0                /*240 ~ 254*/
};

static u8 used_bit[] = {
	0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4, 1, 2, 2, 3,/*  0 ~  19*/
	2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5, 1, 2, 2, 3, 2, 3, 3, 4,/* 20 ~  39*/
	2, 3, 3, 4, 3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5,/* 40 ~  59*/
	4, 5, 5, 6, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,/* 60 ~  79*/
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 2, 3, 3, 4,/* 80 ~  99*/
	3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6,/*100 ~ 119*/
	4, 5, 5, 6, 5, 6, 6, 7, 1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4,/*120 ~ 139*/
	3, 4, 4, 5, 2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,/*140 ~ 159*/
	2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5,/*160 ~ 179*/
	4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7, 2, 3, 3, 4, 3, 4, 4, 5,/*180 ~ 199*/
	3, 4, 4, 5, 4, 5, 5, 6, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6,/*200 ~ 219*/
	5, 6, 6, 7, 3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,/*220 ~ 239*/
	4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8             /*240 ~ 255*/
};


/*======================================================================*/
/*  Local Function Definitions                                          */
/*======================================================================*/
/*
 *  Directory Entry Management Functions
 */
static u32 exfat_get_entry_type(DENTRY_T *p_entry)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	if (ep->type == EXFAT_UNUSED) {
		return TYPE_UNUSED;
	} else if (ep->type < 0x80) {
		return TYPE_DELETED;
	} else if (ep->type == 0x80) {
		return TYPE_INVALID;
	} else if (ep->type < 0xA0) {
		if (ep->type == 0x81) {
			return TYPE_BITMAP;
		} else if (ep->type == 0x82) {
			return TYPE_UPCASE;
		} else if (ep->type == 0x83) {
			return TYPE_VOLUME;
		} else if (ep->type == 0x85) {
			if (le16_to_cpu(ep->attr) & ATTR_SUBDIR)
				return TYPE_DIR;
			else
				return TYPE_FILE;
		}
		return TYPE_CRITICAL_PRI;
	} else if (ep->type < 0xC0) {
		if (ep->type == 0xA0) {
			return TYPE_GUID;
		} else if (ep->type == 0xA1) {
			return TYPE_PADDING;
		} else if (ep->type == 0xA2) {
			return TYPE_ACLTAB;
		}
		return TYPE_BENIGN_PRI;
	} else if (ep->type < 0xE0) {
		if (ep->type == 0xC0) {
			return TYPE_STREAM;
		} else if (ep->type == 0xC1) {
			return TYPE_EXTEND;
		} else if (ep->type == 0xC2) {
			return TYPE_ACL;
		}
		return TYPE_CRITICAL_SEC;
	}

	return TYPE_BENIGN_SEC;
} /* end of exfat_get_entry_type */

static void exfat_set_entry_type(DENTRY_T *p_entry, u32 type)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	if (type == TYPE_UNUSED) {
		ep->type = 0x0;
	} else if (type == TYPE_DELETED) {
		ep->type &= ~0x80;
	} else if (type == TYPE_STREAM) {
		ep->type = 0xC0;
	} else if (type == TYPE_EXTEND) {
		ep->type = 0xC1;
	} else if (type == TYPE_BITMAP) {
		ep->type = 0x81;
	} else if (type == TYPE_UPCASE) {
		ep->type = 0x82;
	} else if (type == TYPE_VOLUME) {
		ep->type = 0x83;
	} else if (type == TYPE_DIR) {
		ep->type = 0x85;
		ep->attr = cpu_to_le16(ATTR_SUBDIR);
	} else if (type == TYPE_FILE) {
		ep->type = 0x85;
		ep->attr = cpu_to_le16(ATTR_ARCHIVE);
	} else if (type == TYPE_SYMLINK) {
		ep->type = 0x85;
		ep->attr = cpu_to_le16(ATTR_ARCHIVE | ATTR_SYMLINK);
	}
} /* end of exfat_set_entry_type */

static u32 exfat_get_entry_attr(DENTRY_T *p_entry)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;
	return((u32) le16_to_cpu(ep->attr));
} /* end of exfat_get_entry_attr */

static void exfat_set_entry_attr(DENTRY_T *p_entry, u32 attr)
{
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;
	ep->attr = cpu_to_le16((u16) attr);
} /* end of exfat_set_entry_attr */

static u8 exfat_get_entry_flag(DENTRY_T *p_entry)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	return(ep->flags);
} /* end of exfat_get_entry_flag */

static void exfat_set_entry_flag(DENTRY_T *p_entry, u8 flags)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	ep->flags = flags;
} /* end of exfat_set_entry_flag */

static u32 exfat_get_entry_clu0(DENTRY_T *p_entry)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	return le32_to_cpu(ep->start_clu);
} /* end of exfat_get_entry_clu0 */

static void exfat_set_entry_clu0(DENTRY_T *p_entry, u32 start_clu)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	ep->start_clu = cpu_to_le32(start_clu);
} /* end of exfat_set_entry_clu0 */

static u64 exfat_get_entry_size(DENTRY_T *p_entry)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	return le64_to_cpu(ep->valid_size);
} /* end of exfat_get_entry_size */

static void exfat_set_entry_size(DENTRY_T *p_entry, u64 size)
{
	STRM_DENTRY_T *ep = (STRM_DENTRY_T *) p_entry;
	ep->valid_size = cpu_to_le64(size);
	ep->size = cpu_to_le64(size);
} /* end of exfat_set_entry_size */

static void exfat_get_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, u8 mode)
{
	u16 t = 0x00, d = 0x21;
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	switch (mode) {
	case TM_CREATE:
		t = le16_to_cpu(ep->create_time);
		d = le16_to_cpu(ep->create_date);
		break;
	case TM_MODIFY:
		t = le16_to_cpu(ep->modify_time);
		d = le16_to_cpu(ep->modify_date);
		break;
	case TM_ACCESS:
		t = le16_to_cpu(ep->access_time);
		d = le16_to_cpu(ep->access_date);
		break;
	}

	tp->sec  = (t & 0x001F) << 1;
	tp->min  = (t >> 5) & 0x003F;
	tp->hour = (t >> 11);
	tp->day  = (d & 0x001F);
	tp->mon  = (d >> 5) & 0x000F;
	tp->year = (d >> 9);
} /* end of exfat_get_entry_time */

static void exfat_set_entry_time(DENTRY_T *p_entry, TIMESTAMP_T *tp, u8 mode)
{
	u16 t, d;
	FILE_DENTRY_T *ep = (FILE_DENTRY_T *) p_entry;

	t = (tp->hour << 11) | (tp->min << 5) | (tp->sec >> 1);
	d = (tp->year <<  9) | (tp->mon << 5) |  tp->day;

	switch (mode) {
	case TM_CREATE:
		ep->create_time = cpu_to_le16(t);
		ep->create_date = cpu_to_le16(d);
		break;
	case TM_MODIFY:
		ep->modify_time = cpu_to_le16(t);
		ep->modify_date = cpu_to_le16(d);
		break;
	case TM_ACCESS:
		ep->access_time = cpu_to_le16(t);
		ep->access_date = cpu_to_le16(d);
		break;
	}
} /* end of exfat_set_entry_time */


static void __init_file_entry(struct super_block *sb, FILE_DENTRY_T *ep, u32 type)
{
	TIMESTAMP_T tm, *tp;

	exfat_set_entry_type((DENTRY_T *) ep, type);

	tp = tm_now(SDFAT_SB(sb), &tm);
	exfat_set_entry_time((DENTRY_T *) ep, tp, TM_CREATE);
	exfat_set_entry_time((DENTRY_T *) ep, tp, TM_MODIFY);
	exfat_set_entry_time((DENTRY_T *) ep, tp, TM_ACCESS);
	ep->create_time_ms = 0;
	ep->modify_time_ms = 0;
	ep->access_time_ms = 0;
} /* end of __init_file_entry */

static void __init_strm_entry(STRM_DENTRY_T *ep, u8 flags, u32 start_clu, u64 size)
{
	exfat_set_entry_type((DENTRY_T *) ep, TYPE_STREAM);
	ep->flags = flags;
	ep->start_clu = cpu_to_le32(start_clu);
	ep->valid_size = cpu_to_le64(size);
	ep->size = cpu_to_le64(size);
} /* end of __init_strm_entry */

static void __init_name_entry(NAME_DENTRY_T *ep, u16 *uniname)
{
	s32 i;

	exfat_set_entry_type((DENTRY_T *) ep, TYPE_EXTEND);
	ep->flags = 0x0;

	for (i = 0; i < 15; i++) {
		ep->unicode_0_14[i] = cpu_to_le16(*uniname);
		if (*uniname == 0x0)
			break;
		uniname++;
	}
} /* end of __init_name_entry */

static s32 exfat_init_dir_entry(struct super_block *sb, CHAIN_T *p_dir, s32 entry, u32 type, u32 start_clu, u64 size)
{
	u32 sector;
	u8 flags;
	FILE_DENTRY_T *file_ep;
	STRM_DENTRY_T *strm_ep;

	flags = (type == TYPE_FILE) ? 0x01 : 0x03;

	/* we cannot use get_dentry_set_in_dir here because file ep is not initialized yet */
	file_ep = (FILE_DENTRY_T *)get_dentry_in_dir(sb, p_dir, entry, &sector);
	if (!file_ep)
		return -EIO;

	strm_ep = (STRM_DENTRY_T *)get_dentry_in_dir(sb, p_dir, entry+1, &sector);
	if (!strm_ep)
		return -EIO;

	__init_file_entry(sb, file_ep, type);
	if (dcache_modify(sb, sector))
		return -EIO;

	__init_strm_entry(strm_ep, flags, start_clu, size);
	if (dcache_modify(sb, sector))
		return -EIO;

	return 0;
} /* end of exfat_init_dir_entry */

s32 update_dir_chksum(struct super_block *sb, CHAIN_T *p_dir, s32 entry)
{
	s32 ret = -EIO;
	s32 i, num_entries;
	u32 sector;
	u16 chksum;
	FILE_DENTRY_T *file_ep;
	DENTRY_T *ep;

	file_ep = (FILE_DENTRY_T *)get_dentry_in_dir(sb, p_dir, entry, &sector);
	if (!file_ep)
		return -EIO;

	dcache_lock(sb, sector);

	num_entries = (s32) file_ep->num_ext + 1;
	chksum = calc_chksum_2byte((void *) file_ep, DENTRY_SIZE, 0, CS_DIR_ENTRY);

	for (i = 1; i < num_entries; i++) {
		ep = get_dentry_in_dir(sb, p_dir, entry+i, NULL);
		if (!ep)
			goto out_unlock;

		chksum = calc_chksum_2byte((void *) ep, DENTRY_SIZE, chksum, CS_DEFAULT);
	}

	file_ep->checksum = cpu_to_le16(chksum);
	ret = dcache_modify(sb, sector);
out_unlock:
	dcache_unlock(sb, sector);
	return ret;

} /* end of update_dir_chksum */


static s32 exfat_init_ext_entry(struct super_block *sb, CHAIN_T *p_dir, s32 entry, s32 num_entries,
						   UNI_NAME_T *p_uniname, DOS_NAME_T *p_dosname)
{
	s32 i;
	u32 sector;
	u16 *uniname = p_uniname->name;
	FILE_DENTRY_T *file_ep;
	STRM_DENTRY_T *strm_ep;
	NAME_DENTRY_T *name_ep;

	file_ep = (FILE_DENTRY_T *)get_dentry_in_dir(sb, p_dir, entry, &sector);
	if (!file_ep)
		return -EIO;

	file_ep->num_ext = (u8)(num_entries - 1);
	dcache_modify(sb, sector);

	strm_ep = (STRM_DENTRY_T *)get_dentry_in_dir(sb, p_dir, entry+1, &sector);
	if (!strm_ep)
		return -EIO;

	strm_ep->name_len = p_uniname->name_len;
	strm_ep->name_hash = cpu_to_le16(p_uniname->name_hash);
	dcache_modify(sb, sector);

	for (i = 2; i < num_entries; i++) {
		name_ep = (NAME_DENTRY_T *)get_dentry_in_dir(sb, p_dir, entry+i, &sector);
		if (!name_ep)
			return -EIO;

		__init_name_entry(name_ep, uniname);
		dcache_modify(sb, sector);
		uniname += 15;
	}

	update_dir_chksum(sb, p_dir, entry);

	return 0;
} /* end of exfat_init_ext_entry */


static s32 exfat_delete_dir_entry(struct super_block *sb, CHAIN_T *p_dir, s32 entry, s32 order, s32 num_entries)
{
	s32 i;
	u32 sector;
	DENTRY_T *ep;

	for (i = order; i < num_entries; i++) {
		ep = get_dentry_in_dir(sb, p_dir, entry+i, &sector);
		if (!ep)
			return -EIO;

		exfat_set_entry_type(ep, TYPE_DELETED);
		if (dcache_modify(sb, sector))
			return -EIO;
	}

	return 0;
}

static s32 __write_partial_entries_in_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es, u32 sec, s32 off, u32 count)
{
	s32 num_entries, buf_off = (off - es->offset);
	u32 remaining_byte_in_sector, copy_entries;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);
	u32 clu;
	u8 *buf, *esbuf = (u8 *)&(es->__buf);

	TMSG("%s entered\n", __func__);
	MMSG("%s: es %p sec %u off %d cnt %d\n", __func__, es, sec, off, count);
	num_entries = count;

	while(num_entries) {
		// white per sector base
		remaining_byte_in_sector = (1 << sb->s_blocksize_bits) - off;
		copy_entries = min((s32)(remaining_byte_in_sector>> DENTRY_SIZE_BITS) , num_entries);
		buf = dcache_getblk(sb, sec);
		if (!buf)
			goto err_out;
		MMSG("es->buf %p buf_off %u\n", esbuf, buf_off);
		MMSG("copying %d entries from %p to sector %u\n", copy_entries, (esbuf + buf_off), sec);
		memcpy(buf + off, esbuf + buf_off, copy_entries << DENTRY_SIZE_BITS);
		dcache_modify(sb, sec);
		num_entries -= copy_entries;

		if (num_entries) {
			// get next sector
			if (IS_LAST_SECT_IN_CLUS(fsi, sec)) {
				clu = SECT_TO_CLUS(fsi, sec);
				if (es->alloc_flag == 0x03)
					clu++;
				else if (get_next_clus_safe(sb, &clu))
					goto err_out;
				sec = CLUS_TO_SECT(fsi, clu);
			} else {
				sec++;
			}
			off = 0;
			buf_off += copy_entries << DENTRY_SIZE_BITS;
		}
	}

	TMSG("%s exited successfully\n", __func__);
	return 0;
err_out:
	TMSG("%s failed\n", __func__);
	return -EIO;
}

/* write back all entries in entry set */
static s32 __write_whole_entry_set(struct super_block *sb, ENTRY_SET_CACHE_T *es)
{
	return __write_partial_entries_in_entry_set(sb, es, es->sector,es->offset, es->num_entries);
}

s32 update_dir_chksum_with_entry_set (struct super_block *sb, ENTRY_SET_CACHE_T *es)
{
	DENTRY_T *ep;
	u16 chksum = 0;
	s32 chksum_type = CS_DIR_ENTRY, i;

	ep = (DENTRY_T *)&(es->__buf);
	for (i=0; i < es->num_entries; i++) {
		MMSG ("%s %p\n", __func__, ep);
		chksum = calc_chksum_2byte((void *) ep, DENTRY_SIZE, chksum, chksum_type);
		ep++;
		chksum_type = CS_DEFAULT;
	}

	ep = (DENTRY_T *)&(es->__buf);
	((FILE_DENTRY_T *)ep)->checksum = cpu_to_le16(chksum);
	return __write_whole_entry_set(sb, es);
}

/* returns a set of dentries for a file or dir.
 * Note that this is a copy (dump) of dentries so that user should call write_entry_set()
 * to apply changes made in this entry set to the real device.
 * in:
 *   sb+p_dir+entry: indicates a file/dir
 *   type:  specifies how many dentries should be included.
 * out:
 *   file_ep: will point the first dentry(= file dentry) on success
 * return:
 *   pointer of entry set on success,
 *   NULL on failure.
 */

#define ES_MODE_STARTED				0
#define ES_MODE_GET_FILE_ENTRY			1
#define ES_MODE_GET_STRM_ENTRY			2
#define ES_MODE_GET_NAME_ENTRY			3
#define ES_MODE_GET_CRITICAL_SEC_ENTRY		4
ENTRY_SET_CACHE_T *get_dentry_set_in_dir (struct super_block *sb, CHAIN_T *p_dir, s32 entry, u32 type, DENTRY_T **file_ep)
{
	s32 off, ret, byte_offset;
	u32 clu=0;
	u32 sec, entry_type;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);
	ENTRY_SET_CACHE_T *es = NULL;
	DENTRY_T *ep, *pos;
	u8 *buf;
	u8 num_entries;
	s32 mode = ES_MODE_STARTED;

	/* FIXME : is available in error case? */
	if (p_dir->dir == DIR_DELETED) {
		EMSG("%s : access to deleted dentry\n", __func__);
		BUG_ON(!fsi->prev_eio);
		return NULL;
	}

	TMSG("%s entered\n", __func__);
	MMSG("p_dir dir %u flags %x size %d\n", p_dir->dir, p_dir->flags, p_dir->size);
	MMSG("entry %d type %d\n", entry, type);

	byte_offset = entry << DENTRY_SIZE_BITS;
	ret = walk_fat_chain(sb, p_dir, byte_offset, &clu);
	if (ret)
		return NULL;

	/* byte offset in cluster */
	byte_offset &= fsi->cluster_size - 1;

	/* byte offset in sector */
	off = byte_offset & (u32)(sb->s_blocksize - 1);

	/* sector offset in cluster */
	sec = byte_offset >> (sb->s_blocksize_bits);
	sec += CLUS_TO_SECT(fsi, clu);

	buf = dcache_getblk(sb, sec);
	if (!buf)
		goto err_out;

	ep = (DENTRY_T *)(buf + off);
	entry_type = exfat_get_entry_type(ep);

	if ((entry_type != TYPE_FILE)
		&& (entry_type != TYPE_DIR))
		goto err_out;

	if (type == ES_ALL_ENTRIES)
		num_entries = ((FILE_DENTRY_T *)ep)->num_ext+1;
	else
		num_entries = type;

	MMSG("trying to malloc %lx bytes for %d entries\n",
		(unsigned long)(offsetof(ENTRY_SET_CACHE_T, __buf) + (num_entries)  * sizeof(DENTRY_T)), num_entries);
	es = kmalloc((offsetof(ENTRY_SET_CACHE_T, __buf) + (num_entries)  * sizeof(DENTRY_T)), GFP_KERNEL);
	if (!es) {
		EMSG("%s: failed to alloc entryset\n", __func__);
		goto err_out;
	}

	es->num_entries = num_entries;
	es->sector = sec;
	es->offset = off;
	es->alloc_flag = p_dir->flags;

	pos = (DENTRY_T *) &(es->__buf);

	while(num_entries) {
		// instead of copying whole sector, we will check every entry.
		// this will provide minimum stablity and consistancy.
		entry_type = exfat_get_entry_type(ep);

		if ((entry_type == TYPE_UNUSED) || (entry_type == TYPE_DELETED))
			goto err_out;

		switch(mode) {
		case ES_MODE_STARTED:
			if  ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR))
				mode = ES_MODE_GET_FILE_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_FILE_ENTRY:
			if (entry_type == TYPE_STREAM)
				mode = ES_MODE_GET_STRM_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_STRM_ENTRY:
			if (entry_type == TYPE_EXTEND)
				mode = ES_MODE_GET_NAME_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_NAME_ENTRY:
			if (entry_type == TYPE_EXTEND)
				break;
			else if (entry_type == TYPE_STREAM)
				goto err_out;
			else if (entry_type & TYPE_CRITICAL_SEC)
				mode = ES_MODE_GET_CRITICAL_SEC_ENTRY;
			else
				goto err_out;
			break;
		case ES_MODE_GET_CRITICAL_SEC_ENTRY:
			if ((entry_type == TYPE_EXTEND) || (entry_type == TYPE_STREAM))
				goto err_out;
			else if ((entry_type & TYPE_CRITICAL_SEC) != TYPE_CRITICAL_SEC)
				goto err_out;
			break;
		}

		/* copy dentry */
		memcpy(pos, ep, sizeof(DENTRY_T));

		if (--num_entries == 0)
			break;

		if ( ((off + DENTRY_SIZE) & (u32)(sb->s_blocksize - 1)) <
					(off &  (u32)(sb->s_blocksize - 1)) ) {
			// get the next sector
			if (IS_LAST_SECT_IN_CLUS(fsi, sec)) {
				if (es->alloc_flag == 0x03)
					clu++;
				else if (get_next_clus_safe(sb, &clu))
					goto err_out;
				sec = CLUS_TO_SECT(fsi, clu);
			} else {
				sec++;
			}
			buf = dcache_getblk(sb, sec);
			if (!buf)
				goto err_out;
			off = 0;
			ep = (DENTRY_T *)(buf);
		} else {
			ep++;
			off += DENTRY_SIZE;
		}
		pos++;
	}

	if (file_ep)
		*file_ep = (DENTRY_T *)&(es->__buf);

	MMSG("es sec %u offset %d flags %d, num_entries %u buf ptr %p\n",
	 es->sector, es->offset, es->alloc_flag, es->num_entries, &(es->__buf));
	TMSG("%s exited %p\n", __func__, es);
	return es;
err_out:
	TMSG("%s exited (return NULL) (es %p)\n", __func__, es);
	if (es) {
		kfree(es);
		es = NULL;
	}
	return NULL;
}

void release_dentry_set (ENTRY_SET_CACHE_T *es)
{
	TMSG("%s %p\n", __func__, es);
	if (es) {
		kfree(es);
		es = NULL;
	}
}

static s32 __extract_uni_name_from_name_entry(NAME_DENTRY_T *ep, u16 *uniname, s32 order)
{
	s32 i, len = 0;

	for (i = 0; i < 15; i++) {
		/* FIXME : unaligned? */
		*uniname = le16_to_cpu(ep->unicode_0_14[i]);
		if (*uniname == 0x0)
			return(len);
		uniname++;
		len++;
	}

	*uniname = 0x0;
	return(len);

} /* end of __extract_uni_name_from_name_entry */

/* return values of exfat_find_dir_entry()
 * >= 0 : return dir entiry position with the name in dir
 * -EEXIST : (root dir, ".") it is the root dir itself
 * -ENOENT : entry with the name does not exist
 * -EIO    : I/O error
 */
static s32 exfat_find_dir_entry(struct super_block *sb, CHAIN_T *p_dir, UNI_NAME_T *p_uniname, s32 num_entries, DOS_NAME_T *unused, u32 type)
{
	s32 i, dentry = 0, num_ext_entries = 0, len;
	s32 order = 0, is_feasible_entry = false;
	s32 dentries_per_clu, num_empty = 0;
	u32 entry_type;
	u16 entry_uniname[16], *uniname = NULL, unichar;
	CHAIN_T clu;
	DENTRY_T *ep;
	FILE_DENTRY_T *file_ep;
	STRM_DENTRY_T *strm_ep;
	NAME_DENTRY_T *name_ep;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);


	/*
	 * REMARK:
	 * DOT and DOTDOT are handled by VFS layer
	 */

	if (IS_CLUS_FREE(p_dir->dir))
		return -EIO;

	dentries_per_clu = fsi->dentries_per_clu;

	clu.dir = p_dir->dir;
	clu.size = p_dir->size;
	clu.flags = p_dir->flags;

	fsi->hint_uentry.dir = p_dir->dir;
	fsi->hint_uentry.entry = -1;

	while (!IS_CLUS_EOF(clu.dir)) {

		for (i = 0; i < dentries_per_clu; i++, dentry++) {
			ep = get_dentry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -EIO;

			entry_type = exfat_get_entry_type(ep);

			if ((entry_type == TYPE_UNUSED) || (entry_type == TYPE_DELETED)) {
				is_feasible_entry = false;

				if (fsi->hint_uentry.entry == -1) {
					num_empty++;

					if (num_empty == 1) {
						fsi->hint_uentry.clu.dir = clu.dir;
						fsi->hint_uentry.clu.size = clu.size;
						fsi->hint_uentry.clu.flags = clu.flags;
					}
					if ((num_empty >= num_entries) || (entry_type == TYPE_UNUSED)) {
						fsi->hint_uentry.entry = dentry - (num_empty-1);
					}
				}

				if (entry_type == TYPE_UNUSED) {
					return -ENOENT;
				}
			} else {
				num_empty = 0;

				if ((entry_type == TYPE_FILE) || (entry_type == TYPE_DIR)) {
					if ((type == TYPE_ALL) || (type == entry_type)) {
						file_ep = (FILE_DENTRY_T *) ep;
						num_ext_entries = file_ep->num_ext;
						is_feasible_entry = true;
					} else {
						is_feasible_entry = false;
					}
				} else if (entry_type == TYPE_STREAM) {
					if (is_feasible_entry) {
						strm_ep = (STRM_DENTRY_T *) ep;
						if ((p_uniname->name_hash == le16_to_cpu(strm_ep->name_hash)) &&
							(p_uniname->name_len == strm_ep->name_len)) {
							order = 1;
						} else {
							is_feasible_entry = false;
						}
					}
				} else if (entry_type == TYPE_EXTEND) {
					if (is_feasible_entry) {
						name_ep = (NAME_DENTRY_T *) ep;

						if ((++order) == 2)
							uniname = p_uniname->name;
						else
							uniname += 15;

						len = __extract_uni_name_from_name_entry(name_ep, entry_uniname, order);

						unichar = *(uniname+len);
						*(uniname+len) = 0x0;

						if (nls_cmp_uniname(sb, uniname, entry_uniname)) {
							is_feasible_entry = false;
						} else if (order == num_ext_entries) {
							fsi->hint_uentry.dir = CLUS_EOF;
							fsi->hint_uentry.entry = -1;
							return(dentry - (num_ext_entries));
						}

						*(uniname+len) = unichar;
					}
				} else {
					is_feasible_entry = false;
				}
			}
		}

		if (clu.flags == 0x03) {
			if ((--clu.size) > 0)
				clu.dir++;
			else
				clu.dir = CLUS_EOF;
		} else {
			if (get_next_clus_safe(sb, &clu.dir))
				return -EIO;
		}
	}

	return -ENOENT;
} /* end of exfat_find_dir_entry */

/* returns -EIO on error */
static s32 exfat_count_ext_entries(struct super_block *sb, CHAIN_T *p_dir, s32 entry, DENTRY_T *p_entry)
{
	s32 i, count = 0;
	u32 type;
	FILE_DENTRY_T *file_ep = (FILE_DENTRY_T *) p_entry;
	DENTRY_T *ext_ep;

	for (i = 0, entry++; i < file_ep->num_ext; i++, entry++) {
		ext_ep = get_dentry_in_dir(sb, p_dir, entry, NULL);
		if (!ext_ep)
			return -EIO;

		type = exfat_get_entry_type(ext_ep);
		if ((type == TYPE_EXTEND) || (type == TYPE_STREAM)) {
			count++;
		} else {
			return count;
		}
	}

	return count;
} /* end of exfat_count_ext_entries */


/*
 *  Name Conversion Functions
 */
static void exfat_get_uniname_from_ext_entry(struct super_block *sb, CHAIN_T *p_dir, s32 entry, u16 *uniname)
{
	s32 i;
	DENTRY_T *ep;
	ENTRY_SET_CACHE_T *es;

	es = get_dentry_set_in_dir(sb, p_dir, entry, ES_ALL_ENTRIES, &ep);
	if (!es)
		return;

	if (es->num_entries < 3)
		goto out;

	ep += 2;

	/*
	 * First entry  : file entry
	 * Second entry : stream-extension entry
	 * Third entry  : first file-name entry
	 * So, the index of first file-name dentry should start from 2.
	 */
	for (i = 2; i < es->num_entries; i++, ep++) {
		/* end of name entry */
		if (exfat_get_entry_type(ep) != TYPE_EXTEND)
			goto out;

		__extract_uni_name_from_name_entry((NAME_DENTRY_T *)ep, uniname, i);
		uniname += 15;
	}

out:
	release_dentry_set(es);
} /* end of exfat_get_uniname_from_ext_entry */

static s32 exfat_calc_num_entries(UNI_NAME_T *p_uniname)
{
	s32 len;

	len = p_uniname->name_len;
	if (len == 0)
		return 0;

	/* 1 file entry + 1 stream entry + name entries */
	return((len-1) / 15 + 3);

} /* end of exfat_calc_num_entries */


/*
 *  Allocation Bitmap Management Functions
 */
s32 load_alloc_bmp(struct super_block *sb)
{
	s32 i, j, ret;
	u32 map_size;
	u32 type, sector;
	CHAIN_T clu;
	BMAP_DENTRY_T *ep;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	clu.dir = fsi->root_dir;
	clu.flags = 0x01;

	while (!IS_CLUS_EOF(clu.dir)) {
		for (i = 0; i < fsi->dentries_per_clu; i++) {
			ep = (BMAP_DENTRY_T *) get_dentry_in_dir(sb, &clu, i, NULL);
			if (!ep)
				return -EIO;

			type = exfat_get_entry_type((DENTRY_T *) ep);

			if (type == TYPE_UNUSED)
				break;
			if (type != TYPE_BITMAP)
				continue;

			if (ep->flags == 0x0) {
				fsi->map_clu  = le32_to_cpu(ep->start_clu);
				map_size = (u32) le64_to_cpu(ep->size);

				fsi->map_sectors = ((map_size-1) >> (sb->s_blocksize_bits)) + 1;
				fsi->vol_amap = (struct buffer_head **) kmalloc((sizeof(struct buffer_head *) * fsi->map_sectors), GFP_KERNEL);
				if (!fsi->vol_amap)
					return -ENOMEM;

				sector = CLUS_TO_SECT(fsi, fsi->map_clu);

				for (j = 0; j < fsi->map_sectors; j++) {
					fsi->vol_amap[j] = NULL;
					ret = read_sect(sb, sector+j, &(fsi->vol_amap[j]), 1);
					if (ret) {
						/*  release all buffers and free vol_amap */
						i=0;
						while (i < j)
							brelse(fsi->vol_amap[i++]);

						if (fsi->vol_amap) {
							kfree(fsi->vol_amap);
							fsi->vol_amap = NULL;
						}
						return ret;
					}
				}

				fsi->pbr_bh = NULL;
				return 0;
			}
		}

		if (get_next_clus_safe(sb, &clu.dir))
			return -EIO;
	}

	return -EINVAL;
} /* end of load_alloc_bmp */

void free_alloc_bmp(struct super_block *sb)
{
	s32 i;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	brelse(fsi->pbr_bh);

	for (i = 0; i < fsi->map_sectors; i++) {
		__brelse(fsi->vol_amap[i]);
	}

	if(fsi->vol_amap) {
		kfree(fsi->vol_amap);
		fsi->vol_amap = NULL;
	}
}

/* WARN :
 * If the value of "clu" is 0, it means cluster 2 which is
 * the first cluster of cluster heap.
 */
static s32 set_alloc_bitmap(struct super_block *sb, u32 clu)
{
	s32 i, b;
	u32 sector;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	i = clu >> (sb->s_blocksize_bits + 3);
	b = clu & (u32)((sb->s_blocksize << 3) - 1);

	sector = CLUS_TO_SECT(fsi, fsi->map_clu) + i;

	bitmap_set((unsigned long*)(fsi->vol_amap[i]->b_data), b, 1);

	return write_sect(sb, sector, fsi->vol_amap[i], 0);
} /* end of set_alloc_bitmap */

/* WARN :
 * If the value of "clu" is 0, it means cluster 2 which is
 * the first cluster of cluster heap.
 */
static s32 clr_alloc_bitmap(struct super_block *sb, u32 clu)
{
	s32 ret;
	s32 i, b;
	u32 sector;
	struct sdfat_sb_info *sbi = SDFAT_SB(sb);
	struct sdfat_mount_options *opts = &sbi->options;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	i = clu >> (sb->s_blocksize_bits + 3);
	b = clu & (u32)((sb->s_blocksize << 3) - 1);

	sector = CLUS_TO_SECT(fsi, fsi->map_clu) + i;

	bitmap_clear((unsigned long*)(fsi->vol_amap[i]->b_data), b, 1);

	ret = write_sect(sb, sector, fsi->vol_amap[i], 0);

	if (opts->discard) {
		s32 ret_discard;
		TMSG("discard cluster(%08x)\n", clu+2);
		ret_discard = sb_issue_discard(sb, CLUS_TO_SECT(fsi, clu+2),
				(1 << fsi->sect_per_clus_bits), GFP_NOFS, 0);

		if (ret_discard == -EOPNOTSUPP) {
			sdfat_msg(sb, KERN_ERR,
				"discard not supported by device, disabling");
			opts->discard = 0;
		}
	}

	return ret;
} /* end of clr_alloc_bitmap */

/* WARN :
 * If the value of "clu" is 0, it means cluster 2 which is
 * the first cluster of cluster heap.
 */
static u32 test_alloc_bitmap(struct super_block *sb, u32 clu)
{
	u32 i, map_i, map_b;
	u32 clu_base, clu_free;
	u8 k, clu_mask;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	clu_base = (clu & ~(0x7)) + 2;
	clu_mask = (1 << (clu - clu_base + 2)) - 1;

	map_i = clu >> (sb->s_blocksize_bits + 3);
	map_b = (clu >> 3) & (u32)(sb->s_blocksize - 1);

	for (i = 2; i < fsi->num_clusters; i += 8) {
		k = *(((u8 *) fsi->vol_amap[map_i]->b_data) + map_b);
		if (clu_mask > 0) {
			k |= clu_mask;
			clu_mask = 0;
		}
		if (k < 0xFF) {
			clu_free = clu_base + free_bit[k];
			if (clu_free < fsi->num_clusters)
				return clu_free;
		}
		clu_base += 8;

		if (((++map_b) >= (u32)sb->s_blocksize) ||
			(clu_base >= fsi->num_clusters)) {
			if ((++map_i) >= fsi->map_sectors) {
				clu_base = 2;
				map_i = 0;
			}
			map_b = 0;
		}
	}

	return CLUS_EOF;
} /* end of test_alloc_bitmap */

void sync_alloc_bmp(struct super_block *sb)
{
	s32 i;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	if (fsi->vol_amap == NULL)
		return;

	for (i = 0; i < fsi->map_sectors; i++)
		sync_dirty_buffer(fsi->vol_amap[i]);
}

static s32 exfat_chain_cont_cluster(struct super_block *sb, u32 chain, s32 len)
{
	if (!len)
		return 0;

	while (len > 1) {
		if (fat_ent_set(sb, chain, chain+1))
			return -EIO;
		chain++;
		len--;
	}

	if (fat_ent_set(sb, chain, CLUS_EOF))
		return -EIO;
	return 0;
}

s32 chain_cont_cluster(struct super_block *sb, u32 chain, s32 len)
{
	return exfat_chain_cont_cluster(sb, chain, len);
}

static s32 exfat_alloc_cluster(struct super_block *sb, s32 num_alloc, CHAIN_T *p_chain, int dest)
{
	s32 num_clusters = 0;
	u32 hint_clu, new_clu, last_clu = CLUS_EOF;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	hint_clu = p_chain->dir;
	/* find new cluster */
	if (IS_CLUS_EOF(hint_clu)) {
		if (fsi->clu_srch_ptr < 2) {
			EMSG("%s: fsi->clu_srch_ptr is invalid (%u)\n",
					__func__,
					fsi->clu_srch_ptr);
			sdfat_debug_bug_on(1);
			fsi->clu_srch_ptr = 2;
		}

		hint_clu = test_alloc_bitmap(sb, fsi->clu_srch_ptr-2);
		if (IS_CLUS_EOF(hint_clu))
			return 0;
	}

	/* check cluster validation */
	if ((hint_clu < 2) && (hint_clu >= fsi->num_clusters)) {
		EMSG("%s: hint_cluster is invalid (%u)\n", __func__, hint_clu);
		sdfat_debug_bug_on(1);
		hint_clu = 2;
		if (p_chain->flags == 0x03) {
			if (exfat_chain_cont_cluster( sb, p_chain->dir, num_clusters))
				return -EIO;
			p_chain->flags = 0x01;
		}
	}

	set_sb_dirty(sb);

	p_chain->dir = CLUS_EOF;

	while ((new_clu = test_alloc_bitmap(sb, hint_clu-2)) != CLUS_EOF) {
		if ( (new_clu != hint_clu) && (p_chain->flags == 0x03) ) {
			if (exfat_chain_cont_cluster( sb, p_chain->dir, num_clusters))
				return -EIO;
			p_chain->flags = 0x01;
		}

		/* update allocation bitmap */
		if (set_alloc_bitmap(sb, new_clu-2))
			return -EIO;

		num_clusters++;

		/* update FAT table */
		if (p_chain->flags == 0x01)
			if (fat_ent_set(sb, new_clu, CLUS_EOF))
				return -EIO;

		if (IS_CLUS_EOF(p_chain->dir)) {
			p_chain->dir = new_clu;
		} else if (p_chain->flags == 0x01) {
			if (fat_ent_set(sb, last_clu, new_clu))
				return -EIO;
		}
		last_clu = new_clu;

		if ((--num_alloc) == 0) {
			fsi->clu_srch_ptr = hint_clu;
			if (fsi->used_clusters != (u32) ~0)
				fsi->used_clusters += num_clusters;

			p_chain->size += num_clusters;
			return num_clusters;
		}

		hint_clu = new_clu + 1;
		if (hint_clu >= fsi->num_clusters) {
			hint_clu = 2;

			if (p_chain->flags == 0x03) {
				if (exfat_chain_cont_cluster(sb, p_chain->dir, num_clusters))
					return -EIO;
				p_chain->flags = 0x01;
			}
		}
	}

	fsi->clu_srch_ptr = hint_clu;
	if (fsi->used_clusters != (u32) ~0)
		fsi->used_clusters += num_clusters;

	p_chain->size += num_clusters;
	return num_clusters;
} /* end of exfat_alloc_cluster */


static s32 exfat_free_cluster(struct super_block *sb, CHAIN_T *p_chain, s32 do_relse)
{
	s32 ret = -EIO;
	s32 num_clusters = 0;
	u32 clu;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);
	s32 i;
	u32 sector;

	/* invalid cluster number */
	if (IS_CLUS_FREE(p_chain->dir) || IS_CLUS_EOF(p_chain->dir))
		return 0;

	/* no cluster to truncate */
	if (p_chain->size <= 0) {
		DMSG("%s: cluster(%u) truncation is not required.",
			 __func__, p_chain->dir);
		return 0;
	}

	/* check cluster validation */
	if ((p_chain->dir < 2) && (p_chain->dir >= fsi->num_clusters)) {
		EMSG("%s: invalid start cluster (%u)\n", __func__, p_chain->dir);
		sdfat_debug_bug_on(1);
		return -EIO;
	}

	set_sb_dirty(sb);
	clu = p_chain->dir;

	if (p_chain->flags == 0x03) {
		do {
			if (do_relse) {
				sector = CLUS_TO_SECT(fsi, clu);
				for (i = 0; i < fsi->sect_per_clus; i++) {
					if (dcache_release(sb, sector+i) == -EIO)
						goto out;
				}
			}

			if (clr_alloc_bitmap(sb, clu-2))
				goto out;
			clu++;

			num_clusters++;
		} while (num_clusters < p_chain->size);
	} else {
		do {
			if (do_relse) {
				sector = CLUS_TO_SECT(fsi, clu);
				for (i = 0; i < fsi->sect_per_clus; i++) {
					if (dcache_release(sb, sector+i) == -EIO)
						goto out;
				}
			}

			if (clr_alloc_bitmap(sb, (clu - CLUS_BASE)))
				goto out;

			if (get_next_clus_safe(sb, &clu))
				goto out;

			num_clusters++;
		} while (!IS_CLUS_EOF(clu));
	}

	/* success */
	ret = 0;
out:

	if (fsi->used_clusters != (u32) ~0)
		fsi->used_clusters -= num_clusters;
	return ret;
} /* end of exfat_free_cluster */

static s32 exfat_count_used_clusters(struct super_block *sb, u32* ret_count)
{
	u32 count = 0;
	u32 i, map_i, map_b;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);
	u32 total_clus = fsi->num_clusters - 2;

	map_i = map_b = 0;

	for (i = 0; i < total_clus; i += 8) {
		u8 k = *(((u8 *) fsi->vol_amap[map_i]->b_data) + map_b);
		count += used_bit[k];

		if ((++map_b) >= (u32)sb->s_blocksize) {
			map_i++;
			map_b = 0;
		}
	}

	/* FIXME : abnormal bitmap count should be handled as more smart */
	if (total_clus < count)
		count = total_clus;

	*ret_count = count;
	return 0;
} /* end of exfat_count_used_clusters */


/*
 *  File Operation Functions
 */
static FS_FUNC_T exfat_fs_func = {
	.alloc_cluster = exfat_alloc_cluster,
	.free_cluster = exfat_free_cluster,
	.count_used_clusters = exfat_count_used_clusters,

	.init_dir_entry = exfat_init_dir_entry,
	.init_ext_entry = exfat_init_ext_entry,
	.find_dir_entry = exfat_find_dir_entry,
	.delete_dir_entry = exfat_delete_dir_entry,
	.get_uniname_from_ext_entry = exfat_get_uniname_from_ext_entry,
	.count_ext_entries = exfat_count_ext_entries,
	.calc_num_entries = exfat_calc_num_entries,

	.get_entry_type = exfat_get_entry_type,
	.set_entry_type = exfat_set_entry_type,
	.get_entry_attr = exfat_get_entry_attr,
	.set_entry_attr = exfat_set_entry_attr,
	.get_entry_flag = exfat_get_entry_flag,
	.set_entry_flag = exfat_set_entry_flag,
	.get_entry_clu0 = exfat_get_entry_clu0,
	.set_entry_clu0 = exfat_set_entry_clu0,
	.get_entry_size = exfat_get_entry_size,
	.set_entry_size = exfat_set_entry_size,
	.get_entry_time = exfat_get_entry_time,
	.set_entry_time = exfat_set_entry_time,
};

s32 mount_exfat(struct super_block *sb, pbr_t *p_pbr)
{
	pbr64_t *p_bpb = (pbr64_t *)p_pbr;
	FS_INFO_T *fsi = &(SDFAT_SB(sb)->fsi);

	if (!p_bpb->bsx.num_fats) {
		sdfat_msg(sb, KERN_ERR, "bogus number of FAT structure");
		return -EINVAL;
	}

	fsi->sect_per_clus = 1 << p_bpb->bsx.sect_per_clus_bits;
	fsi->sect_per_clus_bits = p_bpb->bsx.sect_per_clus_bits;
	fsi->cluster_size_bits = fsi->sect_per_clus_bits + sb->s_blocksize_bits;
	fsi->cluster_size = 1 << fsi->cluster_size_bits;

	fsi->num_FAT_sectors = le32_to_cpu(p_bpb->bsx.fat_length);

	fsi->FAT1_start_sector = le32_to_cpu(p_bpb->bsx.fat_offset);
	if (p_bpb->bsx.num_fats == 1)
		fsi->FAT2_start_sector = fsi->FAT1_start_sector;
	else
		fsi->FAT2_start_sector = fsi->FAT1_start_sector + fsi->num_FAT_sectors;

	fsi->root_start_sector = le32_to_cpu(p_bpb->bsx.clu_offset);
	fsi->data_start_sector = fsi->root_start_sector;

	fsi->num_sectors = le64_to_cpu(p_bpb->bsx.vol_length);
	fsi->num_clusters = le32_to_cpu(p_bpb->bsx.clu_count) + 2;
	/* because the cluster index starts with 2 */

	fsi->vol_type = EXFAT;
	fsi->vol_id = le32_to_cpu(p_bpb->bsx.vol_serial);

	fsi->root_dir = le32_to_cpu(p_bpb->bsx.root_cluster);
	fsi->dentries_in_root = 0;
	fsi->dentries_per_clu = 1 << (fsi->cluster_size_bits - DENTRY_SIZE_BITS);

	fsi->vol_flag = (u32) le16_to_cpu(p_bpb->bsx.vol_flags);
	fsi->clu_srch_ptr = 2;
	fsi->used_clusters = (u32) ~0;

	fsi->fs_func = &exfat_fs_func;
	fat_ent_ops_init(sb);
	
	sdfat_log_msg(sb, KERN_INFO, "detected fs-type : exFAT");
	
	if (p_bpb->bsx.vol_flags & VOL_DIRTY) {
		fsi->vol_flag |= VOL_DIRTY;
		sdfat_log_msg(sb, KERN_WARNING, "Volume was not properly "
				"unmounted. Some data may be corrupt. "
				"Please run fsck.");
	}

	return 0;
} /* end of mount_exfat */

/* end of core_exfat.c */
