/* DISKDUMP/KDUMP format test suite.
   Copyright (C) 2016 Petr Tesarik <ptesarik@suse.com>

   This file is free software; you can redistribute it and/or modify
   it under the terms of either

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version

   or both in parallel, as here.

   libkdumpfile is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>
#include <endian.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

#include "config.h"
#include "testutil.h"
#include "diskdump.h"

#if USE_ZLIB
# include <zlib.h>
#endif
#if USE_LZO
# include <lzo/lzo1x.h>
#endif
#if USE_SNAPPY
# include <snappy-c.h>
#endif
#if USE_ZSTD
# include <zstd.h>
#endif
typedef int write_fn(FILE *);

struct page_data_kdump {
	FILE *f;

	unsigned long long addr;
	unsigned long flags;
	enum {
		compress_auto = -1,
		compress_no,
		compress_yes,
		compress_exclude,
	} compress;

	void *cbuf;
	size_t cbufsz;
#if USE_LZO
	lzo_bytep lzo_wrkmem;
#endif
};

static endian_t be;
static write_fn *writeheader;
static off_t pdoff, dataoff;

static unsigned char *bitmap1, *bitmap2;

enum compress_method {
	COMPRESS_NONE,
	COMPRESS_ZLIB,
	COMPRESS_LZO,
	COMPRESS_SNAPPY,
	COMPRESS_ZSTD,
};

static bool flattened;
static unsigned long long flattened_type = MDF_TYPE_FLAT_HEADER;
static unsigned long long flattened_version = MDF_VERSION_FLAT_HEADER;
static char *arch_name;
static unsigned long long compression;
static char *signature;
static unsigned long long header_version;

static char *uts_sysname;
static char *uts_nodename;
static char *uts_release;
static char *uts_version;
static char *uts_machine;
static char *uts_domainname;

static unsigned long long status;
static unsigned long long block_size;
static unsigned long long sub_hdr_size;
static unsigned long long bitmap_blocks;
static unsigned long long max_mapnr;
static unsigned long long total_ram_blocks;
static unsigned long long device_blocks;
static unsigned long long written_blocks;
static unsigned long long current_cpu;
static unsigned long long nr_cpus;
static struct number_array tasks;

static unsigned long long phys_base;
static unsigned long long dump_level;
static unsigned long long split;
static unsigned long long start_pfn;
static unsigned long long end_pfn;

static struct blob *vmcoreinfo;
static struct blob *notes;
static struct blob *eraseinfo;

static char *vmcoreinfo_file;
static char *note_file;
static char *eraseinfo_file;
static char *data_file;

static unsigned long start_pdidx;

static const struct param param_array[] = {
	/* meta-data */
	PARAM_YESNO("flattened", flattened),
	PARAM_NUMBER("flattened.type", flattened_type),
	PARAM_NUMBER("flattened.version", flattened_version),
	PARAM_STRING("arch_name", arch_name),
	PARAM_NUMBER("compression", compression),

	/* header */
	PARAM_STRING("signature", signature),
	PARAM_NUMBER("version", header_version),

	PARAM_STRING("uts.sysname", uts_sysname),
	PARAM_STRING("uts.nodename", uts_nodename),
	PARAM_STRING("uts.release", uts_release),
	PARAM_STRING("uts.version", uts_version),
	PARAM_STRING("uts.machine", uts_machine),
	PARAM_STRING("uts.domainname", uts_domainname),

	PARAM_NUMBER("status", status),
	PARAM_NUMBER("block_size", block_size),
	PARAM_NUMBER("sub_hdr_size", sub_hdr_size),
	PARAM_NUMBER("bitmap_blocks", bitmap_blocks),
	PARAM_NUMBER("max_mapnr", max_mapnr),
	PARAM_NUMBER("total_ram_blocks", total_ram_blocks),
	PARAM_NUMBER("device_blocks", device_blocks),
	PARAM_NUMBER("written_blocks", written_blocks),
	PARAM_NUMBER("current_cpu", current_cpu),
	PARAM_NUMBER("nr_cpus", nr_cpus),
	PARAM_NUMBER_ARRAY("tasks", tasks),

	/* sub-header */
	PARAM_NUMBER("phys_base", phys_base),
	PARAM_NUMBER("dump_level", dump_level),
	PARAM_NUMBER("split", split),
	PARAM_NUMBER("start_pfn", start_pfn),
	PARAM_NUMBER("end_pfn", end_pfn),

	/* data files */
	PARAM_STRING("VMCOREINFO", vmcoreinfo_file),
	PARAM_STRING("NOTE", note_file),
	PARAM_STRING("ERASEINFO", eraseinfo_file),
	PARAM_STRING("DATA", data_file)
};

