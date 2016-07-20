/*
 * Copyright 2015 Andreas Nordal
 *
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file,
 * you can obtain one at https://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE
#include <narg.h>
#include <libintl.h>
#include <sched.h> //unshare
#include <errno.h>
#include <fcntl.h> //open
#include <sys/stat.h> //mkdir
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h> //getenv
#include <stdio.h>
#include <string.h>

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#ifndef PATH_MAX
#	define PATH_MAX 1024
#endif
#ifndef ROOTOVERLAY
#	error ROOTOVERLAY not defined
#endif

// Try to conform or give way to existing exit status conventions
#define EXIT_NAME_IN_USE 123 //self-defined
#define EXIT_CANNOT      124 //self-defined
#define EXIT_UNTESTABLE  125 //inapplicable convention (git-bisect)
#define EXIT_CMDNOTEXEC  126 //applicable convention
#define EXIT_CMDNOTFOUND 127 //applicable convention

#ifndef NO_REINVENT_MKDTEMP
#	define mkdtemp(x) selfdock_mkdtemp(x)
// Workaround for selfdock-in-selfdock – if you are affected, the tests won't
// pass. Looks like a combination of unshare() and mkdtemp() triggers a glibc
// assertion in fork():
//
// ../sysdeps/nptl/fork.c:141: __libc_fork: Assertion
// `THREAD_GETMEM (self, tid) != ppid' failed.
//
// Looks related to:
// https://lists.linuxcontainers.org/pipermail/lxc-devel/2013-April/004156.html
//
static char *selfdock_mkdtemp(char *path) {
	int fd = open("/dev/random", O_RDONLY);
	if (fd == -1) {
		goto fail_random;
	}

	uint32_t entropy;
	if (read(fd, &entropy, sizeof(entropy)) != sizeof(entropy)) {
		close(fd);
		goto fail_random;
	}
	close(fd);

	char *apply_entropy = path + strlen(path) - 6; // mkdtemp spec
	for (uint32_t giveup = entropy + 1000; ; ++entropy) {
		// simplified Base64
		for (int i=0; i<6; ++i) {
			apply_entropy[i] = 63 + ((entropy >> (6*i)) & 0x3F);
		}
		if (0 == mkdir(path, 0700)) {
			return path; // success
		}
		if (errno == EEXIST && entropy != giveup) {
			continue;
		}
		goto fail;
	}

fail_random:
	strcpy(path, "/dev/random");
fail:
	return NULL;
}
#endif // NO_REINVENT_MKDTEMP

// Drop euid privileges temporarily for the directory creation.
// It would be a security problem to expose directory creation
// with elevated privileges if the user can influence the path.
static int mkdir_as_realuser(char *path)
{
	uid_t effective = geteuid();
	if (seteuid(getuid())) return -1;

	int ret = (NULL != mkdtemp(path)) ? 0 : -1;

	if (seteuid(effective)) return -1;
	return ret;
}

// Use overlayfs to emulate a readonly bind mount when required,
// as indicated by a non-NULL @upper argumment, else bind mount.
// Errmsg printing taken care of.
static int mount_bind(const char* lower, const char* upper, const char* dst)
{
	const char *errmsg;
	if (upper) {
		char opt[PATH_MAX];
		if (PATH_MAX <= snprintf(opt, PATH_MAX, "lowerdir=%s:%s", upper, lower)) {
			errmsg = strerror(ENAMETOOLONG);
			goto fail;
		}
		if (mount("none", dst, "overlay", MS_RDONLY, opt)) {
			errmsg = (errno == ENODEV)
				? "Overlayfs (Linux 3.18 or newer) needed. "
				"Sorry, this requirement is a Linux specific workaround "
				"for lack of a readonly bindmount."
				: strerror(errno)
			;
			goto fail;
		}
	} else {
		if (mount(lower, dst, NULL, MS_BIND, NULL)) {
			errmsg = strerror(errno);
			goto fail;
		}
	}
	return 0;

fail:
	fprintf(stderr, "bindmount «%s» → «%s»: %s\n", lower, dst, errmsg);
	return -1;
}

// assume no (writable) on failure
static _Bool is_readonly_rootfs(const char *rootdir)
{
	char path[PATH_MAX];
	if (PATH_MAX <= snprintf(path, PATH_MAX, "%s/bin/sh", rootdir)) {
		return 0;
	}

	int fd = open(path, O_RDWR);
	if (fd != -1) {
		close(fd);
		return 0;
	}

	return (errno == EROFS);
}

static int tmpfs_mkdir_dirname(
	const char *dirname, struct stat *statbuf,
	int flag, const char *opt)
{
	if (0 == stat(dirname, statbuf)) {
		return mount("none", dirname, "tmpfs", flag, opt);
	}

	char *basename = strrchr(dirname, '/');
	if (!basename || basename == dirname) {
		// stat set errno
		return -1; //fail
	}

	*basename = '\0';
	int ret = tmpfs_mkdir_dirname(dirname, statbuf, flag, opt);
	*basename = '/';

	if (mkdir(dirname, 0755)) {
		ret = -1;
	}
	return ret;
}

static int tmpfs_mkdir(const char *path, int flag, const char *opt)
{
	char dirname[PATH_MAX];
	if (PATH_MAX <= snprintf(dirname, PATH_MAX, "%s", path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	struct stat statbuf;
	return tmpfs_mkdir_dirname(dirname, &statbuf, flag, opt);
}

static int child(
	const char *oldroot, const char *newroot,
	const char *rundir, char *const argv[])
{
	// CLONE_NEWNS must be applied in the child process
	if (unshare(CLONE_NEWNS)) {
		perror("unshare");
		return EXIT_CANNOT;
	}
	// The containing mountpoint must be marked private. How this is supposed
	// to be done is AFAIK undocumented, but this trick, recursing from root,
	// was taken from http://sourceforge.net/p/fuse/mailman/message/24957287/
	if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL)) {
		fprintf(stderr, "Failed to mark all mounts private: %s\n", strerror(errno));
		return EXIT_CANNOT;
	}

	// Use overlayfs sparingly; it has a maximum stacking depth.
	_Bool already_readonly = is_readonly_rootfs(oldroot);

	if (mount_bind(
		oldroot,
		already_readonly ? NULL : TOSTRING(ROOTOVERLAY),
		newroot))
	{
		return EXIT_CANNOT;
	}

	if (chdir(newroot)) {
		fprintf(stderr, "chdir(%s): %s\n", newroot, strerror(errno));
		return EXIT_CANNOT;
	}

	if (already_readonly) {
		if (mount_bind(TOSTRING(ROOTOVERLAY) "/dev", NULL, "dev")) {
			return EXIT_CANNOT;
		}
	}

	if (chroot(".")) {
		fprintf(stderr, "chroot(%s): %s\n", newroot, strerror(errno));
		return EXIT_CANNOT;
	}

	if (mount("none", "proc", "proc", MS_RDONLY|MS_NOEXEC, NULL)) {
		perror("mount proc");
		return EXIT_CANNOT;
	}

	if (tmpfs_mkdir(rundir, MS_NOEXEC, "size=2M")) {
		perror(rundir);
		return EXIT_CANNOT;
	}
	uid_t uid = getuid();
	if (chown(rundir, uid, -1)) {
		perror(rundir);
		return EXIT_CANNOT;
	}

	// Drop effective uid
	if (setuid(uid)) {
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

const char *narg_strerror(unsigned err) {
	switch (err) {
		case NARG_ENOSUCHOPTION: return "No such option";
		case NARG_EMISSINGPARAM: return "Missing parameter";
		case NARG_EUNEXPECTEDPARAM: return "Unexpected parameter";
		case NARG_EILSEQ: return strerror(EILSEQ);
	}
	return "Unknown error";
}

void help(const struct narg_optspec *optv, struct narg_optparam *ansv, unsigned optc) {
	unsigned width = narg_terminalwidth(stdout);
	flockfile(stdout);
	narg_printopt_unlocked(stdout, width, optv, ansv, optc, dgettext, NULL, 2);
	funlockfile(stdout);
}

int main(int argc, char *argv[])
{
	enum {
		OPT_HELP,
		OPT_ROOT,
		OPT_IGN,
		OPT_MAX
	};
	static const struct narg_optspec optv[OPT_MAX] = {
		{"h","help",NULL,"Show help text"},
		{"r","root"," DIR","Directory to use as root filesystem"},
		{NULL,"",&narg_metavar.ignore_rest,"Don\'t interpret further arguments as options"}
	};
	struct narg_optparam ansv[OPT_MAX] = {
		[OPT_HELP] = {0, NULL},
		[OPT_ROOT] = {1, (const char*[]){"/"}},
		[OPT_IGN] = {0, NULL}
	};
	struct narg_result nargres = narg_findopt(argv, optv, ansv, OPT_MAX, 1, 2);
	if (nargres.err) {
		fprintf(stderr, "%s: %s\n", argv[nargres.arg], narg_strerror(nargres.err));
		return EXIT_CANNOT;
	}

	if (ansv[OPT_HELP].paramc) {
		ansv[OPT_HELP].paramc = 0;
		help(optv, ansv, OPT_MAX);
		return EXIT_SUCCESS;
	}

	if (argc - nargres.arg < 2) {
		printf(
			"Usage: %s [OPTIONS] run argv\n"
			"       %s run [OPTIONS] -- argv\n"
			, argv[0], argv[0]);
		return EXIT_CANNOT;
	}
	argv += nargres.arg;
	if (0 != strcmp(argv[0], "run")) {
		fputs("Action must be \"run\" for now. TODO: build|enter\n", stderr);
		return EXIT_CANNOT;
	}
	argv += 1;

	// CLONE_NEWPID must be applied in the parent process
	if (unshare(CLONE_NEWPID)) {
		perror("unshare");
		return EXIT_CANNOT;
	}

	const char *rundir = getenv("XDG_RUNTIME_DIR");
	if (!rundir) {
		fputs("Please set XDG_RUNTIME_DIR.\n", stderr);
		return EXIT_CANNOT;
	}
	char newroot[48];
	if (sizeof(newroot) <= (size_t)snprintf(newroot, sizeof(newroot), "%s/selfdock_XXXXXX", rundir)) {
		fprintf(stderr, "%s/selfdock_XXXXXX: %s\n", rundir, strerror(ENAMETOOLONG));
		return EXIT_CANNOT;
	}

	if (mkdir_as_realuser(newroot)) {
		perror(newroot);
		return (errno == EEXIST)
			? EXIT_NAME_IN_USE
			: EXIT_CANNOT;
	}

	int ret = EXIT_CANNOT;
	pid_t pid = fork();
	if (pid == -1) {
		fprintf(stderr, "fork(): %s\n", strerror(errno));
	} else if (pid == 0) {
		return child(ansv[OPT_ROOT].paramv[0], newroot, rundir, argv);
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
	if (rmdir(newroot)) {
		perror(newroot);
	}
	return ret;
}
