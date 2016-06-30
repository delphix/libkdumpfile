/* Virtual to physical translation.
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

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <kdumpfile.h>

#include "testutil.h"

static int
vtop(kdump_ctx *ctx, unsigned long long vaddr)
{
	kdump_paddr_t paddr;
	kdump_status res;

	res = kdump_vtop_init(ctx);
	if (res != kdump_ok) {
		fprintf(stderr, "Cannot initialize vtop: %s\n",
			kdump_err_str(ctx));
		return TEST_FAIL;
	}

	res = kdump_vtop(ctx, vaddr, &paddr);
	if (res != kdump_ok) {
		fprintf(stderr, "VTOP translation failed: %s\n",
			kdump_err_str(ctx));
		return TEST_FAIL;
	}

	printf("0x%llx\n", (unsigned long long)paddr);

	return TEST_OK;
}

static int
vtop_fd(int fd, unsigned long long vaddr)
{
	kdump_ctx *ctx;
	kdump_status res;
	int rc;

	ctx = kdump_new();
	if (!ctx) {
		perror("Cannot initialize dump context");
		return TEST_ERR;
	}

	res = kdump_set_fd(ctx, fd);
	if (res != kdump_ok) {
		fprintf(stderr, "Cannot open dump: %s\n", kdump_err_str(ctx));
		rc = TEST_ERR;
	} else
		rc = vtop(ctx, vaddr);

	kdump_free(ctx);
	return rc;
}

int
main(int argc, char **argv)
{
	unsigned long long addr;
	char *endp;
	int fd;
	int rc;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <dump> <vaddr>\n", argv[0]);
		return TEST_ERR;
	}

	addr = strtoull(argv[2], &endp, 0);
	if (*endp) {
		fprintf(stderr, "Invalid address: %s", argv[2]);
		return TEST_ERR;
	}

	fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		perror("open dump");
		return TEST_ERR;
	}

	rc = vtop_fd(fd, addr);

	if (close(fd) < 0) {
		perror("close dump");
		rc = TEST_ERR;
	}

	return rc;
}