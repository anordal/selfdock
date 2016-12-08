#include <sys/types.h> //uid_t
int instancefile_open(const char *name, uid_t uid);
int fd_write_eintr_retry(int fd, void *data, size_t len);
pid_t instancefile_get(const char *name, uid_t uid);
void instancefile_rm(const char *name);
