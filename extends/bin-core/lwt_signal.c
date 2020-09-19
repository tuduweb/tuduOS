#include <rthw.h>
#include <rtthread.h>

#ifndef RT_SIG_INFO_MAX
#define RT_SIG_INFO_MAX 32
#endif

#define DBG_TAG     "SIGN"
#define DBG_LVL     DBG_WARNING
#include <rtdbg.h>

static void _signal_default_handler(int signo)
{
    LOG_I("handled signo[%d] with default action.", signo);
    return ;
}

void lwt_thread_alloc_sig(rt_thread_t tid)
{
    int index;
    rt_base_t level;
    rt_sighandler_t *vectors;

    vectors = (rt_sighandler_t *)RT_KERNEL_MALLOC(sizeof(rt_sighandler_t) * RT_SIG_MAX);
    RT_ASSERT(vectors != RT_NULL);

    for (index = 0; index < RT_SIG_MAX; index ++)
    {
        vectors[index] = _signal_default_handler;
    }

    level = rt_hw_interrupt_disable();
    tid->sig_vectors = vectors;
    rt_hw_interrupt_enable(level);
}

#define sig_mask(sig_no)    (1u << sig_no)
#define sig_valid(sig_no)   (sig_no >= 0 && sig_no < RT_SIG_MAX)

rt_sighandler_t lwt_sighandler_set(int signo, rt_sighandler_t handler)
{
    rt_base_t level;
    rt_sighandler_t old = RT_NULL;
    rt_thread_t tid = rt_thread_self();

    if (!sig_valid(signo)) return SIG_ERR;

    if(tid->sig_vectors == RT_NULL)
    {
        //当还没相关内存空间时 申请内存
        lwt_thread_alloc_sig(tid);
    }

    if(tid->sig_vectors)
    {
        old = tid->sig_vectors[signo];

        if (handler == SIG_IGN) tid->sig_vectors[signo] = RT_NULL;
        else if (handler == SIG_DFL) tid->sig_vectors[signo] = _signal_default_handler;
        else tid->sig_vectors[signo] = handler;
    }

    rt_hw_interrupt_enable(level);

    return old;
}

rt_sighandler_t lwt_sighandler_get(int signo)
{
    rt_base_t level = rt_hw_interrupt_disable();
    rt_thread_t tid = rt_thread_self();



    rt_hw_interrupt_enable(level);

    return _signal_default_handler;
}

#include "lwt.h"

int lwt_kills(pid_t pid,int sig)
{
    //
}


