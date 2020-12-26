/*
 * Copyright 2015-2020 Andreas Nordal
 *
 * This Source Code Form is subject to the terms of the
 * Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file,
 * you can obtain one at https://mozilla.org/MPL/2.0/.
 */

#define _GNU_SOURCE
#include <narg.h>
#include <libintl.h>
#include <sched.h> //clone
#include <errno.h>
#include <fcntl.h> //open, AT_FDCWD
#include <sys/stat.h> //mkdir, utimensat
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h> //getenv
#include <stdio.h>
#include <string.h>
#include <sys/mman.h> //mmap

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])

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

static volatile int sigrid;
static void take_signal(int sig)
{
	sigrid = sig;
}

static int start_handling_signals()
{
	struct sigaction sa;
	sa.sa_handler = take_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	static const int handleable_signals[] = {
		SIGHUP,
		SIGINT,
		SIGUSR1,
		SIGUSR2,
		SIGPIPE,
		SIGTERM
	};
	for (unsigned i=0; i != ARRAY_SIZE(handleable_signals); ++i) {
		int sig = handleable_signals[i];
		if (sigaction(sig, &sa, NULL)) {
			return sig;
		}
	}
	return 0;
}

static _Bool is_suid(const char *path)
{
	struct stat info;
	return 0 == stat(path, &info) && info.st_mode & S_ISUID;
}

// is what execvp calls a pathname && exists && is (a symlink to) a directory
static _Bool isdir_pathname(const char *path)
{
	struct stat info;
	return strchr(path, '/') && 0 == lstat(path, &info) && S_ISDIR(info.st_mode);
}

// Updates atime if writable → only suitable when not supposed to.
static _Bool check_erofs(const char* path)
{
	static const struct timespec atime[2] = {
		{ 0, UTIME_NOW },
		{ 0, UTIME_OMIT },
	};
	return utimensat(AT_FDCWD, path, atime, 0) != 0 && errno == EROFS;
}

static int mount_bind_rw(const char* src, const char* dst)
{
	if (mount(src, dst, NULL, MS_BIND, NULL)) {
		fprintf(stderr, "bindmount «%s» → «%s»: %s\n", src, dst, strerror(errno));
		return -1;
	}
	return 0;
}

static int mount_bind_ro(const char* src, const char* dst)
{
	if (mount_bind_rw(src, dst)) {
		return -1;
	}

	const char *errmsg;
	if (mount(NULL, dst, NULL, MS_REMOUNT | MS_BIND | MS_RDONLY, NULL)) {
		errmsg = strerror(errno);
		goto fail;
	}
	if (!check_erofs(dst)) {
		errmsg = "Still not readonly! This is supported from Linux 2.6.26, see mount(2).";
		goto fail;
	}
	return 0;

fail:
	fprintf(stderr, "remount,bind,ro %s: %s\n", dst, errmsg);
	return -1;
}

