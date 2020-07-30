#include <errno.h>
#include <fcntl.h> // AT_FDCWD
#include <sys/stat.h> // utimensat

_Bool is_readonly(const char* path)
{
	static const struct timespec atime[2] = {
		{ 0, UTIME_NOW },
		{ 0, UTIME_OMIT },
	};
	return utimensat(AT_FDCWD, path, atime, 0) != 0 && errno == EROFS;
}

#if 0
// Doesn't work on directories :-(
#include <unistd.h>
#include <stdio.h>
_Bool is_readonly(const char* path)
{
	errno = 0;
	int fd = open(path, O_RDWR);
	if (fd == -1)
	{
		perror("");
		return errno == EROFS;
	}
	close(fd);
	return 0;
}
#endif

int main(int argc, char* argv[])
{
	for (int i = 1; i != argc; ++i) {
		if (!is_readonly(argv[i])) {
			return 1;
		}
	}
	return 0;
}
