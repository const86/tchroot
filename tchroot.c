/*
 * Copyright (C) 2010 Constantin Baranov
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

#define _XOPEN_SOURCE 700
#define _BSD_SOURCE

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <locale.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s ROOT COMMAND [ARG]...\n", argv[0]);
		return 1;
	}

	int rc;
	const char *const root = argv[1];
	char *const *const args = argv + 2;

	setlocale(LC_ALL, "");

	uid_t uid = getuid();
	long pwsize = sysconf(_SC_GETPW_R_SIZE_MAX);

	if (pwsize == -1)
		return perror("sysconf(_SC_GETPW_R_SIZE_MAX)"), 1;

	// 1. Get current user name.
	char buf1[pwsize];
	struct passwd pws1, *pw1;
	rc = getpwuid_r(uid, &pws1, buf1, sizeof(buf1), &pw1);

	if (!rc && !pw1)
		rc = ENOENT;

	if (rc) {
		fprintf(stderr, "getpwuid(%lu): %s\n",
			(unsigned long)uid, strerror(rc));
		return 1;
	}

	// 2. Save current working directory.
	char wd[PATH_MAX];
	if (!getcwd(wd, sizeof(wd)))
		return perror("getcwd()"), 1;

	// 3. Actually change root.
	if (chroot(root) == -1) {
		fprintf(stderr, "chroot %s: %s\n",
			root, strerror(errno));
		return 1;
	}

	if (chdir("/") == -1)
		return perror("chdir /"), 1;

	// 4. Determine UID/GID in new environment.
	char buf2[pwsize];
	struct passwd pws2, *pw2;
	rc = getpwnam_r(pw1->pw_name, &pws2, buf2, sizeof(buf2), &pw2);

	if (!rc && !pw2)
		rc = ENOENT;

	if (rc) {
		fprintf(stderr, "chroot:getpwuid(%lu): %s\n",
			(unsigned long)uid, strerror(rc));
		return 1;
	}

	// 5. Set supplementary GIDs, GID and UID.
	if (initgroups(pw2->pw_name, pw2->pw_gid) == -1)
		return perror("initgroups()"), 1;

	if (setegid(pw2->pw_gid) == -1)
		return perror("setegid()"), 1;

	if (seteuid(pw2->pw_uid) == -1)
		return perror("seteuid()"), 1;

	// 6. Try to restore working directory. It's OK to fail.
	if (chdir(wd))
		;

	// 7. Run.
	execvp(args[0], args);
	fprintf(stderr, "exec %s: %s\n", args[0], strerror(errno));
	return 127;
}
