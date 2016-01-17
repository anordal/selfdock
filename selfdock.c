/*
 * Copyright 2015 Andreas Nordal
 *
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file,
 * you can obtain one at https://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE
#include <sched.h> //unshare
#include <errno.h>
#include <sys/stat.h> //mkdir
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h> //getenv
#include <stdio.h>
#include <string.h>

#ifndef PATH_MAX
#	define PATH_MAX 1024
#endif

// Try to conform or give way to existing exit status conventions
#define EXIT_NAME_IN_USE 123 //self-defined
#define EXIT_CANNOT      124 //self-defined
#define EXIT_UNTESTABLE  125 //inapplicable convention (git-bisect)
#define EXIT_CMDNOTEXEC  126 //applicable convention
#define EXIT_CMDNOTFOUND 127 //applicable convention

// Drop euid privileges temporarily for the directory creation.
// It would be a security problem to expose directory creation
// with elevated privileges if the user can influence the path.
static int mkdir_as_realuser(const char *path, int mode)
{
	uid_t effective = geteuid();
	if (seteuid(getuid())) return -1;

	int ret = mkdir(path, mode);

	if (seteuid(effective)) return -1;
	return ret;
}

static int mount_root(const char *from, const char *to)
{
	char path[PATH_MAX];
	if (PATH_MAX <= snprintf(path, PATH_MAX, "lowerdir=/usr/local/share/selfdock:%s", from)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	return mount("none", to, "overlay", MS_RDONLY, path);
}

static void nukemounts(const char *root)
{
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "%s/proc", root);
	if (umount(path)) {
		perror(path);
	}

	// TODO: Parse proc/mounts (sort by length) for more mountpoints
}

static int child(const char *fs, char *const argv[])
{
	if (chdir(fs)) {
		fprintf(stderr, "chdir(%s): %s\n", fs, strerror(errno));
		return EXIT_CANNOT;
	}
	if (chroot(".")) {
		fprintf(stderr, "chroot(%s): %s\n", fs, strerror(errno));
		return EXIT_CANNOT;
	}

	if (mount("none", "proc", "proc", MS_RDONLY|MS_NOEXEC, NULL)) {
		perror("mount proc");
		return EXIT_CANNOT;
	}

	// Drop effective uid
	if (setuid(getuid())) {
		perror("setuid");
		return EXIT_CANNOT;
	}

	execvp(argv[0], argv);

	// fail
	int errval = errno;
	fprintf(stderr, "exec(%s): %s\n", argv[0], strerror(errval));
	switch(errval) {
		case ELOOP:
		case ENAMETOOLONG:
		case ENOENT:
		case ENOTDIR:
			return EXIT_CMDNOTFOUND;
		default:
			return EXIT_CMDNOTEXEC;
	}
}

int main(int argc, char *argv[])
{
	int ret = EXIT_CANNOT;

	if (argc < 4) {
		printf("Usage: %s rootdir label argv\n", argv[0]);
		goto fail;
	}

	if (unshare(CLONE_NEWPID)) {
		perror("unshare");
		goto fail;
	}

	const char *rundir = getenv("XDG_RUNTIME_DIR");
	if (!rundir) {
		fputs("Please set XDG_RUNTIME_DIR.\n", stderr);
		goto fail;
	}
	char newroot[64]; //who wants long labels
	if (sizeof(newroot) <= snprintf(newroot, sizeof(newroot), "%s/%s", rundir, argv[2])) {
		fprintf(stderr, "%s/%s: %s\n", rundir, argv[2], strerror(ENAMETOOLONG));
		goto fail;
	}

	if (mkdir_as_realuser(newroot, 0x777)) {
		perror(newroot);
		if (errno == EEXIST) {
			ret = EXIT_NAME_IN_USE;
		}
		goto fail;
	}
	// point of no return without cleanup
	if (mount_root(argv[1], newroot)) {
		if (errno == ENODEV) {
			fprintf(stderr, "Overlayfs (Linux 3.18 or newer) needed. Sorry.\n");
		} else {
			perror(newroot);
		}
		goto cleanup_rmdir;
	}

	pid_t pid = fork();
	if (pid == -1) {
		fprintf(stderr, "fork(): %s\n", strerror(errno));
	} else if (pid == 0) {
		return child(newroot, argv+3);
	} else {
		int status;
		if (-1 == waitpid(pid, &status, 0)) {
			perror("wait");
		} else if (WIFEXITED(status)) {
			ret = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			fprintf(stderr, "%s: killed by signal %d\n", argv[0], WTERMSIG(status));
		}
	}
	nukemounts(newroot);

	if (umount(newroot)) {
		perror(newroot);
		goto fail;
	}
cleanup_rmdir:
	if (rmdir(newroot)) {
		perror(newroot);
	}
fail:
	return ret;
}
