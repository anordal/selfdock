// Reproduces this glibc assertion in fork():
//
// ../sysdeps/nptl/fork.c:141: __libc_fork: Assertion
// `THREAD_GETMEM (self, tid) != ppid' failed.
//
// Looks related to:
// https://lists.linuxcontainers.org/pipermail/lxc-devel/2013-April/004156.html

#define _GNU_SOURCE //unshare
#include <sched.h> //unshare
#include <unistd.h>
#include <stdio.h>
#include <sys/wait.h>

int main() {
	for (unsigned rounds=2; rounds; --rounds) {
		if (unshare(CLONE_NEWPID)) {
			perror("This program must be run as root\nunshare");
			return -1;
		}
		getpid();
		pid_t pid = fork();

		if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
			return status;
		}
	}
	puts("Did not fail!");
	return 0;
}