struct child_args {
	_Bool permit_writable;
	const char *oldroot;
	const char *cd;
	struct narg_optparam *map, *vol;
	char *const *argv;
};
static int child(void *arg)
{
	const struct child_args *self = arg;

	// The containing mountpoint must be marked private. How this is supposed
	// to be done is AFAIK undocumented, but this trick, recursing from root,
	// was taken from http://sourceforge.net/p/fuse/mailman/message/24957287/
	if (mount(NULL, "/", NULL, MS_PRIVATE|MS_REC, NULL)) {
		fprintf(stderr, "Failed to mark all mounts private: %s\n", strerror(errno));
		return EXIT_CANNOT;
	}

	static const char newroot[] = TOSTRING(ROOTOVERLAY) "/dev/empty";

	if (self->permit_writable
		? mount_bind_rw(self->oldroot, newroot)
		: mount_bind_ro(self->oldroot, newroot))
	{
		return EXIT_CANNOT;
	}

	if (chdir(newroot)) {
		fprintf(stderr, "chdir: %s: %s\n", newroot, strerror(errno));
		return EXIT_CANNOT;
	}

	if (mount_bind_ro(TOSTRING(ROOTOVERLAY) "/dev", "dev")) {
		return EXIT_CANNOT;
	}

	for (unsigned i=0; i < self->map->paramc; i += 2) {
		if (mount_bind_ro(self->map->paramv[i], self->map->paramv[i+1]+1)) {
			return EXIT_CANNOT;
		}
	}

	for (unsigned i=0; i < self->vol->paramc; i += 2) {
		if (mount_bind_rw(self->vol->paramv[i], self->vol->paramv[i+1]+1)) {
			return EXIT_CANNOT;
		}
	}

	if (chroot(".")) {
		fprintf(stderr, "chroot: %s: %s\n", newroot, strerror(errno));
		return EXIT_CANNOT;
	}

	if (mount("none", "proc", "proc", MS_NOEXEC, NULL)) {
		perror("mount proc");
		return EXIT_CANNOT;
	}

	if (mount("none", "dev/pts", "devpts", MS_NOEXEC, NULL)) {
		perror("mount devpts");
		return EXIT_CANNOT;
	}

	if (mount("none", "tmp", "tmpfs", MS_NOEXEC, "size=2M")) {
		perror("tmp");
		return EXIT_CANNOT;
	}
	uid_t uid = getuid();
	if (chmod("tmp", 0777)) {
		perror("tmp");
		return EXIT_CANNOT;
	}

	// Drop effective uid
	if (setuid(uid)) {
		perror("setuid");
		return EXIT_CANNOT;
	}

	if (chdir(self->cd)) {
		perror(self->cd);
		return EXIT_CANNOT;
	}

	execvp(self->argv[0], self->argv);

	// fail
	int errval = errno;
	if (errval == EACCES && isdir_pathname(self->argv[0])) {
		errval = EISDIR;
	}
	fprintf(stderr, "exec: %s: %s\n", self->argv[0], strerror(errval));
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
		OPT_CD,
		OPT_MAP,
		OPT_VOL,
		OPT_ENV,
		OPT_ENV_RM,
		OPT_IGN,
		OPT_MAX
	};
	static const struct narg_optspec optv[OPT_MAX] = {
		{"h","help",NULL,"Show help text"},
		{"r","rootfs"," DIR","Directory to use as root filesystem"},
		{"C", NULL, "DIR","Working directory"},
		{"m","map"," SRC DST","Mount SRC to DST read-only"},
		{"v","vol"," SRC DST","Mount SRC to DST read-write"},
		{"e","env"," ENV val","Set environment variable ENV to val"},
		{"E", NULL, "ENV","Unset environment variable ENV"},
		{NULL,"",&narg_metavar.ignore_rest,"Don\'t interpret further arguments as options"}
	};
	struct narg_optparam ansv[OPT_MAX] = {
		[OPT_HELP] = {0, NULL},
		[OPT_ROOT] = {1, (const char*[]){"/"}},
		[OPT_CD ] = {1, (const char*[]){"/"}},
		[OPT_MAP] = {0, NULL},
		[OPT_VOL] = {0, NULL},
		[OPT_ENV] = {0, NULL},
		[OPT_ENV_RM] = {0, NULL},
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

	for (unsigned i=0; i < ansv[OPT_MAP].paramc; i += 2) {
		if (ansv[OPT_MAP].paramv[i+1][0] != '/')
		{
			fprintf(stderr, "%s destinations must be absolute\n", "--map");
			return EXIT_CANNOT;
		}
	}
	for (unsigned i=0; i < ansv[OPT_VOL].paramc; i += 2) {
		if (ansv[OPT_VOL].paramv[i+1][0] != '/')
		{
			fprintf(stderr, "%s destinations must be absolute\n", "--vol");
			return EXIT_CANNOT;
		}
	}
	for (unsigned i=0; i < ansv[OPT_ENV].paramc; i += 2) {
		const char *env = ansv[OPT_ENV].paramv[i];
		const char *val = ansv[OPT_ENV].paramv[i+1];
		if (setenv(env, val, ~0)) {
			fprintf(stderr, "setenv %s=%s: %s\n", env, val, strerror(errno));
			return EXIT_CANNOT;
		}
	}
	for (unsigned i=0; i < ansv[OPT_ENV_RM].paramc; i += 1) {
		const char *env = ansv[OPT_ENV_RM].paramv[i];
		if (unsetenv(env)) {
			fprintf(stderr, "unsetenv %s: %s\n", env, strerror(errno));
			return EXIT_CANNOT;
		}
	}

	// Apparently, this is just the stack size before exec.
	// Apparently, 3 4K pages are needed to print to stderr (1 for stdout).
	const unsigned initial_stack_size = 16*4096;
	char *stack = mmap(
		NULL, initial_stack_size,
		PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_ANONYMOUS,
		-1, 0
	);
	if (stack == MAP_FAILED) {
		perror("mmap");
		return EXIT_CANNOT;
	}

	struct child_args barnebok;
	barnebok.oldroot = ansv[OPT_ROOT].paramv[0];
	barnebok.cd  = ansv[OPT_CD].paramv[0];
	barnebok.map = ansv+OPT_MAP;
	barnebok.vol = ansv+OPT_VOL;
	barnebok.argv = argv + nargres.arg + 1; // += optional + positional args

	if (argc - nargres.arg < 2) {
		printf(
			"Usage: %s run|build [OPTIONS] argv\n"
			, argv[0]);
		return EXIT_CANNOT;
	}
	if (0 == strcmp(argv[nargres.arg], "run")) {
		barnebok.permit_writable = 0;
	} else if (0 == strcmp(argv[nargres.arg], "build")) {
		barnebok.permit_writable = ~0;
	} else {
		fputs("Action must be \"run\" or \"build\" for now. TODO: enter\n", stderr);
		return EXIT_CANNOT;
	}

	int sigfail = start_handling_signals();
	if (sigfail) {
		fprintf(stderr, "sigaction(sig=%d): %s\n", sigfail, strerror(errno));
		return EXIT_CANNOT;
	}

	pid_t pid = clone(
		child, stack + initial_stack_size,
		CLONE_VFORK|CLONE_VM|CLONE_NEWNS|CLONE_NEWPID|SIGCHLD,
		&barnebok
	);

	int ret = EXIT_CANNOT;
	if (pid == -1) {
		if (!is_suid(argv[0])) {
			fprintf(stderr, "No suid. Please check that the program is installed correctly.\n");
		} else {
			fprintf(stderr, "clone: %s\n", strerror(errno));
		}
	} else do {
		int status;
		if (-1 == wait(&status)) {
			if (errno == EINTR) {
				int deliver = sigrid;
				if (kill(pid, deliver)) {
					fprintf(stderr, "Failed to deliver signal %s\n", strsignal(deliver));
				}
				continue;
			}
			perror("wait");
		} else if (WIFEXITED(status)) {
			ret = WEXITSTATUS(status);
		} else if (WIFSIGNALED(status)) {
			psignal(WTERMSIG(status), barnebok.argv[0]);
		}
	} while (0);
	return ret;
}
