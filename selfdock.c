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
#include <sched.h> //clone
#include <errno.h>
#include <fcntl.h> //open
#include <sys/stat.h> //mkdir
#include <sys/wait.h>
#include <sys/mount.h>
#include <unistd.h>
#include <stdlib.h> //getenv
#include <stdio.h>
#include <string.h>
#include <sys/mman.h> //mmap
#include "usualsuspects.h"
#include "instancefile.h"

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

// is what execvp calls a pathname && exists && is (a symlink to) a directory
static _Bool isdir_pathname(const char *path)
{
	struct stat info;
	return strchr(path, '/') && 0 == lstat(path, &info) && S_ISDIR(info.st_mode);
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
	if (upper) {
		fprintf(stderr, "bindmount «%s» + «%s» → «%s»: %s\n", lower, upper, dst, errmsg);
	} else {
		fprintf(stderr, "bindmount «%s» → «%s»: %s\n", lower, dst, errmsg);
	}
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

static int enter_ns(char *path, unsigned pos, const char *ns)
{
	strcpy(path+pos, ns);
	int fd = open(path, O_RDONLY);
	if (fd == -1) {
		perror(path);
		return -1;
	}
	int ret = setns(fd, 0);
	close(fd);
	return ret;
}

static int deny_entering_othermans_instance(uid_t uid)
{
	struct stat info;
	if (stat("/proc/1", &info))
	{
		return -1;
	}
	if (info.st_uid != uid)
	{
		return -1;
	}
	return 0;
}

static int exec_or_failhand(char *const *argv);

struct child_args {
	_Bool permit_writable;
	uid_t uid;
	const char *oldroot;
	const char *cd;
	const char *name;
	struct narg_optparam *map, *vol;
	char *const *argv;
};

static int selfdock_enter(const struct child_args *self)
{
	pid_t pid = instancefile_get(self->name, self->uid);
	if (pid == -1) {
		if (errno == ENOENT) {
			fprintf(stderr, "%s: Not running.\n", self->name);
			return EXIT_NAME_IN_USE;
		} else {
			return EXIT_CANNOT;
		}
	}

	if (seteuid(0)) {
		perror("seteuid");
		fputs("This usually means that the program was not installed with suid permission.", stderr);
		return EXIT_CANNOT;
	}
	int ret = EXIT_CANNOT;
	do {
		char path[24]; // /proc/4294967296/{ns/{pid,mnt},cwd}
		unsigned proc_pid_ns = sprintf(path, "/proc/%u/ns/", (unsigned)pid);
		unsigned proc_pid = proc_pid_ns - 3;

		if (enter_ns(path, proc_pid_ns, "pid")) {
			break;
		}
		if (enter_ns(path, proc_pid_ns, "mnt")) {
			break;
		}
		strcpy(path+proc_pid, "cwd");
		if (chroot(path)) {
			perror("chroot");
			break;
		}
		ret = 0;
	} while (0);
	// Drop effective uid
	if (seteuid(self->uid)) {
		ret = EXIT_CANNOT;
	}
	if (ret) {
		return ret;
	}

	if (deny_entering_othermans_instance(self->uid))
	{
		fprintf(stderr, "You do not own this instance: %s\n", self->name);
		return EXIT_CANNOT;
	}
	return exec_or_failhand(self->argv);
}

static int tmpfs_777(const char *path)
{
	if (mount("none", path, "tmpfs", MS_NOEXEC, "size=2M")) {
		return -1;
	}
	if (chmod(path, 0777)) {
		return -1;
	}
	return 0;
}

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

	if (mount_bind(
		self->oldroot,
		self->permit_writable ? NULL : TOSTRING(ROOTOVERLAY),
		newroot))
	{
		return EXIT_CANNOT;
	}

	if (chdir(newroot)) {
		fprintf(stderr, "chdir: %s: %s\n", newroot, strerror(errno));
		return EXIT_CANNOT;
	}

	if (self->permit_writable) {
		if (mount_bind(TOSTRING(ROOTOVERLAY) "/dev", NULL, "dev")) {
			return EXIT_CANNOT;
		}
	}

	for (unsigned i=0; i < self->map->paramc; i += 2) {
		if (mount_bind(
			self->map->paramv[i],
			TOSTRING(ROOTOVERLAY) "/dev/empty",
			self->map->paramv[i+1]+1))
		{
			return EXIT_CANNOT;
		}
	}

	for (unsigned i=0; i < self->vol->paramc; i += 2) {
		if (mount_bind(
			self->vol->paramv[i],
			NULL,
			self->vol->paramv[i+1]+1))
		{
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

	if (tmpfs_777("tmp")) {
		perror("tmp");
		return EXIT_CANNOT;
	}
	if (tmpfs_777("run")) {
		perror("run");
		return EXIT_CANNOT;
	}

	// Drop effective uid
	if (seteuid(self->uid)) {
		// Not reproducible by installing non-suid.
		return EXIT_CANNOT;
	}

	if (chdir(self->cd)) {
		perror(self->cd);
		return EXIT_CANNOT;
	}

	return exec_or_failhand(self->argv);
}

static int exec_or_failhand(char *const *argv)
{
	execvp(argv[0], argv);

	// fail
	int errval = errno;
	if (errval == EACCES && isdir_pathname(argv[0])) {
		errval = EISDIR;
	}
	fprintf(stderr, "exec: %s: %s\n", argv[0], strerror(errval));
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

static int selfdock_run(struct child_args* self)
{
	const unsigned initial_stack_size = 4096;
	char *stack = mmap(
		NULL, initial_stack_size,
		PROT_READ|PROT_WRITE,
		MAP_PRIVATE|MAP_GROWSDOWN|MAP_ANONYMOUS,
		-1, 0
	);
	if (stack == MAP_FAILED) {
		perror("mmap");
		return EXIT_CANNOT;
	}

	int sigfail = start_handling_signals();
	if (sigfail) {
		fprintf(stderr, "sigaction(sig=%d): %s\n", sigfail, strerror(errno));
		return EXIT_CANNOT;
	}

	if (seteuid(0)) {
		perror("seteuid");
		fputs("This usually means that the program was not installed with suid permission.", stderr);
		return EXIT_CANNOT;
	}

	int instance_fd = -1;
	if (self->name) {
		instance_fd = instancefile_open(self->name, self->uid);
		if (instance_fd == -1) {
			seteuid(self->uid);
			if (errno == EEXIST) {
				fprintf(stderr, "%s: Already running.\n", self->name);
				return EXIT_NAME_IN_USE;
			} else {
				return EXIT_CANNOT;
			}
		}
	}
	// Beyond this point, failures must go through cleanup_instance_file.
	int ret = EXIT_CANNOT;

	pid_t pid = clone(
		child, stack + initial_stack_size,
		CLONE_VFORK|CLONE_VM|CLONE_NEWNS|CLONE_NEWPID|SIGCHLD,
		self
	);
	seteuid(self->uid);

	if (pid == -1) {
		perror("clone");
		goto cleanup_instance_file;
	}

	if (instance_fd != -1) {
		if (fd_write_eintr_retry(instance_fd, (void*)&pid, sizeof(pid))) {
			goto cleanup_instance_file;
		}
	}
	close(instance_fd);
	instance_fd = -1;

	do {
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
			psignal(WTERMSIG(status), self->argv[0]);
		}
	} while (0);

cleanup_instance_file:
	if (self->name) {
		if (instance_fd != -1) {
			close(instance_fd);
		}
		instancefile_rm(self->name);
	}
	return ret;
}

static const char *narg_strerror(unsigned err) {
	switch (err) {
		case NARG_ENOSUCHOPTION: return "No such option";
		case NARG_EMISSINGPARAM: return "Missing parameter";
		case NARG_EUNEXPECTEDPARAM: return "Unexpected parameter";
		case NARG_EILSEQ: return strerror(EILSEQ);
	}
	return "Unknown error";
}

static void help(const struct narg_optspec *optv, struct narg_optparam *ansv, unsigned optc) {
	unsigned width = narg_terminalwidth(stdout);
	flockfile(stdout);
	narg_printopt_unlocked(stdout, width, optv, ansv, optc, dgettext, NULL, 2);
	funlockfile(stdout);
}

int main(int argc, char *argv[])
{
	struct child_args barnebok;
	barnebok.uid = getuid();
	if (seteuid(barnebok.uid)) {
		// Not reproducible by installing non-suid.
		return EXIT_CANNOT;
	}

	enum {
		OPT_HELP,
		OPT_ROOT,
		OPT_CD,
		OPT_MAP,
		OPT_VOL,
		OPT_ENV,
		OPT_ENV_RM,
		OPT_NAME,
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
		{"i","instance-name"," NAME","Name of the running instance"},
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
		[OPT_NAME] = {0, NULL},
		[OPT_IGN] = {0, NULL}
	};
	struct narg_result nargres = narg_findopt(argv, optv, ansv, OPT_MAX, 1, 2);
	if (nargres.err) {
		fprintf(stderr, "%s: %s\n", argv[nargres.arg], narg_strerror(nargres.err));
		return EXIT_CANNOT;
	}

	barnebok.oldroot = ansv[OPT_ROOT].paramv[0];
	barnebok.cd      = ansv[OPT_CD  ].paramv[0];
	barnebok.map     = ansv+OPT_MAP;
	barnebok.vol     = ansv+OPT_VOL;
	barnebok.argv    = argv + nargres.arg + 1; // += optional + positional args

	if (ansv[OPT_HELP].paramc) {
		ansv[OPT_HELP].paramc = 0;
		help(optv, ansv, OPT_MAX);
		return EXIT_SUCCESS;
	}
	if (ansv[OPT_NAME].paramc) {
		barnebok.name = ansv[OPT_NAME].paramv[0];
	} else {
		barnebok.name = NULL;
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

	if (argc - nargres.arg >= 2) {
		if (0 == strcmp(argv[nargres.arg], "run")) {
			// Use overlayfs sparingly; it has a maximum stacking depth.
			barnebok.permit_writable = is_readonly_rootfs(barnebok.oldroot);
			return selfdock_run(&barnebok);
		} else if (0 == strcmp(argv[nargres.arg], "build")) {
			barnebok.permit_writable = ~0;
			return selfdock_run(&barnebok);
		} else if (0 == strcmp(argv[nargres.arg], "enter")) {
			return selfdock_enter(&barnebok);
		}
	}
	printf(
		"Usage: %s run|build|enter [OPTIONS] argv\n"
		, argv[0]);
	return EXIT_CANNOT;
}