static const struct params params = {
	ARRAY_SIZE(param_array),
	param_array
};

static int
set_default_params(void)
{
	arch_name = strdup("x86_64");
	if (!arch_name)
		return TEST_ERR;

	signature = strdup(KDUMP_SIGNATURE);
	if (!signature)
		return TEST_ERR;

	uts_sysname = strdup("Linux");
	if (!uts_sysname)
		return TEST_ERR;

	uts_nodename = strdup("");
	if (!uts_nodename)
		return TEST_ERR;

	uts_release = strdup("0.0.0");
	if (!uts_release)
		return TEST_ERR;

	uts_version = strdup("#1");
	if (!uts_version)
		return TEST_ERR;

	uts_machine = strdup("x86_64");
	if (!uts_machine)
		return TEST_ERR;

	uts_domainname = strdup("(none)");
	if (!uts_domainname)
		return TEST_ERR;

	return TEST_OK;
}

static int
write_chunk(FILE *f, off_t off, const void *ptr, size_t sz, const char *what)
{
	if (flattened) {
		struct makedumpfile_data_header hdr = {
			.offset = htobe64(off),
			.buf_size = htobe64(sz),
		};
		if (fwrite(&hdr, sizeof hdr, 1, f) != 1) {
			perror("flattened segment header");
			return -1;
		}
	} else if (fseek(f, off, SEEK_SET) != 0) {
		fprintf(stderr, "seek %s: %s\n", what, strerror(errno));
		return -1;
	}
	if (fwrite(ptr, sz, 1, f) != 1) {
		fprintf(stderr, "write %s: %s\n", what, strerror(errno));
		return -1;
	}
	return 0;
}

static int
writeheader_32(FILE *f)
{
	struct timeval tv;
	struct disk_dump_header_32 hdr;
	struct kdump_sub_header_32 subhdr;
	off_t pos;

	if (gettimeofday(&tv, NULL) != 0) {
		perror("gettimeofday");
		return TEST_ERR;
	}

	/* initialize dump header to zero */
	memset(&hdr, 0, sizeof hdr);

	strncpy(hdr.signature, signature, SIGNATURE_LEN);
	hdr.header_version = htodump32(be, header_version);
	strncpy(hdr.utsname_sysname, uts_sysname,
		sizeof hdr.utsname_sysname);
	strncpy(hdr.utsname_nodename, uts_nodename,
		sizeof hdr.utsname_nodename);
	strncpy(hdr.utsname_release, uts_release,
		sizeof hdr.utsname_release);
	strncpy(hdr.utsname_version, uts_version,
		sizeof hdr.utsname_version);
	strncpy(hdr.utsname_machine, uts_machine,
		sizeof hdr.utsname_machine);
	strncpy(hdr.utsname_domainname, uts_domainname,
		sizeof hdr.utsname_domainname);
	hdr.timestamp.tv_sec = htodump32(be, tv.tv_sec);
	hdr.timestamp.tv_usec = htodump32(be, tv.tv_usec);
	hdr.status = htodump32(be, status);
	hdr.block_size = htodump32(be, block_size);
	hdr.sub_hdr_size = htodump32(be, sub_hdr_size);
	hdr.bitmap_blocks = htodump32(be, bitmap_blocks);
	hdr.max_mapnr = htodump32(be, max_mapnr);
	hdr.total_ram_blocks = htodump32(be, total_ram_blocks);
	hdr.device_blocks = htodump32(be, device_blocks);
	hdr.written_blocks = htodump32(be, written_blocks);
	hdr.current_cpu = htodump32(be, current_cpu);
	hdr.nr_cpus = htodump32(be, nr_cpus);

	if (write_chunk(f, 0, &hdr, sizeof hdr, "header"))
		return TEST_ERR;

	pos = DISKDUMP_HEADER_BLOCKS * block_size + sizeof(subhdr);
	subhdr.phys_base = htodump32(be, phys_base);
	subhdr.dump_level = htodump32(be, dump_level);
	subhdr.split = htodump32(be, split);
	subhdr.start_pfn = htodump32(be, start_pfn);
	subhdr.end_pfn = htodump32(be, end_pfn);
	if (vmcoreinfo) {
		subhdr.offset_vmcoreinfo = htodump64(be, pos);
		subhdr.size_vmcoreinfo = htodump32(be, vmcoreinfo->length);
		if (write_chunk(f, pos, vmcoreinfo->data, vmcoreinfo->length,
				"VMCOREINFO"))
			return TEST_ERR;
		pos += vmcoreinfo->length;
	} else {
		subhdr.offset_vmcoreinfo = htodump64(be, 0);
		subhdr.size_vmcoreinfo = htodump32(be, 0);
	}
	if (notes) {
		subhdr.offset_note = htodump64(be, pos);
		subhdr.size_note = htodump32(be, notes->length);
		if (write_chunk(f, pos, notes->data, notes->length,
				"ELF notes"))
			return TEST_ERR;
		pos += notes->length;
	} else {
		subhdr.offset_note = htodump64(be, 0);
		subhdr.size_note = htodump32(be, 0);
	}
	if (eraseinfo) {
		subhdr.offset_eraseinfo = htodump64(be, pos);
		subhdr.size_eraseinfo = htodump32(be, eraseinfo->length);
		if (write_chunk(f, pos, notes->data, notes->length,
				"eraseinfo"))
			return TEST_ERR;
		pos += eraseinfo->length;
	} else {
		subhdr.offset_eraseinfo = htodump64(be, 0);
		subhdr.size_eraseinfo = htodump32(be, 0);
	}
	subhdr.start_pfn_64 = htodump64(be, start_pfn);
	subhdr.end_pfn_64 = htodump64(be, end_pfn);
	subhdr.max_mapnr_64 = htodump64(be, max_mapnr);

	if (write_chunk(f, DISKDUMP_HEADER_BLOCKS * block_size,
			&subhdr, sizeof subhdr, "subheader"))
		return TEST_ERR;

	return TEST_OK;
}

