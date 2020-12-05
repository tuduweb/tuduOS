/**
 * 轻量级线程 Light Weight Process
 * 由于是RTT4.02版本的再构建,所以暂时取名为LWT,Process->Task
 * 主要用来实现.bin文件的加载执行等
**/


#include <lwt.h>
#include <rtthread.h>
#include <rthw.h>
#include <dfs_posix.h>

#define DBG_TAG    "LWP"
#define DBG_LVL    DBG_WARNING
#include <rtdbg.h>

//---->以下为日志单元的配置项
//#define LOG_TAG     "lwt"     // 该模块对应的标签。不定义时，默认：NO_TAG
//#define LOG_LVL     LOG_LVL_DBG   // 该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>                 // 必须在 LOG_TAG 与 LOG_LVL 下面
//日志单元配置项结束<----


struct lwt_pidmap lwt_pid;

/**
 * 初始化线程系统
 */
int lwt_init(void)
{
    for(int i = 0; i < LWT_PIDMAP_SIZE; ++i)
    {
        lwt_pid.pidmap[i] = RT_NULL;
    }
}
INIT_ENV_EXPORT(lwt_init);

extern void lwp_user_entry(void *args, const void *text, void *r9, void *data);
void lwt_set_kernel_sp(uint32_t *sp)
{
    rt_thread_t thread = rt_thread_self();
    struct rt_lwt *user_data;
    user_data = (struct rt_lwt *)rt_thread_self()->lwp;
    thread->kernel_sp = sp;
    //kernel_sp = sp;
}

uint32_t *lwt_get_kernel_sp(void)
{
    rt_thread_t thread = rt_thread_self();
    struct rt_lwt *user_data;
    user_data = (struct rt_lwt *)rt_thread_self()->lwp;
    return thread->kernel_sp;
}

