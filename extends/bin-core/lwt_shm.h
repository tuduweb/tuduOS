#ifndef __BIN_LWT_SHM_H__
#define __BIN_LWT_SHM_H__

#include "rtdef.h"

struct _lwt_shm
{
    rt_uint32_t* shm_addr;
    rt_uint32_t shm_size;

    rt_list_t   app;
    rt_list_t   relation;
};

typedef struct _lwt_shm* lwt_shm_t;

#endif