static int
writeheader_64(FILE *f)
{
	struct timeval tv;
	struct disk_dump_header_64 hdr;
	struct kdump_sub_header_64 subhdr;
	off_t pos;

	if (gettimeofday(&tv, NULL) != 0) {
		perror("gettimeofday");
		return TEST_ERR;
	}

	/* initialize dump header to zero */
	memset(&hdr, 0, sizeof hdr);

	strncpy(hdr.signature, signature, SIGNATURE_LEN);
	hdr.header_version = htodump32(be, header_version);
	strncpy(hdr.utsname_sysname, uts_sysname,
		sizeof hdr.utsname_sysname);
	strncpy(hdr.utsname_nodename, uts_nodename,
		sizeof hdr.utsname_nodename);
	strncpy(hdr.utsname_release, uts_release,
		sizeof hdr.utsname_release);
	strncpy(hdr.utsname_version, uts_version,
		sizeof hdr.utsname_version);
	strncpy(hdr.utsname_machine, uts_machine,
		sizeof hdr.utsname_machine);
	strncpy(hdr.utsname_domainname, uts_domainname,
		sizeof hdr.utsname_domainname);
	hdr.timestamp.tv_sec = htodump64(be, tv.tv_sec);
	hdr.timestamp.tv_usec = htodump64(be, tv.tv_usec);
	hdr.status = htodump32(be, status);
	hdr.block_size = htodump32(be, block_size);
	hdr.sub_hdr_size = htodump32(be, sub_hdr_size);
	hdr.bitmap_blocks = htodump32(be, bitmap_blocks);
	hdr.max_mapnr = htodump32(be, max_mapnr);
	hdr.total_ram_blocks = htodump32(be, total_ram_blocks);
	hdr.device_blocks = htodump32(be, device_blocks);
	hdr.written_blocks = htodump32(be, written_blocks);
	hdr.current_cpu = htodump32(be, current_cpu);
	hdr.nr_cpus = htodump32(be, nr_cpus);

	if (write_chunk(f, 0, &hdr, sizeof hdr, "header"))
		return TEST_ERR;

	pos = DISKDUMP_HEADER_BLOCKS * block_size + sizeof(subhdr);
	subhdr.phys_base = htodump64(be, phys_base);
	subhdr.dump_level = htodump32(be, dump_level);
	subhdr.split = htodump32(be, split);
	subhdr.start_pfn = htodump64(be, start_pfn);
	subhdr.end_pfn = htodump64(be, end_pfn);
	if (vmcoreinfo) {
		subhdr.offset_vmcoreinfo = htodump64(be, pos);
		subhdr.size_vmcoreinfo = htodump64(be, vmcoreinfo->length);
		if (write_chunk(f, pos, vmcoreinfo->data, vmcoreinfo->length,
				"VMCOREINFO"))
			return TEST_ERR;
		pos += vmcoreinfo->length;
	} else {
		subhdr.offset_vmcoreinfo = htodump64(be, 0);
		subhdr.size_vmcoreinfo = htodump64(be, 0);
	}
	if (notes) {
		subhdr.offset_note = htodump64(be, pos);
		subhdr.size_note = htodump64(be, notes->length);
		if (write_chunk(f, pos, notes->data, notes->length,
				"ELF notes"))
			return TEST_ERR;
		pos += notes->length;
	} else {
		subhdr.offset_note = htodump64(be, 0);
		subhdr.size_note = htodump64(be, 0);
	}
	if (eraseinfo) {
		subhdr.offset_eraseinfo = htodump64(be, pos);
		subhdr.size_eraseinfo = htodump64(be, eraseinfo->length);
		if (write_chunk(f, pos, notes->data, notes->length,
				"eraseinfo"))
			return TEST_ERR;
		pos += eraseinfo->length;
	} else {
		subhdr.offset_eraseinfo = htodump64(be, 0);
		subhdr.size_eraseinfo = htodump64(be, 0);
	}
	subhdr.start_pfn_64 = htodump64(be, start_pfn);
	subhdr.end_pfn_64 = htodump64(be, end_pfn);
	subhdr.max_mapnr_64 = htodump64(be, max_mapnr);

	if (write_chunk(f, DISKDUMP_HEADER_BLOCKS * block_size,
			&subhdr, sizeof subhdr, "subheader"))
		return TEST_ERR;

	return TEST_OK;
}

