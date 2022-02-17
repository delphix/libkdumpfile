/** @internal @file src/addrxlat/opt.c
 * @brief Option parsing.
 */
/* Copyright (C) 2016 Petr Tesarik <ptesarik@suse.com>

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
#include <string.h>

#include "addrxlat-priv.h"

/** Check if a character is a POSIX white space.
 * @param c  Character to check.
 *
 * We are not using @c isspace here, because the library function may
 * be called in a strange locale, and the parsing should not really
 * depend on locale and this function is less overhead then messing around
 * with the C library locale...
 * Note that this code does not make any assumptions about system
 * character set, because it checks each character individually. Leave
 * possible optimizations to the C compiler.
 */
static inline int
is_posix_space(int c)
{
	return (c == ' ' || c == '\f' || c == '\n' ||
		c == '\r' || c == '\t' || c == '\v');
}

/** Check if a character is a POSIX digit.
 * @param c  Character to check.
 *
 * See @ref is_posix_space for an explanation why the standard call
 * is not used here.
 */
static inline int
is_posix_digit(int c)
{
	return (c == '0' || c == '1' || c == '2' || c == '3' || c == '4' ||
		c == '5' || c == '6' || c == '7' || c == '8' || c == '9');
}

/** Check if a character is a POSIX lowercase character.
 * @param c  Character to check.
 *
 * See @ref is_posix_space for an explanation why the standard call
 * is not used here.
 */
static inline int
is_posix_lower(int c)
{
	return (c == 'a' || c == 'b' || c == 'c' || c == 'd' || c == 'e' ||
		c == 'f' || c == 'g' || c == 'h' || c == 'i' || c == 'j' ||
		c == 'k' || c == 'l' || c == 'm' || c == 'n' || c == 'o' ||
		c == 'p' || c == 'q' || c == 'r' || c == 's' || c == 't' ||
		c == 'u' || c == 'v' || c == 'w' || c == 'x' || c == 'y' ||
		c == 'z');
}

/** Check if a character is a POSIX uppercase character.
 * @param c  Character to check.
 *
 * See @ref is_posix_space for an explanation why the standard call
 * is not used here.
 */
static inline int
is_posix_upper(int c)
{
	return (c == 'A' || c == 'B' || c == 'C' || c == 'D' || c == 'E' ||
		c == 'F' || c == 'G' || c == 'H' || c == 'I' || c == 'J' ||
		c == 'K' || c == 'L' || c == 'M' || c == 'N' || c == 'O' ||
		c == 'P' || c == 'Q' || c == 'R' || c == 'S' || c == 'T' ||
		c == 'U' || c == 'V' || c == 'W' || c == 'X' || c == 'Y' ||
		c == 'Z');
}

/** Check if a character is a POSIX alphabetic character.
 * @param c  Character to check.
 *
 * See @ref is_posix_space for an explanation why the standard call
 * is not used here.
 */
static inline int
is_posix_alpha(int c)
{
	return is_posix_upper(c) || is_posix_lower(c);
}

/** Check if a character is a POSIX alphanumeric character.
 * @param c  Character to check.
 *
 * See @ref is_posix_space for an explanation why the standard call
 * is not used here.
 * Note that this code does not make any assumptions about system
 * character set, because it checks each character individually. Leave
 * optimization to the C compiler.
 */
static inline int
is_posix_alnum(int c)
{
	return is_posix_alpha(c) || is_posix_digit(c);
}

/** Convert an address space string to its enumeration value.
 * @param str     String to be converted.
 * @param endptr  On output, address of the first invalid character.
 * @returns       Address space (or @ref ADDRXLAT_NOADDR on failure).
 *
 * An address space can be specified as:
 *   - one of the @ref addrxlat_addr_t symbols with the @c ADDRXLAT_
 *     prefix stripped (case insensitive), or
 *   - a numeric value.
 */
static addrxlat_addrspace_t
strtoas(const char *str, char **endptr)
{
	const char *p;

	if (is_posix_digit(*str))
		return strtoul(str, endptr, 0);

	p = str;
	while (is_posix_alnum(*p))
		++p;
	*endptr = (char*)p;	/* Optimistic assumption... */

	switch (p - str) {
	case 6:
		if (!strncasecmp(str, "KVADDR", 6))
			return ADDRXLAT_KVADDR;
		break;

	case 9:
		if (!strncasecmp(str, "KPHYSADDR", 9))
			return ADDRXLAT_KPHYSADDR;
		break;

	case 12:
		if (!strncasecmp(str, "MACHPHYSADDR", 12))
			return ADDRXLAT_MACHPHYSADDR;
		break;
	}

	*endptr = (char*)str;
	return ADDRXLAT_NOADDR;
}

