#include <rthw.h>
#include <rtthread.h>
#include "lwt_shm.h"

#define DBG_TAG    "SHM"
#define DBG_LVL    DBG_WARNING
#include <rtdbg.h>

/**
 * 共享内存
 */

#define LWT_SHM_DEBUG 1


struct _lwt_shm lwt_shm;
#define LWT_SHM_SIZE 0x4000


//#define get_relation_app(ADDR)  ((struct shm_app*)(ADDR - struct_offset(struct shm_relation, app_node) + struct_offset(struct shm_relation, app) ))
//#define get_relation_mem(ADDR)  ((struct shm_mem*)(ADDR - struct_offset(struct shm_relation, mem_node) + struct_offset(struct shm_relation, mem) ))
#define get_relation_app(NODE)  (*(struct shm_app**)(NODE - sizeof(rt_base_t) ))
#define get_relation_mem(NODE)  (*(struct shm_mem**)(NODE - sizeof(rt_base_t) ))

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

rt_err_t put_mem_block(struct bmem_typedef* bmem, struct bmem_block* block)
{
    struct bmem_tab* bmem_tab;
    struct bmem_block* bmem_block;
    int tab_num = 0;

    /* 遍历 tab_head,寻找当前的block */
    /* bmem->tab_head是普通节点rt_list_t型,其他的为数据节点bmem_block型 */
    for(bmem_tab = (struct bmem_tab*)bmem->tab_head.next; bmem_tab != (struct bmem_tab*)&bmem->tab_head; bmem_tab = (struct bmem_tab*)bmem_tab->node.next)
    {
        for(tab_num = 0; tab_num < BLOCK_TAB_SIZE; tab_num++)
        {
            //bmem_block = bmem_tab->tab + tab_num;

            /* 如果找到了 */
            if(bmem_block->is_used != 0 && (bmem_tab->tab + tab_num == block))
            {
                //block->len = 0;
                block->is_used = 0;
                bmem_tab->used_num--;

                //tab空闲,则删除
                if(bmem_tab->used_num == 0)
                {
                    rt_list_remove(&bmem_tab->node);//从tab_head中移除
                    rt_free(bmem_tab);
                }

                return RT_EOK;

            }
            
        }

    }

    return -RT_ERROR;
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

rt_err_t bmem_free(struct bmem_typedef* bmem, void* addr)
{
    struct bmem_block* bmem_block;
    rt_uint32_t mem_offset = 0;

    for(bmem_block = (struct bmem_block*)bmem->block_node.next;
        bmem_block != (struct bmem_block*)&bmem->block_node;
        bmem_block = (struct bmem_block*)bmem_block->node.next)
    {
        if(addr < (void *)&(bmem->start[mem_offset]))
        {
            LOG_E("bmem_free error! %x", addr);
        }else if(addr == (void *)&(bmem->start[mem_offset]))
        {
            put_mem_block(bmem, bmem_block);
            rt_list_remove((rt_list_t *)bmem_block);
            /* 找到了这块bmem */
            //put
            return RT_EOK;
        }
        

        mem_offset += bmem_block->len;
    }

    /* 在bmem_block中寻找 addr 块 */
    return -RT_ERROR;
}
#define MALLOC_TAB_SIZE 20
rt_uint32_t malloc_addr_tab[MALLOC_TAB_SIZE];
int malloc_pos = 0;

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

    lwt_shm.app_tab.prev = &lwt_shm.app_tab;
    lwt_shm.app_tab.next = &lwt_shm.app_tab;
    lwt_shm.mem_tab.prev = &lwt_shm.mem_tab;
    lwt_shm.mem_tab.next = &lwt_shm.mem_tab;
    lwt_shm.relation_tab.prev = &lwt_shm.relation_tab;
    lwt_shm.relation_tab.next = &lwt_shm.relation_tab;

    rt_memset(malloc_addr_tab, 0, sizeof(malloc_addr_tab));
    
    return RT_EOK;
}

INIT_ENV_EXPORT(lwt_shm_init);


struct shm_app* get_shm_app(lwt_shm_t lwt_shm)
{
    struct shm_app_tab* app_tab_item;
    struct shm_app* app_item;