/**
 * 参数复制 把参数拷贝到lwt的结构体中
**/
static int lwt_argscopy(struct rt_lwt *lwt, int argc, char **argv)
{
    int size = sizeof(int)*3; /* store argc, argv, NULL */
    int *args;
    char *str;
    char **new_argv;
    int i;
    int len;

    for (i = 0; i < argc; i ++)
    {
        size += (rt_strlen(argv[i]) + 1);
    }
    size  += (sizeof(int) * argc);

    args = (int*)rt_malloc(size);
    if (args == RT_NULL)
        return -RT_ERROR;

    str = (char*)((int)args + (argc + 3) * sizeof(int));
    new_argv = (char**)&args[2];
    args[0] = argc;
    args[1] = (int)new_argv;

    for (i = 0; i < argc; i ++)
    {
        len = rt_strlen(argv[i]) + 1;
        new_argv[i] = str;
        rt_memcpy(str, argv[i], len);
        str += len;
    }
    new_argv[i] = 0;
    lwt->args = args;

    return 0;
}
#include "easyflash.h"
static int lwt_load(const char *filename, struct rt_lwt *lwt, uint8_t *load_addr, size_t addr_size)
{
    int fd;
    uint8_t *ptr;
    int result = RT_EOK;
    int nbytes;
    struct lwt_header header;
    struct lwt_chunk  chunk;

    /* check file name */
    RT_ASSERT(filename != RT_NULL);
    /* check lwp control block */
    RT_ASSERT(lwt != RT_NULL);

    /* 根据加载地址判断地址是否为Fix mode */
    if (load_addr != RT_NULL)
    {
        lwt->lwt_type = LWP_TYPE_FIX_ADDR;
        ptr = load_addr;
    }
    else
    {
        lwt->lwt_type = LWP_TYPE_DYN_ADDR;
        ptr = RT_NULL;
    }


    const char* itemname;
    /* 查找文件名(也就是去除掉目录) -> 若不存在待查字符,则返回空指针*/
    itemname = strrchr( filename, '/');
    if(itemname == RT_NULL)
        itemname = filename;//不存在'/',那么直接拿来用
    else
        itemname++;//去除掉'/'只要后面的 比如 bin/app.bin -> app.bin
    //这个参数干嘛用的呢..
    rt_strncpy(lwt->cmd, itemname, 8);
    
    /* 这里需要更换成xipfs 现在暂时是fatfs */
    fd = open(filename, 0, O_RDONLY);

    if (fd < 0)
    {
        dbg_log(DBG_ERROR, "open file:%s failed!\n", filename);
        result = -RT_ENOSYS;
        goto _exit;
    }else{
        //fd >= 0

        struct env_meta_data env;

        //ioctl
        if(ioctl(fd, 0x0002, &env) != RT_EOK)
        {
            LOG_E("Can't find that [%s] ENV!", filename);
            result = -RT_EEMPTY;
            goto _exit;
        }



        //判断是否能XIP启动,无法XIP启动那么需要复制到RAM中

        //lwp->text_size = RT_ALIGN(chunk.data_len_space, 4);
        lwt->text_size = RT_ALIGN(env.value_len, 4);
        //rt_uint8_t * text_entry = (rt_uint8_t *)rt_malloc( lwt->text_size );//RT_MALLOC_ALGIN
        rt_uint8_t * text_entry = (rt_uint8_t *)rt_malloc_align( lwt->text_size , 8);//RT_MALLOC_ALGIN

        //new_entry = RT_ALIGN(new_entry);

        if (text_entry == RT_NULL)
        {
            dbg_log(DBG_ERROR, "alloc text memory faild!\n");
            result = -RT_ENOMEM;
            goto _exit;
        }
        else
        {
            dbg_log(DBG_LOG, "lwp text malloc : %p, size: %d!\n", text_entry, lwt->text_size);
        }

        rt_kprintf("lwp text malloc : %p, size: %d!\n", text_entry, lwt->text_size);

        //复制内容
        int nbytes = read(fd, text_entry, lwt->text_size);

        if(nbytes != lwt->text_size)
        {
            result = -RT_EIO;
            goto _exit;
        }

        lwt->text_entry = text_entry;


        


        //在app里面第9位
        lwt->data_size = *(rt_uint32_t *)(lwt->text_entry + 0x80);//(uint8_t)*(lwt->text_entry + 9);//addr
        //申请数据空间
        //lwt->data_entry = rt_lwt_alloc_user(lwt,lwt->data_size);
        lwt->data_entry = rt_malloc(lwt->data_size);
        if (lwt->data_entry == RT_NULL)
        {
            rt_free_align(text_entry);//释放text
            dbg_log(DBG_ERROR, "alloc data memory faild!\n");
            result = -RT_ENOMEM;
            goto _exit;
        }
        rt_kprintf("LWT APP stack %p - %p\n", lwt->data_entry, (rt_uint32_t)lwt->data_entry + lwt->data_size);


    }

    //if()

#if 0
//.bin文件不存在这些
    /* read lwp header */
    nbytes = read(fd, &header, sizeof(struct lwt_header));
    if (nbytes != sizeof(struct lwt_header))
    {
        dbg_log(DBG_ERROR, "read lwp header return error size: %d!\n", nbytes);
        result = -RT_EIO;
        goto _exit;
    }

    /* check file header */
    if (header.magic != LWT_MAGIC)
    {
        dbg_log(DBG_ERROR, "erro header magic number: 0x%02X\n", header.magic);
        result = -RT_EINVAL;
        goto _exit;
    }
#endif
    
_exit:
    if(fd >= 0)
        close(fd);
    
    if(result != RT_EOK)
    {
        //
    }
    
    return result;
}

struct rt_lwt *rt_lwt_self(void)
{
    rt_thread_t tid = rt_thread_self();
    if(tid == RT_NULL)
        return RT_NULL;
    return tid->lwp;
}


struct rt_lwt *rt_lwt_new(void)
{
    struct rt_lwt *lwt = RT_NULL;
    //关闭中断 主要是MPU保护这一块
    rt_uint32_t level = rt_hw_interrupt_disable();

    //在pidmap中找一个空闲的位置,如果不存在空闲位置,则满；限制条件,为空,小于lastpid
    int pid = 0;
    for(; pid < lwt_pid.lastpid && lwt_pid.pidmap[pid]; ++pid);
    //超出最大值,当前LWT线程太多了
    if(pid >= LWT_PIDMAP_SIZE)
    {
        LOG_I("PID MAP full!");
        lwt_pid.lastpid = 0;
        goto _exit;
    }

    lwt_pid.lastpid = pid + 1;

    lwt = (struct rt_lwt *)rt_malloc(sizeof(struct rt_lwt));

