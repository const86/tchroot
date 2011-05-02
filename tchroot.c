/*
 * Copyright (C) 2011 Constantin Baranov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <unistd.h>

static void wait_exit(pid_t pid)
{
	sigset_t any, orig;
	sigfillset(&any);
	sigprocmask(SIG_BLOCK, &any, &orig);

	struct sigaction child = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT
	};
	sigemptyset(&child.sa_mask);
	sigaction(SIGCHLD, &child, NULL);

	for (;;) {
		siginfo_t si;
		if (sigwaitinfo(&any, &si) == -1)
			continue;

		if (si.si_signo == SIGCHLD && si.si_pid == pid) {
			sigprocmask(SIG_SETMASK, &orig, NULL);

			switch (si.si_code) {
			case CLD_KILLED:
			case CLD_DUMPED:
				raise(si.si_status);
				raise(SIGKILL);
			default:
				exit(si.si_status);
			}
		}

		if (si.si_pid == 0 || getpid() != 1)
			sigqueue(pid, si.si_signo, si.si_value);
	}
}

static int process_config(FILE *config)
{
	char *line = NULL;
	size_t len = 0;
	bool first = true;
	int res = 1;

	while (getline(&line, &len, config) != -1) {
		const char *const delim = " \t\n";
		char *tmp;

		char *from = strtok_r(line, delim, &tmp);
		char *to = strtok_r(NULL, delim, &tmp);

		if (from == NULL || to == NULL ||
			strtok_r(NULL, delim, &tmp) != NULL) {
			fputs("config: Line must contain exactly two paths\n",
				stderr);
			goto out;
		}

		if (from[0] != '/' || to[0] != '/') {
			fputs("config: Paths must be absolute\n", stderr);
			goto out;
		}

		if (first) {
			if (strcmp(to, "/") != 0) {
				fputs("config: First target must be /\n",
					stderr);
				goto out;
			}

			if (chdir(from) == -1) {
				perror("config:chdir");
				goto out;
			}

			first = false;
			continue;
		}

		if (mount(from, to + 1, NULL, MS_BIND, NULL) == -1) {
			fprintf(stderr, "config:bind:%s: %s\n",
				to, strerror(errno));
			goto out;
		}
	}

	res = 0;

out:
	free(line);
	fclose(config);
	return res;
}

static void cleanup_ns(void)
{
	struct mp {
		struct mp *next;
		char path[];
	};

	char root[PATH_MAX];
	if (!getcwd(root, sizeof(root)))
		perror("init:getcwd");

	const size_t root_len = strlen(root);
	FILE *mtab = fopen("/proc/mounts", "r");
	char *line = NULL;
	size_t len = 0;
	struct mp *head = NULL;

	while (getline(&line, &len, mtab) != -1) {
		const char *const delim = " \n";
		char *tmp;
		strtok_r(line, delim, &tmp);
		char *mp = strtok_r(NULL, delim, &tmp);
		char *type = strtok_r(NULL, delim, &tmp);

		if (mp == NULL || type == NULL)
			continue;

		if (strcmp(type, "proc") != 0 &&
			strncmp(root, mp, root_len) == 0 &&
			mp[root_len] == '/')
			continue;

		size_t mp_len = strlen(mp);
		struct mp *mpi = malloc(sizeof(*mpi) + mp_len + 1);
		if (mpi) {
			memcpy(mpi->path, mp, mp_len + 1);
			mpi->next = head;
			head = mpi;
		}
	}

	free(line);
	fclose(mtab);

	while (head) {
		struct mp *mp = head;
		head = head->next;

		umount2(mp->path, 0);
		free(mp);
	}
}

struct task {
	char *const *args;
	FILE *config;
	char wd[PATH_MAX];
};

static int init(void *arg)
{
	const struct task *task = arg;

	if (process_config(task->config))
		goto fail;

	cleanup_ns();

	uid_t uid = getuid();

	if (chroot(".") == -1) {
		perror("init:chroot");
		goto fail;
	}

	if (chdir("/") == -1) {
		perror("init:chdir/");
		goto fail;
	}

	if (mount("proc", "/proc", "proc",
			MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == -1) {
		perror("init:proc");
		goto fail;
	}

	pid_t pid = fork();
	switch (pid) {

	case -1:
		perror("init:fork");
		break;

	case 0:
		if (seteuid(uid) == -1) {
			perror("child:seteuid");
			break;
		}

		if (chdir(task->wd))
			;

		execvp(task->args[0], task->args);
		perror("child:exec");
		break;

	default:

		wait_exit(pid);
		break;

	}

fail:
	return 127;
}

int main(int argc, char **argv)
{
	char stack[sysconf(_SC_PAGESIZE)];
	struct task task;

	setlocale(LC_ALL, "");

	if (argc < 3) {
		fprintf(stderr, "Usage: %s NAME COMMAND [ARG]...\n", argv[0]);
		goto fail;
	}

	task.args = argv + 2;

	if (!getcwd(task.wd, sizeof(task.wd))) {
		perror("getcwd");
		goto fail;
	}

	if (chdir("/etc/tchroot") == -1) {
		perror("config:chdir");
		goto fail;
	}

	task.config = fopen(argv[1], "r");
	if (task.config == NULL) {
		perror("config:fopen");
		goto fail;
	}

	pid_t pid;
	if (clone(init, stack + sizeof(stack),
			CLONE_IO | CLONE_PARENT_SETTID | CLONE_UNTRACED |
			CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS | SIGCHLD,
			&task, &pid, NULL, NULL) == -1) {
		perror("clone");
		goto fail_file;
	}

	fclose(task.config);
	wait_exit(pid);

fail_file:
	fclose(task.config);
fail:
	return 127;
}
