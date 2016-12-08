#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define ARRAY_SIZE(x) sizeof(x)/sizeof(x[0])

#ifndef PATH_MAX
#	define PATH_MAX 1024
#endif