    if(lwt == RT_NULL)
    {
        LOG_E("no memory for lwp struct!");

    }else{

        //置位,赋0
        rt_memset(lwt, 0, sizeof(*lwt));

        //重置双向链表
        lwt->wait_list.prev = &lwt->wait_list;
        lwt->wait_list.next = &lwt->wait_list;
        lwt->object_list.prev = &lwt->object_list;
        lwt->object_list.next = &lwt->object_list;
        lwt->t_grp.prev = &lwt->t_grp;
        lwt->t_grp.next = &lwt->t_grp;
        lwt->ref = 1;//引用次数

        //lwt->pid = //申请到的pid map中的位置(下标)
        lwt->pid = pid;
        //把这个lwt结构体放入 pid_map相应下标形成映射关系
        lwt_pid.pidmap[pid] = lwt;
    }


_exit:
    //重新开启保护
    rt_hw_interrupt_enable(level);

    return lwt;
}

int rt_lwt_free(struct rt_lwt *lwt)
{
    if(lwt)
    {

        if (lwt->lwt_type == LWP_TYPE_DYN_ADDR)
        {
            dbg_log(DBG_INFO, "dynamic lwp\n");
            if (lwt->text_entry)
            {
                dbg_log(DBG_LOG, "lwp text free: %p\n", lwt->text_entry);
    #ifdef RT_USING_CACHE
                rt_free_align(lwt->text_entry);
    #else
                rt_free_align(lwt->text_entry);
    #endif
            }

            //rt_free(lwt);

        }

        if (lwt->data_entry)
        {
                dbg_log(DBG_LOG, "lwp data free: %p\n", lwt->data_entry);
                rt_free(lwt->data_entry);
        }

        if(lwt->args)
        {
            rt_free(lwt->args);
        }

        rt_free(lwt->fdt.fds);
        rt_free(lwt);
    }
    return RT_EOK;
}

void lwt_ref_inc(struct rt_lwt *lwt)
{
    rt_uint32_t level = rt_hw_interrupt_disable();
    
    lwt->ref++;

    rt_hw_interrupt_enable(level);
}

void lwt_ref_dec(struct rt_lwt *lwt)
{
    rt_uint32_t level = rt_hw_interrupt_disable();
    
    if(lwt->ref > 0)
    {
        lwt->ref--;
        if(lwt->ref == 0)
        {
            //无任何地方引用,执行删除操作

            //共享内存
            //引用对象销毁
            while (lwt->fdt.maxfd > 1)
            {
                lwt->fdt.maxfd --;
                close(lwt->fdt.maxfd - 1);//注意不能销毁uart,否则shell失效
            }
            //数据删除
            lwt->pid;
            lwt_pid.pidmap[lwt->pid] = NULL;
            //销毁旗下所有没启动的thread,没启动的thread不会自动退出?
            //启动的也要删除吧

            //以上操作需要防止内存溢出

            rt_lwt_free(lwt);
        }
    }

    rt_hw_interrupt_enable(level);
}

struct rt_lwt * lwt_get_lwt_from_pid(pid_t pid)
{
    return lwt_pid.pidmap[pid];
}

char* lwt_get_name_from_pid(pid_t pid)
{
    struct rt_lwt *lwt;
    char *name;
    if( lwt = lwt_pid.pidmap[pid] )
    {
        //返回字符最后一次出现的指针
        if(name = strrchr(lwt->cmd, '/'))
        {
            return name + 1;
        }
    }

    return RT_NULL;
}

pid_t lwt_get_pid_from_name(char *name)
{
    //在pid_map中进行查找操作..
    for(int i = 0; i < LWT_PIDMAP_SIZE; ++i)
    {
        //
    }
    return -RT_ERROR;
}

pid_t lwt_get_pid(struct rt_lwt *lwt)
{
    if(lwt)
    {
        return lwt->pid;
    }
    return -RT_ERROR;
}

//lwt主线程的cleanup程序
void lwt_cleanup(rt_thread_t tid)
{
    struct rt_lwt *lwt;

    //dbg_log(DBG_INFO, "thread: %s, stack_addr: %08X\n", tid->name, tid->stack_addr);

    lwt = (struct rt_lwt *)tid->lwp;

    lwt_ref_dec(lwt);//引用--,当为0时销毁

}

//lwt子线程临时cleanup程序
void lwt_son_cleanup(rt_thread_t tid)
{
    struct rt_lwt *lwt;

    //dbg_log(DBG_INFO, "thread: %s, stack_addr: %08X\n", tid->name, tid->stack_addr);

    lwt = (struct rt_lwt *)tid->lwp;

    lwt_ref_dec(lwt);//引用--,当为0时销毁
}


