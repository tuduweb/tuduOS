#ifndef __BIN_LWT_SHM_H__
#define __BIN_LWT_SHM_H__

#include "rtdef.h"

struct bmem_block
{
    rt_list_t       node;
    rt_uint16_t     is_used;//block_is_used
    rt_uint16_t     len;//block_size
};

#define BLOCK_TAB_SIZE 8

struct bmem_tab
{
    rt_list_t   node;
    rt_uint32_t used_num;
    struct bmem_block tab[BLOCK_TAB_SIZE];
};

struct bmem_typedef
{
    char*       start;
    rt_uint32_t mem_size;
    rt_uint32_t used_mem_size;
    rt_uint32_t used_max_mem_size;
    
    rt_list_t   tab_head;
    rt_list_t   block_node;
};



struct shm_mem
{
    rt_list_t   app_node;
    rt_uint32_t addr;
    rt_uint32_t size;
};
#define SHM_MEM_TAB_SIZE 8
struct shm_mem_tab
{
    rt_list_t node;
    rt_uint32_t used_num;
    struct shm_mem tab[SHM_MEM_TAB_SIZE];
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
    rt_uint16_t use_num;//use mem num
};

#define SHM_APP_TAB_SIZE 8
struct shm_app_tab
{
    rt_list_t node;
    rt_uint32_t used_num;
    struct shm_app tab[SHM_APP_TAB_SIZE];
};

struct shm_relation
{
    struct shm_app  *app;
    rt_list_t       app_node;
    struct shm_mem  *mem;
    rt_list_t       mem_node;
};

#define SHM_RELATION_TAB_SIZE 8
struct shm_relation_tab
{
    rt_list_t node;
    rt_uint32_t used_num;
    struct shm_relation tab[SHM_RELATION_TAB_SIZE];
};

struct _lwt_shm
{
    struct bmem_typedef* bmem;
    rt_uint32_t* shm_addr;
    rt_uint32_t shm_size;


    //struct shm_relation *relation_tabs;
    //struct shm_app      *app_tabs;
    //struct shm_mem      *mem_tabs;

    //指向三种tab的node节点
    rt_list_t   relation_tab;
    rt_list_t   app_tab;
    rt_list_t   mem_tab;

    //lock

};
typedef struct _lwt_shm* lwt_shm_t;

#endif
