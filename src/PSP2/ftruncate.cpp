#include <errno.h>
#include <sys/fcntl.h>
#include <stdio.h>
#include <psp2/types.h>

#include "sysdeps.h"

#ifdef __cplusplus
extern "C" {
#endif
int truncate(const char *path, off_t length);
#ifdef __cplusplus
}
#endif


#define __PSP_FILENO_MAX 1024

#define __PSP_IS_FD_VALID(FD) \
		( (FD >= 0) && (FD < __PSP_FILENO_MAX) && (__psp_descriptormap[FD] != NULL) )

typedef enum {
	__PSP_DESCRIPTOR_TYPE_FILE  ,
	__PSP_DESCRIPTOR_TYPE_PIPE ,
	__PSP_DESCRIPTOR_TYPE_SOCKET,
	__PSP_DESCRIPTOR_TYPE_TTY
} __psp_fdman_fd_types;

typedef struct {
	char * filename;
	uint8     type;
	uint32    sce_descriptor;
	uint32    flags;
	uint32    ref_count;
} __psp_descriptormap_type;

//extern __psp_descriptormap_type *__psp_descriptormap[__PSP_FILENO_MAX];


int ftruncate(int fd, off_t length)
{

	return -1;
}