void lwt_thread_entry(void* parameter)
{
    rt_thread_t thread;
    struct rt_lwt* lwt;

    thread = rt_thread_self();
    lwt = thread->lwp;
    thread->cleanup = lwt_cleanup;
    //thread->stack_addr = 0;

    lwp_user_entry(lwt->args, lwt->text_entry, lwt->data_entry, 0);
    
}

#include "dfs_file.h"
/**
 * 执行操作
 * envp里存放的是系统的环境变量
**/
int lwt_execve(char *filename, int argc, char **argv, char **envp)
{
    struct rt_lwt *lwt;

    if (filename == RT_NULL)
        return -RT_ERROR;

    //申请LWT

    //把这里换成新建的函数体 类似于c++中的新建一个实例
    lwt = rt_lwt_new();

    
    if (lwt == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    dbg_log(DBG_INFO, "lwt malloc : %p, size: %d!\n", lwt, sizeof(struct rt_lwt));


    //拷贝入口参数到 lwt->args
    if (lwt_argscopy(lwt, argc, argv) != 0)
    {
        //没清理干净
        rt_free(lwt);
        goto __fail;
        //return -ENOMEM;
    }

    //为lwt构建空间
    if(lwt_load(filename, lwt, 0, 0) != RT_EOK)
    {
        //其实需要cleanup
        rt_free(lwt);
        goto __fail;
        //return -RT_ERROR;
    }

    //构建线程FD表
    int fd = libc_stdio_get_console();
    //get然后put是为了ref_count不增加
    struct dfs_fd *d = fd_get(fd);
    fd_put(d);
    if(d)
    {
        fd = fd - DFS_FD_OFFSET;
        struct dfs_fd **fdt = (struct dfs_fd **)rt_malloc( (fd+1) * sizeof(struct dfs_fd*) );
        lwt->fdt.fds = fdt;
        fdt[fd] = d;
        
        lwt->fdt.maxfd = fd + 1;
    }


    char* name = strrchr(filename, '/');
    rt_thread_t thread = rt_thread_create( name ? name + 1: filename, lwt_thread_entry, NULL, 0x200, 29, 200);
    if(thread == RT_NULL)
    {
        LOG_E("lwt_execve create thread error!");
        goto __fail;
    }

    rt_kprintf("LWT kernel stack %p - %p\n", thread->stack_addr, (rt_uint32_t)thread->stack_addr + thread->stack_size);

    int IRID = rt_hw_interrupt_disable();
    
    /* 如果当前是LWP进程,那么构造相互关系 */

    struct rt_lwt *current_lwt;
    current_lwt = rt_thread_self()->lwp;

    if(current_lwt)
    {
        lwt->sibling = current_lwt->first_child;
        current_lwt->first_child = lwt;
        lwt->parent = current_lwt;
    }

    thread->lwp = lwt;

    //初始化: t_grp->(prev/next) = self  t_grp相当于head节点

    /**
     * Thread Group
     * <thread> is new object, so thread->sibling is itself
     * <lwt> is also new object, so lwt->t_grp is empty(or say point itself)
     * Result: lwt->t_grp <=> thread->sibing <=> t_grp
     **/
    lwt->t_grp.prev = &thread->sibling;
    thread->sibling.next = &lwt->t_grp;
    lwt->t_grp.next = &thread->sibling;
    thread->sibling.prev = &lwt->t_grp;

    thread->cleanup = lwt_cleanup;

    rt_hw_interrupt_enable(IRID);

    //启动进程
    rt_thread_startup(thread);

_success:
    //引用次数
    lwt->ref = 1;
    return lwt_get_pid(lwt);//lwt_to_pid(lwt)

__fail:
    return -RT_ERROR;

}


/* 部分测试函数 */
#include "lwp.h"

void lwt_sub_thread_entry(void* parameter)
{
    rt_thread_t thread;
    struct rt_lwt* lwt;

    thread = rt_thread_self();
    lwt = thread->lwp;
    thread->cleanup = lwt_son_cleanup;
    //thread->stack_addr = 0;

    //仿照thread构造stack
    rt_uint8_t* stack_base = (char *)thread->user_stack + thread->user_stack_size - sizeof(rt_ubase_t) +  + sizeof(rt_uint32_t);
    stack_base = (rt_uint8_t *)RT_ALIGN_DOWN((rt_uint32_t)stack_base, 8);

    lwp_user_entry(parameter, thread->user_entry, lwt->data_entry, stack_base);
    
}

rt_thread_t sys_thread_create(const char *name,
                             void (*entry)(void *parameter),
                             void       *parameter,
                             void       *stack_addr,
                             rt_uint32_t stack_size,
                             rt_uint8_t  priority)
                             //rt_uint32_t tick
{
    struct rt_thread *thread = NULL;
    struct rt_lwp *lwp = NULL;

    //系统调用的是LWP进程
    struct rt_lwt *lwt;

    //entry要改成user_entry(即程序运行在用户态)
    thread = rt_thread_create(name, lwt_sub_thread_entry, parameter, 0x200, priority, 200);

    if(thread != NULL)
    {
        //CleanUp里面写了清Text_entry,所以需要判断引用次数才可以
        
        thread->cleanup = lwt_cleanup;

        lwt = rt_thread_self()->lwp;

        thread->lwp = lwt;

        thread->user_entry = entry;
        thread->user_stack = stack_addr;
        thread->user_stack_size = stack_size;
        rt_memset(stack_addr, '#', stack_size);//初始化堆栈

        rt_kprintf("LWT %s kernel stack %p - %p\n", thread->name, thread->stack_addr, (rt_uint32_t)thread->stack_addr + thread->stack_size);
        rt_kprintf("LWT %s app stack %p - %p\n", thread->name, thread->user_stack, (rt_uint32_t)thread->user_stack + thread->user_stack_size);


        rt_uint32_t level = rt_hw_interrupt_disable();


        //相对关系构造
        /**
         * (双向链表)
         * t_grp | init_sibling(old/first) | other_sibling(old) | create_sibling(new)
         */
        
        /**
         * Thread Group
         * if we create a new thread by rt_thread_create in a lwt in user APP, the new thread is parallel to old thread.
         * t_grp <=> old_thread->sibling ... <=> new_thread_sibling <=> t_grp
         * 
         * now <thread> point to itself
         **/
        rt_list_insert_before(&lwt->t_grp, &thread->sibling);

        /* for test */
        struct rt_list_node *n = lwt->t_grp.next;
        rt_thread_t t = RT_NULL;

        while(n != &lwt->t_grp)
        {
            thread = rt_list_entry(n, struct rt_thread, sibling);
            rt_kprintf("thread %s\n", thread->name);
            n = n->next;
            /**
             * thread thread.b 
             * thread dmeo_thr
             **/
        }


        rt_hw_interrupt_enable(level);

    }

    return thread;
}

rt_err_t sys_thread_startup(rt_thread_t thread)
{
    if(thread == NULL)
    {
        //
    }

    lwt_ref_inc(thread->lwp);

    return rt_thread_startup(thread);
}

long list_lwt(void)
{
    const char *item_title = "LWT";
    struct rt_lwt* lwt = RT_NULL;
    int maxlen = RT_NAME_MAX;

    rt_thread_t thread = RT_NULL;
    rt_list_t* n = RT_NULL;

    //rt_kprintf("%-*.s      cmd    suspend thread  pid\n", maxlen, item_title);
    rt_kprintf(" pid   cmd     stack_start  stack_size    thread\n", maxlen, item_title);
    rt_kprintf(" --- --------  -----------  ---------- -------------- -----\n");

    for(int pid = 0; pid < lwt_pid.lastpid; pid++)
    {
        if((lwt = lwt_pid.pidmap[pid]) == NULL)
            continue;
        n = lwt->t_grp.next;

        rt_kprintf(" %3d %-*.*s  0x%08x   0x%08x\n", pid, maxlen, RT_NAME_MAX, lwt->cmd, lwt->data_entry, lwt->data_size);
    
        //from old to new
        while(n != &lwt->t_grp)
        {
            thread = rt_list_entry(n, struct rt_thread, sibling);
            rt_kprintf("     %-*.*s  0x%08x   0x%08x\n", maxlen, RT_NAME_MAX, thread->name, thread->stack_addr, thread->stack_size);
            n = n->next;
        }
    
    }
    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(list_lwt, __cmd_showlwt, Show lwt.);