static int
parseheader(struct page_data *pg, char *p)
{
	struct page_data_kdump *pgkdump = pg->priv;
	char *endp;

	if (!*p) {
		pgkdump->addr += block_size;
		return TEST_OK;
	}

	pgkdump->addr = strtoull(p, &endp, 0);
	if (*endp && !isspace(*endp)) {
		*endp = '\0';
		fprintf(stderr, "Invalid address: %s\n", p);
		return TEST_FAIL;
	}

	pgkdump->flags = 0;
	pgkdump->compress = compress_auto;

	p = endp;
	while (*p && isspace(*p))
		++p;

	if (!*p)
		return TEST_OK;

	if (!strcmp(p, "raw")) {
		pgkdump->compress = compress_no;
	} else if (!strcmp(p, "zlib")) {
		pgkdump->flags |= DUMP_DH_COMPRESSED_ZLIB;
		pgkdump->compress = compress_yes;
	} else if (!strcmp(p, "lzo")) {
		pgkdump->flags |= DUMP_DH_COMPRESSED_LZO;
		pgkdump->compress = compress_yes;
	} else if (!strcmp(p, "snappy")) {
		pgkdump->flags |= DUMP_DH_COMPRESSED_SNAPPY;
		pgkdump->compress = compress_yes;
	} else if (!strcmp(p, "exclude")) {
		pgkdump->compress = compress_exclude;
	} else if (!strcmp(p, "zstd")) {
		pgkdump->flags |= DUMP_DH_COMPRESSED_ZSTD;
		pgkdump->compress = compress_yes;
	} else {
		pgkdump->flags = strtoul(p, &endp, 0);
		if (*endp) {
			fprintf(stderr, "Invalid flags: %s\n", p);
			return TEST_FAIL;
		}
	}

	return TEST_OK;
}

#if USE_ZLIB || USE_LZO || USE_SNAPPY || USE_ZSTD
static size_t
enlarge_cbuf(struct page_data_kdump *pgkdump, size_t newsz)
{
	unsigned char *newbuf;

	newbuf = realloc(pgkdump->cbuf, newsz);
	if (!newbuf) {
		perror("Cannot enlarge compression buffer");
		return 0;
	}
	pgkdump->cbuf = newbuf;
	pgkdump->cbufsz = newsz;
	return newsz;
}
#endif

