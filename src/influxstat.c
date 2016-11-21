/*-
 * Copyright (c) 2016 Lasse Karstensen
 * All rights reserved.
 *
 * Author: Lasse Karstensen <lasse.karstensen@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Export VSC ("varnishstat") to InfluxDB over UDP.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#include "vdef.h"
#include "vapi/vsl.h"
#include "vapi/vsm.h"
#include "vapi/voptget.h"
#include "vapi/vsc.h"
#include "vas.h"
#include "foreign/vut.h"
#include "vcs.h"
#include "vsb.h"

#include "vtim.h"

#define NANO 1000000000

/* Global variables are the bestest. */
int sock = -1;
char hostname[64];

struct vsb *msg, *tags;

static int
send_message(void)
{
	int written = write(sock, VSB_data(msg), VSB_len(msg));
	if (written < 0) {
		if (errno != 111) {  /* Ignore connection refused */
			fprintf(stderr, "write error: (%i) %s\n", errno, strerror(errno));
		}
	}
	// fprintf(stderr, "[%i] %s", written, VSB_data(msg));
	return(written);
}

static int
do_influx_cb(void *priv, const struct VSC_point * const pt)
{
	char timestamp[20];
	(void)priv;

	if (pt == NULL)
		return (0);

	AZ(strcmp(pt->desc->ctype, "uint64_t"));
	uint64_t val = *(const volatile uint64_t*)pt->ptr;

	time_t now = time(NULL);
	(void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", localtime(&now));

	VSB_clear(msg);
	if (pt->section->fantom->type[0])
		VSB_printf(msg, "%s.%s", pt->section->fantom->type,
		    pt->desc->name);
	else if (pt->section->fantom->ident[0])
		VSB_printf(msg, "%s.%s", pt->section->fantom->ident,
		    pt->desc->name);
	else
		WRONG("Unknown field type");
	VSB_printf(msg, ",hostname=%s", hostname);
	VSB_printf(msg, ",%s", VSB_data(tags));
	VSB_printf(msg, " value=%ju", (uintmax_t)val);
	VSB_printf(msg, " %ju\n", time(NULL)*NANO);
	VSB_finish(msg);

	send_message();
	VTIM_sleep(0.001);  /* Smear the packet rate slightly */

	return (0);
}


static void
do_influx_udp(struct VSM_data *vd, const long interval)
{
	while (1) {
		(void)VSC_Iter(vd, NULL, do_influx_cb, NULL);
		VTIM_sleep(interval);
	}
}

/* List function is verbatim from varnishstat.c. */
static int
do_list_cb(void *priv, const struct VSC_point * const pt)
{
	int i;
	const struct VSC_section * sec;

	(void)priv;

	if (pt == NULL)
		return (0);

	sec = pt->section;
	i = 0;
	if (strcmp(sec->fantom->type, ""))
		i += fprintf(stderr, "%s.", sec->fantom->type);
	if (strcmp(sec->fantom->ident, ""))
		i += fprintf(stderr, "%s.", sec->fantom->ident);
	i += fprintf(stderr, "%s", pt->desc->name);
	if (i < 30)
		fprintf(stderr, "%*s", i - 30, "");
	fprintf(stderr, " %s\n", pt->desc->sdesc);
	return (0);
}


static void
list_fields(struct VSM_data *vd)
{
	fprintf(stderr, "influxstat -f option fields:\n");
	fprintf(stderr, "Field name                     Description\n");
	fprintf(stderr, "----------                     -----------\n");

	(void)VSC_Iter(vd, NULL, do_list_cb, NULL);
}


static int
create_socket(const char *host, const char *port)
{
	struct addrinfo hints, *result, *rp;
	int sfd, s;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;

	if (getenv("NO_IPV6") == NULL)
		hints.ai_family = AF_INET;

        s = getaddrinfo(host, port, &hints, &result);
        if (s != 0) {
           fprintf(stderr, "ERROR: Unable to resolve: %s\n", gai_strerror(s));
           exit(EXIT_FAILURE);
        }

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype,
		    rp->ai_protocol);
		if (sfd == -1)
		   continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
		   break;                  /* Success */

		close(sfd);
	}
	if (rp == NULL) {               /* No address succeeded */
		fprintf(stderr, "ERROR: Could not create socket\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(result);

	return(sfd);
}



/*--------------------------------------------------------------------*/

static void
usage(void)
{
#define FMT "    %-28s # %s\n"
	fprintf(stderr, "Usage: influxstat "
	    "[lV] [-f field] [-t seconds|<off>] [-i seconds] "
	    VSC_n_USAGE " hostname port\n");
	fprintf(stderr, FMT, "-f field", "Field inclusion glob");
	fprintf(stderr, FMT, "",
	    "If it starts with '^' it is used as an exclusion list.");
	fprintf(stderr, FMT, "-i seconds", "Update interval (default: 10s)");
	fprintf(stderr, FMT, "-V", "Display the version number and exit.");
	fprintf(stderr, FMT, "-l",
	    "Lists the available fields to use with the -f option.");
	fprintf(stderr, FMT, "-n varnish_name",
	    "The varnishd instance to get logs from.");
	fprintf(stderr, FMT, "-N filename",
	    "Filename of a stale VSM instance.");
	fprintf(stderr, FMT, "-t seconds|<off>",
	    "Timeout before returning error on initial VSM connection.");
	fprintf(stderr, FMT, "-V", "Display the version number and exit.");
#undef FMT
	exit(1);
}

int
main(int argc, char * const *argv)
{
	struct VSM_data *vd;
	double t_arg = 5.0, t_start = NAN;
	long interval = 10.0;
	int f_list = 0;
	int i, c;

	vd = VSM_New();
	AN(vd);

	while ((c = getopt(argc, argv, VSC_ARGS "1f:i:lVxjt:")) != -1) {
		switch (c) {
		case 'i':
			interval = atol(optarg);  /* seconds */
			break;
		case 'l':
			f_list = 1;
			break;
		case 't':
			if (!strcasecmp(optarg, "off"))
				t_arg = -1.;
			else {
				t_arg = atoi(optarg);
				if (isnan(t_arg)) {
					fprintf(stderr, "-t: Syntax error");
					exit(1);
				}
				if (t_arg < 0.) {
					fprintf(stderr, "-t: Range error");
					exit(1);
				}
			}
			break;
		case 'V':
			VCS_Message("influxstat");
			exit(0);
		default:
			if (VSC_Arg(vd, c, optarg) > 0)
				break;
			fprintf(stderr, "%s\n", VSM_Error(vd));
			usage();
		}
	}

	if (optind != argc-2)
		usage();

	sock = create_socket(argv[optind], argv[optind+1]);
	AN(sock);

	while (1) {
		i = VSM_Open(vd);
		if (!i)
			break;
		if (isnan(t_start) && t_arg > 0.) {
			fprintf(stderr, "Can't open log -"
			    " retrying for %.0f seconds\n", t_arg);
			t_start = VTIM_real();
		}
		if (t_arg <= 0.)
			break;
		if (VTIM_real() - t_start > t_arg)
			break;
		VSM_ResetError(vd);
		VTIM_sleep(0.5);
	}

	if (f_list) {
		list_fields(vd);
	}

	AZ(gethostname(hostname, sizeof(hostname)));

	msg = VSB_new(NULL, NULL, 2048, 0);
	// fprintf(stderr, "Attempting to resolve %s:%s\n", argv[optind], argv[optind+1]);

	/* Make a startup argument for the tags. Along with -f it should make
	 * this nice and configurable. */
	tags = VSB_new_auto();
	VSB_printf(tags, "service=varnish");
	VSB_finish(tags);

	do_influx_udp(vd, interval);
	exit(EXIT_SUCCESS);
}