/** Option description. */
struct optdesc {
	enum optidx idx;	/**< Option index */
	const char name[];	/**< Option name */
};

/** Define an option without repeating its name. */
#define DEF(name)				\
	{ { OPT_ ## name, }, #name }

/** Option table terminator. */
#define END					\
	{ { OPT_NUM } }

/** Six-character options. */
static const struct {
	struct optdesc opt;
	char name[7];
} opt6[] = {
	DEF(levels),
	END
};

/** Seven-character options. */
static const struct {
	struct optdesc opt;
	char name[8];
} opt7[] = {
	DEF(rootpgt),
	END
};

/** Eight-character options. */
static const struct {
	struct optdesc opt;
	char name[9];
} opt8[] = {
	DEF(pagesize),
	DEF(xen_xlat),
	END
};

/** Nine-character options. */
static const struct {
	struct optdesc opt;
	char name[10];
} opt9[] = {
	DEF(phys_base),
	END
};

/** Eleven-character options. */
static const struct {
	struct optdesc opt;
	char name[12];
} opt11[] = {
	DEF(xen_p2m_mfn),
	END
};

#define DEFPTR(len)						\
	[len] = { &opt ## len[0].opt, sizeof(opt ## len[0]) }

static const struct {
	const struct optdesc *opt;
	size_t elemsz;
} options[] = {
	DEFPTR(6),
	DEFPTR(7),
	DEFPTR(8),
	DEFPTR(9),
	DEFPTR(11),
};

/** Result of parsing a single value. */
typedef enum {
	PARSE_OK,		/**< Success. */
	PARSE_UNKNOWN,		/**< Unknown option. */
	PARSE_NOVAL,		/**< Missing value. */
	PARSE_BADVAL,		/**< Invalid value. */
}  parse_status;

#define NAME(name)	[OPT_ ## name] = #name

/** Human-readable option names. */
static const char optnames[OPT_NUM][16] = {
	NAME(levels),
	NAME(pagesize),
	NAME(phys_base),
	NAME(rootpgt),
	NAME(xen_p2m_mfn),
	NAME(xen_xlat),
};

/** Parse a boolean option value.
 * @param str[in]   String value.
 * @param var[out]  Output variable.
 * @returns         Error status.
 */
static parse_status
parse_bool(const char *str, bool *var)
{
	char *endp;

	if (!str ||
	    !strcasecmp(str, "yes") ||
	    !strcasecmp(str, "true")) {
		*var = true;
	} else if (!strcasecmp(str, "no") ||
		   !strcasecmp(str, "false")) {
		*var = false;
	} else {
		*var = !!strtol(str, &endp, 0);
		if (!*str || *endp)
			return PARSE_BADVAL;
	}

	return PARSE_OK;
}

/** Parse a number option value.
 * @param str[in]   String value.
 * @param var[out]  Output variable.
 * @returns         Error status.
 */
static parse_status
parse_number(const char *str, long *var)
{
	char *endp;

	if (!str)
		return PARSE_NOVAL;

	*var = strtol(str, &endp, 0);
	if (!*str || *endp)
		return PARSE_BADVAL;

	return PARSE_OK;
}

/** Parse an address option value.
 * @param str[in]   String value.
 * @param var[out]  Output variable.
 * @returns         Error status.
 */
static parse_status
parse_addr(const char *str, addrxlat_addr_t *var)
{
	char *endp;

	if (!str)
		return PARSE_NOVAL;

	*var = strtoull(str, &endp, 0);
	if (!*str || *endp)
		return PARSE_BADVAL;

	return PARSE_OK;
}

/** Parse a full address option value.
 * @param str[in]   String value.
 * @param var[out]  Output variable.
 * @returns         Error status.
 */
static parse_status
parse_fulladdr(const char *str, addrxlat_fulladdr_t *var)
{
	char *endp;

	if (!str)
		return PARSE_NOVAL;

	var->as = strtoas(str, &endp);
	if (*str == ':' || *endp != ':')
		return PARSE_BADVAL;

	str = endp + 1;
	var->addr = strtoull(str, &endp, 0);
	if (!*str || *endp)
		return PARSE_BADVAL;

	return PARSE_OK;
}

/** Parse a single option value.
 * @param popt   Parsed options.
 * @param opt    Option index.
 * @param val    Value.
 * @returns      Error status.
 */
static parse_status
parse_val(struct parsed_opts *popt, enum optidx opt, const char *val)
{
	switch (opt) {
	case OPT_levels:
		return parse_number(val, &popt->levels);

	case OPT_pagesize:
		return parse_number(val, &popt->pagesize);

	case OPT_phys_base:
		return parse_addr(val, &popt->phys_base);

	case OPT_rootpgt:
		return parse_fulladdr(val, &popt->rootpgt);

	case OPT_xen_p2m_mfn:
		return parse_number(val, &popt->xen_p2m_mfn);

	case OPT_xen_xlat:
		return parse_bool(val, &popt->xen_xlat);

	default:
		return PARSE_UNKNOWN;
	}
}

/** Parse a single option.
 * @param ctx    Translation context.
 * @param opt    Option index.
 * @param val    Option value.
 * @param err    Parsing error code.
 * @returns      Error status.
 */
static addrxlat_status
parse_error(addrxlat_ctx_t *ctx, enum optidx opt,
	    const char *val, parse_status err)
{
	switch (err) {
	case PARSE_UNKNOWN:
		return set_error(ctx, ADDRXLAT_ERR_NOTIMPL,
				 "Unknown option: %u", (unsigned) opt);

	case PARSE_NOVAL:
		return set_error(ctx, ADDRXLAT_ERR_INVALID,
				 "Missing value for option '%s'",
				 optnames[opt]);

	case PARSE_BADVAL:
		return set_error(ctx, ADDRXLAT_ERR_INVALID,
				 "'%s' is not a valid value for option '%s'",
				 val, optnames[opt]);

	default:
		return set_error(ctx, ADDRXLAT_ERR_NOTIMPL,
				 "Unknown parser error");
	}
}

/** Parse a single option.
 * @param popt   Parsed options.
 * @param ctx    Translation context (for error handling).
 * @param key    Option name.
 * @param klen   Name length.
 * @param val    Value.
 * @returns      Error status.
 */
static addrxlat_status
parse_opt(struct parsed_opts *popt, addrxlat_ctx_t *ctx,
	  const char *key, size_t klen, const char *val)
{
	const struct optdesc *opt;
	parse_status status;

	if (klen >= ARRAY_SIZE(options))
		goto err;

	opt = options[klen].opt;
	if (!opt)
		goto err;

	while (opt->idx != OPT_NUM) {
		if (!strcasecmp(key, opt->name)) {
			status = parse_val(popt, opt->idx, val);
			if (status != PARSE_OK)
				return parse_error(ctx, opt->idx, val, status);
			popt->isset[opt->idx] = true;
			return ADDRXLAT_OK;
		}

		opt = (void*)(opt) + options[klen].elemsz;
	}

 err:
	return set_error(ctx, ADDRXLAT_ERR_NOTIMPL, "Unknown option: %s", key);
}

/** OS map option parser.
 * @param popt  Parsed options.
 * @param ctx   Translation context (for error handling).
 * @param opts  Option string.
 * @returns     Error status.
 */
addrxlat_status
parse_opts(struct parsed_opts *popt, addrxlat_ctx_t *ctx, const char *opts)
{
	enum optidx idx;
	const char *p;
	char *dst;
	addrxlat_status status;

	for (idx = 0; idx < OPT_NUM; ++idx)
		popt->isset[idx] = false;

	if (!opts)
		return ADDRXLAT_OK;

	popt->buf = realloc(NULL, strlen(opts) + 1);
	if (!popt->buf)
		return set_error(ctx, ADDRXLAT_ERR_NOMEM,
				 "Cannot allocate options");

	p = opts;
	while (is_posix_space(*p))
		++p;

	dst = popt->buf;
	while (*p) {
		char quot, *key, *val;
		size_t keylen;

		quot = 0;
		key = dst;
		val = NULL;
		while (*p) {
			if (quot) {
				if (*p == quot)
					quot = 0;
				else
					*dst++ = *p;
			} else if (*p == '\'' || *p == '"')
				quot = *p;
			else if (is_posix_space(*p))
				break;
			else if (*p == '=') {
				*dst++ = '\0';
				val = dst;
			} else
				*dst++ = *p;
			++p;
		}
		if (quot) {
			status = set_error(ctx, ADDRXLAT_ERR_INVALID,
					   "Unterminated %s quotes",
					   quot == '"' ? "double" : "single");
			goto err;
		}

		*dst++ = '\0';

		keylen = (val ? val - key : dst - key) - 1;
		status = parse_opt(popt, ctx, key, keylen, val);
		if (status != ADDRXLAT_OK)
			goto err;

		while (is_posix_space(*p))
			++p;
	}

	return ADDRXLAT_OK;

 err:
	free(popt->buf);
	return status;
}