#if USE_ZLIB
static size_t
do_gzip(struct page_data *pg)
{
	struct page_data_kdump *pgkdump = pg->priv;
	uLongf clen;

	clen = pgkdump->cbufsz;
	while (compress(pgkdump->cbuf, &clen, pg->buf, pg->len) != Z_OK) {
		clen = enlarge_cbuf(pgkdump, clen + (block_size >> 2));
		if (!clen)
			break;
	}
	return clen;
}
#endif

#if USE_LZO
static size_t
do_lzo(struct page_data *pg)
{
	struct page_data_kdump *pgkdump = pg->priv;
	lzo_uint clen;

	clen = pg->len + pg->len / 16 + 64 + 3;
	if (clen > pgkdump->cbufsz &&
	    !(clen = enlarge_cbuf(pgkdump, clen)))
		return clen;

	if (lzo1x_1_compress(pg->buf, pg->len, pgkdump->cbuf, &clen,
			     pgkdump->lzo_wrkmem) != LZO_E_OK) {
		fprintf(stderr, "LZO compression failed\n");
		clen = 0;
	}
	return clen;
}
#endif

#if USE_SNAPPY
static size_t
do_snappy(struct page_data *pg)
{
	struct page_data_kdump *pgkdump = pg->priv;
	size_t clen;

	clen = snappy_max_compressed_length(pg->len);
	if (clen > pgkdump->cbufsz &&
	    !(clen = enlarge_cbuf(pgkdump, clen)))
		return clen;

	if (snappy_compress((const char*)pg->buf, pg->len,
			    pgkdump->cbuf, &clen) != SNAPPY_OK) {
		fprintf(stderr, "snappy compression failed\n");
		clen = 0;
	}
	return clen;
}
#endif

#if USE_ZSTD
static size_t
do_zstd(struct page_data *pg)
{
	struct page_data_kdump *pgkdump = pg->priv;
	size_t clen;

	clen = ZSTD_compressBound(pg->len);
	if (clen > pgkdump->cbufsz &&
	    !(clen = enlarge_cbuf(pgkdump, clen)))
		return clen;

	clen = ZSTD_compress(pgkdump->cbuf, clen, pg->buf, pg->len, 1);
	if (ZSTD_isError(clen)) {
		fprintf(stderr, "zstd compression failed\n");
		clen = 0;
	}
	return clen;
}
#endif

static size_t
compresspage(struct page_data *pg, uint32_t *pflags)
{
	if ((*pflags & (DUMP_DH_COMPRESSED)) == 0)
		switch (compression) {
		case COMPRESS_ZLIB:
			*pflags |= DUMP_DH_COMPRESSED_ZLIB;
			break;
		case COMPRESS_LZO:
			*pflags |= DUMP_DH_COMPRESSED_LZO;
			break;
		case COMPRESS_SNAPPY:
			*pflags |= DUMP_DH_COMPRESSED_SNAPPY;
			break;
		case COMPRESS_ZSTD:
			*pflags |= DUMP_DH_COMPRESSED_ZSTD;
			break;
		}

#if USE_ZLIB
	if (*pflags & DUMP_DH_COMPRESSED_ZLIB)
		return do_gzip(pg);
#endif
#if USE_LZO
	if(*pflags & DUMP_DH_COMPRESSED_LZO)
		return do_lzo(pg);
#endif
#if USE_SNAPPY
	if (*pflags & DUMP_DH_COMPRESSED_SNAPPY)
		return do_snappy(pg);
#endif
#if USE_ZSTD
	if (*pflags & DUMP_DH_COMPRESSED_ZSTD)
		return do_zstd(pg);
#endif

	fprintf(stderr, "Unsupported compression flags: %lu\n",
		(unsigned long) *pflags);

	return 0;
}

static int
markpage(struct page_data *pg)
{
	struct page_data_kdump *pgkdump = pg->priv;
	unsigned long long pfn, blknum;
	size_t idx;

	pfn = pgkdump->addr / block_size;
	blknum = (pfn / block_size) >> 3;
	if (blknum >= bitmap_blocks / 2) {
		fprintf(stderr, "PFN too large: %llu\n", pfn);
		return TEST_ERR;
	}

	idx = pfn >> 3;
	bitmap1[idx] |= 1U << (pfn & 7);
	if (pgkdump->compress != compress_exclude)
		bitmap2[idx] |= 1U << (pfn & 7);

	dataoff += sizeof(struct page_desc);
	return TEST_OK;
}