    while (1)
    {

        for(app_tab_item = (struct shm_app_tab *)lwt_shm->app_tab.next;
            (rt_list_t *)app_tab_item != &lwt_shm->app_tab;
            app_tab_item = (struct shm_app_tab *)app_tab_item->node.next)
        {
            if(app_tab_item->used_num == SHM_APP_TAB_SIZE)
                continue;

            for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
            {
                app_item = app_tab_item->tab + i;
                //判断是否为空闲..
                if(app_item->use_num != 0)
                    continue;
                
                app_item->mem_node.prev = &app_item->mem_node;
                app_item->mem_node.next = &app_item->mem_node;
                app_tab_item->used_num++;
                return app_item;
            }
        }
        //reach here:new
        app_tab_item = rt_calloc(1, sizeof(struct shm_app_tab));
        if(app_tab_item == RT_NULL)
        {
            LOG_I("can't malloc new shm_app_tab!");
            return RT_NULL;
        }

        app_tab_item->node.prev = &app_tab_item->node;
        app_tab_item->node.next = &app_tab_item->node;
        //放入tab池
        rt_list_insert_after(&lwt_shm->app_tab, &app_tab_item->node);

    }
}

struct shm_app* find_shm_app(lwt_shm_t lwt_shm, void* lwp)
{
    struct shm_app_tab* app_tab_item;
    struct shm_app* app_item;

    //lwp == 0时..

    for(app_tab_item = (struct shm_app_tab *)lwt_shm->app_tab.next;
        (rt_list_t *)app_tab_item != &lwt_shm->app_tab;
        app_tab_item = (struct shm_app_tab *)app_tab_item->node.next)
    {
        if(app_tab_item->used_num == 0)
            continue;

        for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
        {
            app_item = app_tab_item->tab + i;
            if((rt_uint32_t)lwp == app_item->lwp_addr)
            {
                return app_item;
            }
        }
    }

    //没找到
    return RT_NULL;
}

rt_err_t put_shm_app(lwt_shm_t lwt_shm, struct shm_app * app)
{
    /* find app in list */
    struct shm_app_tab* app_tab_item;
    struct shm_app* find_app = RT_NULL;

    for(app_tab_item = (struct shm_app_tab *)lwt_shm->app_tab.next;
        (rt_list_t *)app_tab_item != &lwt_shm->app_tab;
        app_tab_item = (struct shm_app_tab *)app_tab_item->node.next)
    {
        if(app_tab_item->used_num == 0)
            continue;

        for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
        {
            if((app_tab_item->tab + i) == app)
            {
                find_app = app_tab_item->tab + i;
                goto _find_ok;
            }
        }
    }
_not_find:

    return -RT_ERROR;
_find_ok:
    //删除社会关系
    struct shm_relation* relation;



    //尝试删除相关内存?引用次数--
    //删除相关内存的引用对象

    //删除app
    rt_list_remove(&find_app->mem_node);
    rt_free(find_app->ref_tab);
    rt_memset(find_app, 0, sizeof(struct shm_app));

    //尝试清除tab
    return RT_EOK;
}

struct shm_mem* get_shm_mem(lwt_shm_t lwt_shm)
{
    struct shm_mem_tab* mem_tab_item;
    struct shm_mem*     mem_item;

    while(1)
    {

        for(mem_tab_item = (struct shm_mem_tab *)lwt_shm->mem_tab.next;
            (rt_list_t *)mem_tab_item != &lwt_shm->mem_tab;
            mem_tab_item = (struct shm_mem_tab *)mem_tab_item->node.next)
        {
            if(mem_tab_item->used_num == SHM_APP_TAB_SIZE)
                continue;

            for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
            {
                mem_item = mem_tab_item->tab + i;
                //判断是否为空闲..
                if(mem_item->size == 0)
                {
                    mem_tab_item->used_num++;
                    
                    mem_item->app_node.prev = &mem_item->app_node;
                    mem_item->app_node.next = &mem_item->app_node;
                    return mem_item;
                }
            }
        }
        //reach here:new
        mem_tab_item = rt_calloc(1, sizeof(struct shm_mem_tab));
        if(mem_tab_item == RT_NULL)
        {
            LOG_I("can't malloc new shm_mem_tab!");
            return RT_NULL;
        }

        mem_tab_item->node.prev = &mem_tab_item->node;
        mem_tab_item->node.next = &mem_tab_item->node;
        //放入tab池
        rt_list_insert_after(&lwt_shm->mem_tab, &mem_tab_item->node);

    }
}

