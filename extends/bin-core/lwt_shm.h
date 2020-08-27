#ifndef __BIN_LWT_SHM_H__
#define __BIN_LWT_SHM_H__

#include "rtdef.h"

struct _bmem_type
{
    rt_uint32_t start;
    rt_uint32_t size;
    rt_list_t   block_node;
};

struct bmem_block
{
    rt_list_t   node;
    uint16_t    is_used;//block_is_used
    uint16_t    size;//block_size
};

struct shm_mem
{
    rt_list_t   app_node;
    rt_uint32_t addr;
    rt_uint32_t size;
};

struct shm_mem_ref
{
    rt_uint32_t addr;
    rt_uint32_t ref;
};

struct shm_app
{
    rt_list_t   mem_node;
    rt_uint32_t lwp_addr;
    struct shm_mem_ref* ref_tab;
    rt_uint16_t ref_tab_size;
    rt_uint16_t use_num;
};

struct shm_relation
{
    struct shm_app  app;
    rt_list_t       app_node;
    struct shm_mem  mem;
    rt_list_t       mem_node;
};

struct _lwt_shm
{
    rt_uint32_t* shm_addr;
    rt_uint32_t shm_size;

    struct shm_relation *relation_tab;
    struct shm_app      *app_tab;
    struct shm_mem      *mem_tab;

    rt_list_t   relation;
    rt_list_t   app;
    rt_list_t   mem;

    //lock

};
typedef struct _lwt_shm* lwt_shm_t;

#endif