static inline unsigned
bitcount(unsigned x)
{
	return (uint32_t)((((x * 0x08040201) >> 3) & 0x11111111) * 0x11111111)
		>> 28;
}

unsigned long
bitmap_index(const unsigned char *bmp, unsigned long bit)
{
	unsigned long ret = 0;
	unsigned char mask;
	while (bit >= 8) {
		ret += bitcount(*bmp++);
		bit -= 8;
	}
	for (mask = 1; bit; --bit, mask <<= 1)
		if (*bmp & mask)
			++ret;
	return ret;
}

static int
writepage(struct page_data *pg)
{
	struct page_data_kdump *pgkdump = pg->priv;
	struct page_desc pd;
	unsigned long pdidx;
	unsigned char *buf;
	unsigned long pfn;
	size_t buflen;
	uint32_t flags;

	if (pgkdump->compress == compress_exclude)
		return TEST_OK;

	pfn = pgkdump->addr / block_size;
	if (split && (start_pfn > pfn || pfn >= end_pfn))
		return TEST_OK;

	flags = pgkdump->flags;

	if (pg->len &&
	    (pgkdump->compress == compress_yes ||
	     (pgkdump->compress == compress_auto &&
	      compression != COMPRESS_NONE))) {
		buflen = compresspage(pg, &flags);
		if (!buflen)
			return TEST_ERR;
		buf = pgkdump->cbuf;

		if (pgkdump->compress == compress_auto) {
			if (buflen >= pg->len) {
				buflen = pg->len;
				buf = pg->buf;
				flags &= ~DUMP_DH_COMPRESSED;
			}
		}
	} else {
		buflen = pg->len;
		buf = pg->buf;
	}
	pd.offset = htodump64(be, dataoff);
	pd.size = htodump32(be, buflen);
	pd.flags = htodump32(be, flags);
	pd.page_flags = htodump64(be, 0);

	pdidx = bitmap_index(bitmap2, pfn) - start_pdidx;
	if (write_chunk(pgkdump->f, pdoff + pdidx * sizeof pd,
			&pd, sizeof pd, "page desc"))
		return TEST_ERR;

	if (write_chunk(pgkdump->f, dataoff, buf, buflen, "page data"))
		return TEST_ERR;
	dataoff += buflen;

	return TEST_OK;
}

static int
writedata(FILE *f)
{
	struct page_data_kdump pgkdump;
	struct page_data pg;
	unsigned long bmp_blocks1;
	int rc;

	if (!data_file)
		return writeheader(f);

	pgkdump.f = f;
	pgkdump.addr = 0;
	pgkdump.flags = 0;
	pgkdump.cbuf = NULL;
	pgkdump.cbufsz = 0;

#if USE_LZO
	if (lzo_init() != LZO_E_OK) {
		fprintf(stderr, "lzo_init() failed\n");
		return TEST_ERR;
	}
	pgkdump.lzo_wrkmem = (lzo_bytep) malloc(LZO1X_1_MEM_COMPRESS);
	if (!pgkdump.lzo_wrkmem) {
		perror("Cannot allocate LZO work memory");
		return TEST_ERR;
	}
#endif

	if (!bitmap_blocks) {
		bmp_blocks1 =
			(((max_mapnr + 7) / 8) + block_size - 1) / block_size;
		bitmap_blocks = bmp_blocks1 * 2;
	} else
		bmp_blocks1 = bitmap_blocks / 2;

	pdoff = (1 + sub_hdr_size + bitmap_blocks) * block_size;
	dataoff = pdoff;

	printf("Creating page bitmap\n");

	bitmap1 = calloc(bmp_blocks1, block_size);
	if (!bitmap1) {
		perror("Cannot allocate 1st bitmap");
		rc = TEST_ERR;
		goto out_wrkmem;
	}

	bitmap2 = calloc(bmp_blocks1, block_size);
	if (!bitmap2) {
		perror("Cannot allocate 2nd bitmap");
		rc = TEST_ERR;
		goto out_bitmap1;
	}

	pg.endian = be;
	pg.priv = &pgkdump;
	pg.parse_hdr = parseheader;
	pg.write_page = markpage;

	rc = process_data(&pg, data_file);
	if (rc != TEST_OK)
		goto out_bitmap2;

	rc = writeheader(f);
	if (rc != TEST_OK)
		goto out_bitmap2;

	if (write_chunk(f, (1 + sub_hdr_size) * block_size,
			bitmap1, block_size * bmp_blocks1, "1st bitmap")) {
		rc = TEST_ERR;
		goto out_bitmap2;
	}
	if (write_chunk(f, (1 + sub_hdr_size + bmp_blocks1) * block_size,
			bitmap2, block_size * bmp_blocks1, "2nd bitmap")) {
		rc = TEST_ERR;
		goto out_bitmap2;
	}

	printf("Creating page data\n");

	if (split)
		start_pdidx = bitmap_index(bitmap2, start_pfn);
	dataoff += block_size - (dataoff - 1) % block_size - 1;

	pg.endian = be;
	pg.priv = &pgkdump;
	pg.parse_hdr = parseheader;
	pg.write_page = writepage;

	rc = process_data(&pg, data_file);

	if (pgkdump.cbuf)
		free(pgkdump.cbuf);
 out_bitmap2:
	free(bitmap2);
 out_bitmap1:
	free(bitmap1);
 out_wrkmem:
#if USE_LZO
	free(pgkdump.lzo_wrkmem);
#endif

	return rc;
}

