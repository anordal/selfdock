#include <stdlib.h> //getenv
#include <sys/stat.h> //mkdir
#include <unistd.h> //chown
#include <errno.h> //errno
#include <stdio.h> //snprintf
#include <fcntl.h> //open
#include "instancefile.h"
#include "usualsuspects.h"

static int mkdir_uid(const char *path, mode_t mode, uid_t uid)
{
	if (mkdir(path, mode)) {
		return -1; // Fail.
	}
	if (chown(path, uid, -1)) {
		// Bad place to fail.
		rmdir(path);
		return -1; // Fail.
	}
	return 0;
}

static int create_runtime_dir(const char *path, uid_t uid)
{
	if (mkdir("/run/user", 0755) && errno != EEXIST) {
		return -1; // Fail.
	}
	if (mkdir_uid(path, 0700, uid) && errno != EEXIST) {
		return -1; // Fail.
	}
	return 0; // Success.
}

static void set_runtime_dir_create_if_necessary(uid_t uid, _Bool necessary)
{
	char path[21]; // /run/user/4294967295
	if (sizeof(path) <= (size_t)snprintf(path, sizeof(path), "/run/user/%u", uid)) {
		errno = ENAMETOOLONG;
		return;
	}

	struct stat exists;
	if (0 != stat(path, &exists) && errno == ENOENT) {
		if (necessary && create_runtime_dir(path, uid)) {
			return; // Fail.
		}
	}

	// setenv copies the buffer so it can be freed.
	setenv("XDG_RUNTIME_DIR", path, 0);
}

static const char *get_runtime_dir_create_if_necessary(uid_t uid, _Bool necessary)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	if (dir) {
		return dir;
	}
	set_runtime_dir_create_if_necessary(uid, necessary);
	return getenv("XDG_RUNTIME_DIR");
}

int instancefile_open(const char *name, uid_t uid)
{
	const char *rundir = get_runtime_dir_create_if_necessary(uid, ~0);
	if (!rundir) {
		fputs("Please set XDG_RUNTIME_DIR\n", stderr);
		return -1;
	}
	char path[PATH_MAX];
	size_t bytes = snprintf(path, PATH_MAX, "%s/selfdock", rundir);
	if (bytes >= sizeof(path)) {
		errno = ENAMETOOLONG;
		perror(path);
		return -1;
	}
	if (mkdir_uid(path, 0700, uid) && errno != EEXIST) {
		perror(path);
		return -1;
	}

	bytes += snprintf(path+bytes, PATH_MAX-bytes, "/%s", name);
	if (bytes >= sizeof(path)) {
		errno = ENAMETOOLONG;
		perror(path);
		return -1;
	}
	int fd = open(path, O_CREAT|O_EXCL|O_WRONLY, 0400);
	if (fd == -1) {
		// TODO: if EEXIST, check with /proc/pid
		perror(path);
		return fd;
	}
	if (chown(path, uid, -1)) {
		unlink(path);
	}
	return fd;
}

int fd_write_eintr_retry(int fd, void *data, size_t len)
{
	// retry loop
	for (;;) {
		ssize_t bytes = write(fd, data, len);
		if ((size_t)bytes == len) {
			return 0; // success
		}
		if (bytes == -1 && errno != EINTR) {
			return -1; // fail
		}
		lseek(fd, 0, SEEK_SET);
	}
}

pid_t instancefile_get(const char *name, uid_t uid)
{
	const char *rundir = get_runtime_dir_create_if_necessary(uid, 0);
	char path[PATH_MAX];
	if (PATH_MAX <= snprintf(path, PATH_MAX, "%s/selfdock/%s", rundir, name)) {
		errno = ENAMETOOLONG;
		perror(path);
		return -1;
	}

	FILE *fp = fopen(path, "r");
	if (!fp) {
		perror(path);
		return -1;
	}

	pid_t pid;
	if (1 != fread(&pid, sizeof(pid), 1, fp)) {
		pid = -1;
	}

	fclose(fp);
	return pid;
}

void instancefile_rm(const char *name)
{
	const char *rundir = getenv("XDG_RUNTIME_DIR");
	char path[PATH_MAX];
	sprintf(path, "%s/selfdock/%s", rundir, name);
	if (unlink(path)) {
		perror(path);
	}
}