struct shm_mem* find_shm_mem(lwt_shm_t lwt_shm, void* addr)
{
    struct shm_mem_tab* mem_tab_item;
    struct shm_mem*     mem_item;

    for(mem_tab_item = (struct shm_mem_tab *)lwt_shm->mem_tab.next;
        (rt_list_t *)mem_tab_item != &lwt_shm->mem_tab;
        mem_tab_item = (struct shm_mem_tab *)mem_tab_item->node.next)
    {
        if(mem_tab_item->used_num == 0)
            continue;

        for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
        {
            mem_item = mem_tab_item->tab + i;
            //判断是否为空闲..
            if(mem_item->addr == (rt_uint32_t)addr)
                return mem_item;
        }
    }
    //没有找到
    return RT_NULL;
}

/**
 * @name 删除内存
 */
rt_err_t put_shm_mem(lwt_shm_t lwt_shm, struct shm_mem* mem)
{
    struct shm_mem_tab* mem_tab_item;
    struct shm_mem*     mem_item;

    for(mem_tab_item = (struct shm_mem_tab *)lwt_shm->mem_tab.next;
        (rt_list_t *)mem_tab_item != &lwt_shm->mem_tab;
        mem_tab_item = (struct shm_mem_tab *)mem_tab_item->node.next)
    {
        for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
        {
            mem_item = mem_tab_item->tab + i;
            if(mem == mem_item)
            {
                //找到了
                mem_tab_item->used_num--;
                //mem->addr = RT_NULL;
                mem_item->size = 0;
                break;
            }
        }

        if(mem_tab_item->used_num == 0)
        {
            //rt_free(mem_tab_item);
            //rt_list_remove(&mem_tab_item->node);
            //可以删除tab
        }
    }

    /* 判断是否执行 */
    if(mem->size > 0)
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

struct shm_relation* get_shm_relation(lwt_shm_t lwt_shm)
{
    struct shm_relation_tab* relation_tab_item;
    struct shm_relation*    relation_item;

    while(1)
    {
        for(relation_tab_item = (struct shm_relation_tab *)lwt_shm->relation_tab.next;
            (rt_list_t *)relation_tab_item != &lwt_shm->relation_tab;
            relation_tab_item = (struct shm_relation_tab *)relation_tab_item->node.next)
        {
            if(relation_tab_item->used_num == SHM_APP_TAB_SIZE)
                continue;

            for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
            {
                relation_item = relation_tab_item->tab + i;
                //判断是否为空闲..
                if(relation_item->app == RT_NULL || relation_item->app_node.next == &relation_item->app_node)
                    return relation_item;
            }
        }

        //reach here:new
        relation_tab_item = rt_calloc(1, sizeof(struct shm_relation_tab));
        if(relation_tab_item == RT_NULL)
        {
            LOG_I("can't malloc new shm_relation_tab!");
            return RT_NULL;
        }

        relation_tab_item->node.prev = &relation_tab_item->node;
        relation_tab_item->node.next = &relation_tab_item->node;
        //放入tab池
        rt_list_insert_after(&lwt_shm->relation_tab, &relation_tab_item->node);

    }
}

rt_err_t put_shm_relation(lwt_shm_t lwt_shm, struct shm_relation* relation)
{
    return -RT_ERROR;
}

/**
 * 申请/内存引用
 * if addr == 0,then new
 */
struct shm_mem_ref* get_shm_mem_ref(struct shm_app* app, void* addr)
{
    #define SHM_MEM_REF_APPEND_NUM 8
    struct shm_mem_ref* mem_ref_item;
    struct shm_mem_ref* last_empty_mem_ref = RT_NULL;


    for(int i = 0; i < app->ref_tab_size; i++)
    {
        mem_ref_item = app->ref_tab + i;
        if(mem_ref_item->addr)
        {
            if(mem_ref_item->addr == (rt_uint32_t)addr)
                return mem_ref_item;
        }else{
            if(addr == RT_NULL)
                return mem_ref_item;
            //记录首个空的位置
            if(last_empty_mem_ref == RT_NULL)
                last_empty_mem_ref = mem_ref_item;
        }
    }

    /* 新增模式 && 没有空闲位置,则新建? */
    //if(addr == RT_NULL && last_empty_mem_ref == RT_NULL)
    if(last_empty_mem_ref == RT_NULL)
    {
        //reach here:not find
        mem_ref_item = rt_calloc(app->ref_tab_size + SHM_MEM_REF_APPEND_NUM, sizeof(struct shm_mem_ref));
        
        if(mem_ref_item == RT_NULL)
            return RT_NULL;

        /* copy old then free old,change pointer */
        rt_memcpy(mem_ref_item, app->ref_tab, sizeof(struct shm_mem_ref)*app->ref_tab_size);
        rt_free(app->ref_tab);
        app->ref_tab = mem_ref_item;
        app->ref_tab_size += SHM_MEM_REF_APPEND_NUM;

        if(addr == RT_NULL)
        {
            //
        }

        return app->ref_tab + app->ref_tab_size - SHM_MEM_REF_APPEND_NUM;
    }

    return last_empty_mem_ref;
}


/**
 * @name 申请共享内存接口函数
 * @brief 申请共享内存接口函数，通过bmem
 */
void *lwt_shm_alloc(int size)
{
    struct shm_app* app;
#if LWT_SHM_DEBUG == 1
    app = find_shm_app(&lwt_shm, (void *)0x20000800);//查找当前的LWP有没有APP
#else
    app = find_shm_app(&lwt_shm, rt_thread_self()->lwp);
#endif
    if(app == RT_NULL)
    {
        app = get_shm_app(&lwt_shm);
        if(app == RT_NULL)
        {
            LOG_E("can't alloc new shm_app!");
            return RT_NULL;
        }
#if LWT_SHM_DEBUG == 1
        app->lwp_addr = 0x20000800;//(rt_uint32_t)rt_thread_self()->lwp;
#else
        app->lwp_addr = (rt_uint32_t)rt_thread_self()->lwp;
#endif
    }

    //新建
    struct shm_mem_ref* mem_ref = get_shm_mem_ref(app, 0);
    
    struct shm_mem* mem = get_shm_mem(&lwt_shm);
    struct shm_relation* relation = get_shm_relation(&lwt_shm);

    //判断是否成功
    if(mem && relation)
    {
        mem->addr = (rt_uint32_t)bmem_malloc(lwt_shm.bmem, size);
        if(mem->addr)
        {
            mem->size = size;
            app->use_num++;
            mem_ref->addr = mem->addr;
            mem_ref->ref = 1;
            //建立关系
            relation->app = app;
            relation->mem = mem;
            //mem->app_node => relation->app_node => ..
            rt_list_insert_after(&mem->app_node, &relation->app_node);
            rt_list_insert_after(&app->mem_node, &relation->mem_node);
            return (void *)mem->addr;
        }
    }

    return RT_NULL;
}

/**
 * 申明共享内存
 * @brief 申明共享内存,区别在不申请内存。而是查找。
 */
rt_err_t lwt_shm_retain(void* addr)
{
    struct shm_mem* mem;
    struct shm_app* app;

    mem = find_shm_mem(&lwt_shm, addr);

    if(mem)
    {
#if LWT_SHM_DEBUG == 1
        app = find_shm_app(&lwt_shm, (void *)0x20000808);//查找当前的LWP有没有APP
#else
        app = find_shm_app(&lwt_shm, rt_thread_self()->lwp);//查找当前的LWP有没有APP
#endif
        /* not find, so need to new one */
        if(app == RT_NULL)
        {
            app = get_shm_app(&lwt_shm);
            if(app != RT_NULL)
            {
#if LWT_SHM_DEBUG == 1
                app->lwp_addr = 0x20000808;//(rt_uint32_t)rt_thread_self()->lwp;
#else
                app->lwp_addr = (rt_uint32_t)rt_thread_self()->lwp;
#endif
            }else{
                /* cant new shm_app */
            }
        }

        /* get mem_ref : new or old */
        struct shm_mem_ref* mem_ref = get_shm_mem_ref(app, addr);
        if(mem_ref)
        {
            //whatever,increase ref cnt first
            mem_ref->ref++;

            if(mem_ref->addr == RT_NULL)
            {
                /* new mem_ref, need to save addr */
                mem_ref->addr = (rt_uint32_t)addr;
                /* cant find mem_ref,so new relation between app and mem */
                struct shm_relation* relation = get_shm_relation(&lwt_shm);
                if(relation == RT_NULL)
                {
                    LOG_E("cant alloc relation:%x", addr);
                    while(1);
                }

                //关系建立在app和mem上
                relation->app = app;
                relation->mem = mem;

                app->use_num++;//引用次数增加

                rt_list_insert_after(&mem->app_node, &relation->app_node);
                rt_list_insert_after(&app->mem_node, &relation->mem_node);
            }else{
                /* old ref,so there have relation between app and mem(addr) */
            }
            
            return RT_EOK;
        }else{
            LOG_E("cant alloc mem_ref / %x", addr);
            while(1);
        }
        
        /* find mem_ref */
    }else{
        LOG_E("dont find this shm_mem / %x", addr);
        while(1);
    }
    
    /* not find */
    return -RT_ERROR;
}

/**
 * @name 软释放内存
 * @brief 要判断被引用ref的次数,次数为0才是真删除 
 */
rt_err_t lwt_shm_free(void* addr)
{
    struct shm_mem* mem = RT_NULL;
    struct shm_app* app = RT_NULL;

    /* find this mem */
    mem = find_shm_mem(&lwt_shm, addr);
    if(mem == RT_NULL)
    {
        LOG_E("mem addr %x not find!", addr);
        while(1);
    }

    /* mem -> app */
    struct shm_relation* relation = RT_NULL;

    struct shm_app* app_item = RT_NULL;
    struct rt_list_node* node;
    for(node = mem->app_node.next; node != &mem->app_node; node = node->next)
    {
        /* get app_item from struct<shm_relation> */
        relation = (*(struct shm_relation**)(node - struct_offset(struct shm_relation, app_node) ));
        //relation->app
        if(relation->app->lwp_addr == (rt_uint32_t)rt_thread_self()->lwp)
        {
            app = relation->app;
            break;
        }
    }

    /* get_shm_mem_ref */
    struct shm_mem_ref* mem_ref = get_shm_mem_ref(app, addr);
    if(mem_ref->addr == RT_NULL)
    {
        LOG_E("mem_ref %x not find!", addr);
        while(1);
    }

    if(mem_ref->ref > 0)
    {
        mem_ref->ref--;

        if(mem_ref->ref == 0)
        {
            /* mem_ref in app must be deleted */
            //relation->app_node
            //relation->mem_node
            rt_list_remove(&relation->app_node);
            rt_list_remove(&relation->mem_node);
            put_shm_relation(&lwt_shm, relation);
            /* emptry mem_ref.addr can make the ref reused */
            mem_ref->addr = RT_NULL;//清空mem_ref
            /* sub app.use_num(mem use num), if the num equal zero then we can free the app */
            app->use_num--;
        }
    }

    /* if app_node in mem is emptry, indicate mem is useless */
    if(mem->app_node.next == &mem->app_node)
    {
        bmem_free(lwt_shm.bmem, (void *)mem->addr);
        put_shm_mem(&lwt_shm, mem);
    }

    /* put shm_app */
    

    return RT_EOK;
}

void shm_malloc_test(int argc,char* argv[])
{
    rt_uint32_t addr = 0;
    addr = (rt_uint32_t)bmem_malloc(lwt_shm.bmem, 100);
    if(addr && malloc_pos < MALLOC_TAB_SIZE)
    {
        malloc_addr_tab[malloc_pos++] = addr;
    }
    rt_kprintf("shm malloc addr:%x\r\n", addr);
}
MSH_CMD_EXPORT(shm_malloc_test, malloc!);

void shm_free_test(int argc,char* argv[])
{
    if(malloc_pos > 0)
    {
        if(bmem_free(lwt_shm.bmem, (void *)malloc_addr_tab[malloc_pos - 1]) == RT_EOK)
        {
            rt_kprintf("shm free addr:%x\r\n", malloc_addr_tab[malloc_pos - 1]);
            malloc_pos--;
        }
    }else{
        rt_kprintf("malloc_pos = 0\r\n");
    }
}
MSH_CMD_EXPORT(shm_free_test, free!);

void sys_alloc_test(int argc,char* argv[])
{
    rt_uint32_t addr = 0;
    addr = (rt_uint32_t)lwt_shm_alloc(200);
    if(addr)
    {
        rt_kprintf("shm sys alloc addr:%x\r\n", addr);

        rt_kprintf("shm retain\r\n");

        lwt_shm_retain((void *)addr);

    }

}
MSH_CMD_EXPORT(sys_alloc_test, alloc!);

void print_shm_app(lwt_shm_t lwt_shm, void *app)
{
    struct shm_app_tab* app_tab_item;
    struct shm_app* app_item;

    rt_kprintf("lwt_addr\tusn\tnode\r\n");


    if(app == RT_NULL)
    {
        //list all
        
        for(app_tab_item = (struct shm_app_tab *)lwt_shm->app_tab.next;
            (rt_list_t *)app_tab_item != &lwt_shm->app_tab;
            app_tab_item = (struct shm_app_tab *)app_tab_item->node.next)
        {

            for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
            {
                app_item = app_tab_item->tab + i;
                if(app_item->use_num != 0)
                {
                    rt_kprintf("%x\t%d\t%x \r\n", app_item->lwp_addr, app_item->use_num, app_item->mem_node);
                    //struct rt_list_node* list_node;
                    //APP下内存
                    int pos = 0;
                    for(struct rt_list_node* list_node = app_item->mem_node.next; list_node != &app_item->mem_node; list_node = list_node->next)
                    {
                        //struct shm_mem* mem_item = get_relation_mem(list_node);
                        struct shm_relation* relation = (struct shm_relation*)((rt_base_t)list_node - struct_offset(struct shm_relation, mem_node));
                        struct shm_mem* mem_item2 = relation->mem;//(struct shm_mem*)((rt_base_t)list_node - sizeof(struct shm_mem *));
                        struct shm_mem* mem_item = *(struct shm_mem**)((rt_base_t)list_node - sizeof(struct shm_mem *));//在计算出的地址处,取值,得到的是指针!所以计算处的类型为**
                        rt_kprintf("[%d]\t%x\t%d\t%x \r\n", pos++, mem_item->addr, mem_item->size, mem_item2->addr);
                    }               
                
                }
            }
        }
    }
}

void msh_print_shm_app(int argc,char* argv[])
{
    print_shm_app(&lwt_shm, 0);
}
FINSH_FUNCTION_EXPORT_ALIAS(msh_print_shm_app, __cmd_list_shm_app, LIST shm app.);


void print_shm_mem(lwt_shm_t lwt_shm, void *mem)
{
    struct shm_mem_tab* mem_tab_item;
    struct shm_mem* mem_item;

    rt_kprintf("mem_addr\tsize\tnode\r\n");

    if(mem == RT_NULL)
    {

        for(mem_tab_item = (struct shm_mem_tab *)lwt_shm->mem_tab.next;
            (rt_list_t *)mem_tab_item != &lwt_shm->mem_tab;
            mem_tab_item = (struct shm_mem_tab *)mem_tab_item->node.next)
        {
            if(mem_tab_item->used_num == 0)
                continue;
            
            for(int i = 0;i < SHM_APP_TAB_SIZE; i++)
            {
                mem_item = mem_tab_item->tab + i;
                if(mem_item->addr != RT_NULL)
                {
                    rt_kprintf("%x\t%d\t%x \r\n", mem_item->addr, mem_item->size, mem_item->app_node);

                    
                    //内存下app
                    int pos = 0;
                    for(struct rt_list_node* list_node = mem_item->app_node.next; list_node != &mem_item->app_node; list_node = list_node->next)
                    {
                        struct shm_app* app_item = *(struct shm_app**)((rt_base_t)list_node - sizeof(struct shm_app *));//在计算出的地址处,取值,得到的是指针!所以计算处的类型为**
                        rt_kprintf("[%d]\t%x\t%d\t%x \r\n", pos++, app_item->lwp_addr, app_item->use_num, app_item->mem_node);
                    }
                }
            }
        }

    }
}

void msh_print_shm_mem(int argc,char* argv[])
{
    print_shm_mem(&lwt_shm, 0);
}
FINSH_FUNCTION_EXPORT_ALIAS(msh_print_shm_mem, __cmd_list_shm_mem, LIST shm mem.);