static int
writedump(FILE *f)
{
	int rc;

	if (flattened) {
		struct makedumpfile_header hdr = {
			.signature = MDF_SIGNATURE,
			.type = htobe64(flattened_type),
			.version = htobe64(flattened_version),
		};
		size_t remain;

		if (fwrite(&hdr, sizeof hdr, 1, f) != 1) {
			perror("flattened header");
			return TEST_ERR;
		}
		remain = MDF_HEADER_SIZE - sizeof hdr;
		while (remain--) {
			if (putc(0, f) != 0) {
				perror("flattened header padding");
				return TEST_ERR;
			}
		}
	}

	rc = writedata(f);
	if (rc != 0)
		return rc;

	if (flattened) {
		struct makedumpfile_data_header hdr = {
			.offset = htobe64(MDF_OFFSET_END_FLAG),
			.buf_size = htobe64(MDF_OFFSET_END_FLAG),
		};
		if (fwrite(&hdr, sizeof hdr, 1, f) != 1) {
			perror("end segment header");
			return TEST_ERR;
		}
	}

	return 0;
}

static int
create_file(const char *name)
{
	FILE *f;
	int rc;

	f = fopen(name, "w");
	if (!f) {
		perror("Cannot create output");
		return TEST_ERR;
	}

	rc = writedump(f);
	if (fclose(f) != 0) {
		perror("Error closing output");
		rc = TEST_ERR;
	}

	return rc;
}

static int
setup_arch(void)
{
	if (!strcmp(arch_name, "x86_64")) {
		be = data_le;
		writeheader = writeheader_64;
	} else if (!strcmp(arch_name, "ia32")) {
		be = data_le;
		writeheader = writeheader_32;
	} else if (!strcmp(arch_name, "ppc64") ||
		   !strcmp(arch_name, "s390x")) {
		be = data_be;
		writeheader = writeheader_64;
	} else {
		fprintf(stderr, "Unknown architecture: %s\n", arch_name);
		return -1;
	}

	return 0;
}

int
main(int argc, char **argv)
{
	int rc;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <dump>\n", argv[0]);
		return TEST_ERR;
	}

	rc = set_default_params();
	if (rc != TEST_OK) {
		perror("Cannot set default params");
		return rc;
	}

	rc = parse_params_file(&params, stdin);
	if (rc != TEST_OK)
		return rc;

	rc = setup_arch();
	if (rc != TEST_OK)
		return rc;

	if (vmcoreinfo_file) {
		vmcoreinfo = slurp(vmcoreinfo_file);
		if (vmcoreinfo == NULL)
			return TEST_ERR;
	}

	if (note_file) {
		notes = slurp(note_file);
		if (notes == NULL)
			return TEST_ERR;
	}

	if (eraseinfo_file) {
		eraseinfo = slurp(eraseinfo_file);
		if (eraseinfo == NULL)
			return TEST_ERR;
	}

	rc = create_file(argv[1]);
	if (rc != TEST_OK)
		return rc;

	return TEST_OK;
}
