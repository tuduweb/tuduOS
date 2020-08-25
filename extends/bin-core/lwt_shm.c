#include <rthw.h>
#include <rtthread.h>
#include "lwt_shm.h"
/**
 * 共享内存
 */

struct _lwt_shm lwt_shm;
#define LWT_SHM_SIZE 0x4000
rt_err_t lwt_shm_init()
{
    rt_base_t* shm = RT_KERNEL_MALLOC(LWT_SHM_SIZE);

    rt_memset(&lwt_shm, 0, sizeof(struct _lwt_shm));
    lwt_shm.shm_addr = shm;
    lwt_shm.shm_size = LWT_SHM_SIZE;

    lwt_shm.app.prev = &lwt_shm.app;
    lwt_shm.app.next = &lwt_shm.app;
    lwt_shm.relation.prev = &lwt_shm.relation;
    lwt_shm.relation.next = &lwt_shm.relation;
    
    return RT_EOK;
}

void *lwt_shm_alloc(int size)
{
    return 0;
}

