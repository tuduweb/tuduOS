#include <rthw.h>
#include <rtthread.h>
#include "lwt_shm.h"

#define DBG_TAG    "SHM"
#define DBG_LVL    DBG_WARNING
#include <rtdbg.h>

/**
 * 共享内存
 */

struct _lwt_shm lwt_shm;
#define LWT_SHM_SIZE 0x4000


struct bmem_block* get_mem_block(struct bmem_typedef* bmem)
{
    struct bmem_tab* bmem_tab;
    struct bmem_block* bmem_block;

    int tab_num = 0;

    while(1)
    {

        /* 遍历 tab_head,寻找空闲 */
        /* bmem->tab_head是普通节点rt_list_t型,其他的为数据节点bmem_block型 */
        for(bmem_tab = (struct bmem_tab*)bmem->tab_head.next; bmem_tab != (struct bmem_tab*)&bmem->tab_head; bmem_tab = (struct bmem_tab*)bmem_tab->node.next)
        {
            
            if(bmem_tab->used_num < BLOCK_TAB_SIZE)
            {
                for(tab_num = 0; tab_num < BLOCK_TAB_SIZE; tab_num++)
                {
                    bmem_block = bmem_tab->tab + tab_num;

                    //找到空闲的节点
                    if(bmem_block->is_used == 0)
                    {
                        bmem_tab->used_num++;
                        return bmem_block;//记住需要初始化
                    }
                }
            }
        }

        //如果没有空闲的tab_head,那么申请新的
        bmem_tab = rt_calloc(1, sizeof(struct bmem_tab));// + sizeof(struct bmem_block)*BLOCK_TAB_SIZE
        if(bmem_tab)
        {
            bmem_tab->node.prev = &bmem_tab->node;
            bmem_tab->node.next = &bmem_tab->node;
            bmem_tab->used_num = 0;
            rt_list_insert_after(&bmem->tab_head, &bmem_tab->node);
        }else{
            return RT_NULL;
        }

    }

    //return RT_NULL;
}

struct bmem_typedef* bmem_create(rt_uint32_t start, rt_uint32_t size)
{
    struct bmem_typedef* bmem;
    struct bmem_block* bmem_block;

    //TODO:字节对齐处理
    bmem = rt_calloc(1, sizeof(struct bmem_typedef));

    if(bmem)
    {
        bmem->start = (char *)start;
        bmem->mem_size = size;

        bmem->block_node.prev = &bmem->block_node;
        bmem->block_node.next = &bmem->block_node;

        bmem->tab_head.prev = &bmem->tab_head;
        bmem->tab_head.next = &bmem->tab_head;

        bmem_block = rt_calloc(1, sizeof(struct bmem_block));//get_mem_block(bmem);//需要解决头个被重复返回的问题
        if(bmem_block)
        {
            bmem_block->len = bmem->mem_size;
            bmem_block->is_used = 0;
            bmem_block->node.prev = &bmem_block->node;
            bmem_block->node.next = &bmem_block->node;

            rt_list_insert_after(&bmem->block_node, &bmem_block->node);
        }else{
            rt_free(bmem);
            bmem = RT_NULL;
        }
    }

    return bmem;
}

/**
 * @name 申请bmem
 * @brief 在create里面的mem_block中,分出更多的block
 */
void *bmem_malloc(struct bmem_typedef* bmem, rt_uint32_t size)
{
    struct bmem_block* bmem_block;
    struct bmem_block* new_bmem_block;
    rt_uint32_t mem_offset = 0;

    if(bmem->mem_size >= size)
    {
        for(bmem_block = (struct bmem_block*)bmem->block_node.next;
            bmem_block != (struct bmem_block*)&bmem->block_node;
            bmem_block = (struct bmem_block*)bmem_block->node.next)
        {
            if(bmem_block->is_used == 0)
            {
                //大小不符合要求
                if(bmem_block->len >= size)
                {

                    if(bmem_block->len > size)
                    {
                        //拆分
                        new_bmem_block = get_mem_block(bmem);
                        if(!new_bmem_block)
                            return RT_NULL;
                        
                        new_bmem_block->is_used = 1;
                        new_bmem_block->len = size;
                        new_bmem_block->node.next = &new_bmem_block->node;
                        new_bmem_block->node.prev = &new_bmem_block->node;
                        //插入,拆分
                        bmem_block->len = bmem_block->len - size;//大小拆分
                        //插入到空闲的前面
                        rt_list_insert_before(&bmem_block->node, &new_bmem_block->node);
                    }

                    if(bmem_block->len == size)
                    {
                        //使用
                        bmem_block->is_used = 1;
                    }
                    
                    bmem->used_mem_size += size;//更新已经占用大小
                    if(bmem->used_mem_size > bmem->used_max_mem_size)
                        bmem->used_max_mem_size = bmem->used_mem_size;//更新最大占用
                    
                    return (void *)&(bmem->start[mem_offset]);

                }

            }
            //偏移地址累加
            mem_offset += bmem_block->len;
        }
    }

    return RT_NULL;
}

rt_err_t bmem_free(void* addr)
{
    return RT_EOK;
}


int lwt_shm_init()
{
    rt_uint32_t shm = (rt_uint32_t)RT_KERNEL_MALLOC(LWT_SHM_SIZE);

    rt_memset(&lwt_shm, 0, sizeof(struct _lwt_shm));

    lwt_shm.bmem = bmem_create(shm, LWT_SHM_SIZE);

    if(!lwt_shm.bmem)
    {
        LOG_E("shm Error");
        return -RT_ERROR;
    }

    lwt_shm.shm_addr = (rt_uint32_t *)shm;
    lwt_shm.shm_size = LWT_SHM_SIZE;

    lwt_shm.app.prev = &lwt_shm.app;
    lwt_shm.app.next = &lwt_shm.app;
    lwt_shm.relation.prev = &lwt_shm.relation;
    lwt_shm.relation.next = &lwt_shm.relation;
    
    return RT_EOK;
}

INIT_ENV_EXPORT(lwt_shm_init);

void *lwt_shm_alloc(int size)
{
    return 0;
}

void *lwt_shm_retain(void* addr)
{
    return 0;
}

rt_err_t lwt_shm_free(void* addr)
{
    return RT_EOK;
}

void malloc_test(int argc,char* argv[])
{
    rt_uint32_t addr = 0;
    addr = (rt_uint32_t)bmem_malloc(lwt_shm.bmem, 100);
    rt_kprintf("addr:%x\r\n", addr);
}
MSH_CMD_EXPORT(malloc_test,malloc!);

