/*****************************************************************************\
 *  xtconsumer.c - Define optional plugin for Cray network statistics
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <pty.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/slurmctld/slurmctld_plugstack.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for SLURM authentication) and <method> is a
 * description of how this plugin satisfies that application.  SLURM will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char	plugin_name[]	= "Slurmctld xtconsumer plugin";
const char	plugin_type[]	= "slurmctld/xtconsumer";
const uint32_t	plugin_version	= SLURM_VERSION_NUMBER;

static bool thread_running = false;
static bool thread_shutdown = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t msg_thread_id;
static pid_t child_pid = 0;

static void *_msg_thread(void *no_data)
{
	ssize_t len, offset, out_len;
	int fd_master = -1, fd_slave = -1, fd_out = -1;
	char buf[512];
	char *argv[2] = {"xtconsumer", NULL };
	char *xtconsumer_path, *xtconsumer_output;
	char *plugin_params, *sep, *outpath2free = NULL;

	if (openpty(&fd_master, &fd_slave, NULL, NULL, NULL) < 0) {
		error("%s: pipe: %m", plugin_name);
		pthread_exit((void *) 0);
	}

	plugin_params = slurm_get_slurmctld_plug_params();
	if (plugin_params) {
		xtconsumer_path = strcasestr(plugin_params, "XtconsumerPath=");
		if (xtconsumer_path) {
			xtconsumer_path += 15;
			sep = strchr(xtconsumer_path, ',');
			if (sep)
				sep[0] = '\0';
		} else {
			xtconsumer_path ="/opt/cray/hss/default/bin/xtconsumer";
		}
		xtconsumer_output = strcasestr(plugin_params,
					       "XtconsumerOutput=");
		if (xtconsumer_output) {
			xtconsumer_output += 17;
			sep = strchr(xtconsumer_output, ',');
			if (sep)
				sep[0] = '\0';
		} else {
			outpath2free = get_extra_conf_path("xtconsumer.out");
			xtconsumer_output = outpath2free;
		}
	}

// FIXME: Disable output buffering
	child_pid = fork();
	if (child_pid == 0) {
		int fd = open("/dev/null", O_RDONLY);
		dup2(fd, 0);		/* stdin from /dev/null */
		dup2(fd_slave, 1);	/* stdout to pipe */
		dup2(fd_slave, 2);	/* stderr to pipe */
		close(fd_master);
		execvp(xtconsumer_path, argv);
		error("%s: execvp(%s): %m", xtconsumer_path, plugin_name);
	} else if (child_pid < 0) {
		error("%s: fork(%s): %m", xtconsumer_path, plugin_name);
		child_pid = 0;
		pthread_exit((void *) 0);
	} else {
		close(fd_slave);

		fd_out = open(xtconsumer_output, O_RDWR | O_CREAT | O_APPEND,
			      0644);
		if (fd_out < 0) {
			error("%s: open(%s): %m",  plugin_name,
			      xtconsumer_output);
			xfree(outpath2free);
			xfree(plugin_params);
			pthread_exit((void *) 0);
		}

		while (1) {
			len = read(fd_master, buf, sizeof(buf));
			if (len == 0)
				break;
			if (len < 0) {
				if ((errno == EAGAIN) ||
				    (errno == EWOULDBLOCK) ||
				    (errno == EINTR))
					continue;
				error("%s: read(%s): %m", xtconsumer_path,
				      plugin_name);
				break;
			}

			offset = 0;
			while (offset < len) {
				out_len = write(fd_out, buf + offset,
						len - offset);
				if (out_len > 0) {
					offset += out_len;
				} else if ((errno == EAGAIN) ||
					   (errno == EWOULDBLOCK) ||
					   (errno == EINTR)) {
					continue;
				} else {
					error("%s: write(%s): %m",
					      plugin_name, xtconsumer_output);
					break;
				}
			}
			/* FIXME: also write to syslog */
		}
		if (fd_out >= 0)
			close(fd_out);
		close(fd_master);

	}
	xfree(outpath2free);
	xfree(plugin_params);

	pthread_exit((void *) 0);
	return NULL;
}

extern int init(void)
{
	pthread_attr_t thread_attr_msg;

	slurm_mutex_lock(&thread_flag_mutex);
	if (thread_running) {
		error("nonstop thread already running");
		slurm_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	slurm_attr_init(&thread_attr_msg);
	if (pthread_create(&msg_thread_id, &thread_attr_msg,
	                   _msg_thread, NULL)) {
		error("pthread_create %m");
	} else {
		info("%s loaded", plugin_name);
		thread_running = true;
	}
	slurm_attr_destroy(&thread_attr_msg);
	slurm_mutex_unlock(&thread_flag_mutex);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	int i;

	slurm_mutex_lock(&thread_flag_mutex);
	if (child_pid > 0) {
		kill(child_pid, 15);
		for (i = 0; i < 10; i++) {
			(void) usleep(100000);
			if ((kill(child_pid, 0) == -1) && (errno == ESRCH))
				break;
		}
		if (i >= 10)
			kill(child_pid, 9);
		child_pid = 0;
	}
	if (msg_thread_id) {
		thread_shutdown = true;
		pthread_join(msg_thread_id, NULL);
		msg_thread_id = 0;
		thread_running = false;
		thread_shutdown = false;
	}
	slurm_mutex_unlock(&thread_flag_mutex);

	return SLURM_SUCCESS;
}
