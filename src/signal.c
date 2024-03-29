/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017/10/5      Bernard      the first version
 * 2018/09/17     Jesven       fix: in _signal_deliver RT_THREAD_STAT_MASK to RT_THREAD_STAT_SIGNAL_MASK
 * 2018/11/22     Jesven       in smp version rt_hw_context_switch_to add a param
 */

#include <stdint.h>
#include <string.h>

#include <rthw.h>
#include <rtthread.h>

#ifdef RT_USING_SIGNALS

#ifndef RT_SIG_INFO_MAX
#define RT_SIG_INFO_MAX 32
#endif

#define DBG_TAG     "SIGN"
#define DBG_LVL     DBG_WARNING
#include <rtdbg.h>

#define sig_mask(sig_no)    (1u << sig_no)
#define sig_valid(sig_no)   (sig_no >= 0 && sig_no < RT_SIG_MAX)

struct siginfo_node
{
    siginfo_t si;
    struct rt_slist_node list;
};

static struct rt_mempool *_rt_siginfo_pool;
static void _signal_deliver(rt_thread_t tid);
void rt_thread_handle_sig(rt_bool_t clean_state);

static void _signal_default_handler(int signo)
{
    LOG_I("handled signo[%d] with default action.", signo);
    return ;
}

static void _signal_entry(void *parameter)
{
    rt_thread_t tid = rt_thread_self();

    /* handle signal */
    rt_thread_handle_sig(RT_FALSE);

#ifdef RT_USING_SMP
    {
        struct rt_cpu* pcpu = rt_cpu_self();

        pcpu->current_thread->cpus_lock_nest--;
        if (pcpu->current_thread->cpus_lock_nest == 0)
        {
            pcpu->current_thread->scheduler_lock_nest--;
        }

    }
#else
    /* return to thread */
    tid->sp = tid->sig_ret;
    tid->sig_ret = RT_NULL;
#endif

    LOG_D("switch back to: 0x%08x\n", tid->sp);
    tid->stat &= ~RT_THREAD_STAT_SIGNAL;

#ifdef RT_USING_SMP
    rt_hw_context_switch_to((rt_base_t)&parameter, tid);
#else
    rt_hw_context_switch_to((rt_ubase_t)&(tid->sp));
#endif /*RT_USING_SMP*/
}

/*
 * To deliver a signal to thread, there are cases:
 * 1. When thread is suspended, function resumes(重返) thread and
 * set signal stat;
 * 2. When thread is ready:
 *   - If function delivers a signal to self thread, just handle
 *    it.
 *   - If function delivers a signal to another ready thread, OS
 *    should build a slice context to handle it.
 */
static void _signal_deliver(rt_thread_t tid)
{
    rt_ubase_t level;

    level = rt_hw_interrupt_disable();

    /* thread is not interested in pended signals */
    if (!(tid->sig_pending & tid->sig_mask))
    {
        rt_hw_interrupt_enable(level);
        return;
    }

    if ((tid->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
    {
        /* resume thread to handle signal */
        rt_thread_resume(tid);
        /* add signal state */
        tid->stat |= (RT_THREAD_STAT_SIGNAL | RT_THREAD_STAT_SIGNAL_PENDING);

        rt_hw_interrupt_enable(level);

        /* re-schedule */
        rt_schedule();
    }
    else
    {
        if (tid == rt_thread_self())
        {
            /* add signal state */
            tid->stat |= RT_THREAD_STAT_SIGNAL;

            rt_hw_interrupt_enable(level);

            /* do signal action in self thread context */
            if (rt_interrupt_get_nest() == 0)
            {
                rt_thread_handle_sig(RT_TRUE);
            }
        }
        else if (!((tid->stat & RT_THREAD_STAT_SIGNAL_MASK) & RT_THREAD_STAT_SIGNAL))
        {
            /* add signal state */
            tid->stat |= (RT_THREAD_STAT_SIGNAL | RT_THREAD_STAT_SIGNAL_PENDING);

#ifdef RT_USING_SMP
            {
                int cpu_id;

                cpu_id = tid->oncpu;
                if ((cpu_id != RT_CPU_DETACHED) && (cpu_id != rt_hw_cpu_id()))
                {
                    rt_uint32_t cpu_mask;

                    cpu_mask = RT_CPU_MASK ^ (1 << cpu_id);
                    rt_hw_ipi_send(RT_SCHEDULE_IPI, cpu_mask);
                }
            }
#else
            /* point to the signal handle entry */
            tid->stat &= ~RT_THREAD_STAT_SIGNAL_PENDING;
            tid->sig_ret = tid->sp;
            tid->sp = rt_hw_stack_init((void *)_signal_entry, RT_NULL,
                                       (void *)((char *)tid->sig_ret - 32), RT_NULL);
#endif

            rt_hw_interrupt_enable(level);
            LOG_D("signal stack pointer @ 0x%08x", tid->sp);

            /* re-schedule */
            rt_schedule();
        }
        else
        {
            rt_hw_interrupt_enable(level);
        }
    }
}

#ifdef RT_USING_SMP
void *rt_signal_check(void* context)
{
    rt_base_t level;
    int cpu_id;
    struct rt_cpu* pcpu;
    struct rt_thread *current_thread;

    level = rt_hw_interrupt_disable();
    cpu_id = rt_hw_cpu_id();
    pcpu   = rt_cpu_index(cpu_id);
    current_thread = pcpu->current_thread;

    if (pcpu->irq_nest)
    {
        rt_hw_interrupt_enable(level);
        return context;
    }

    if (current_thread->cpus_lock_nest == 1)
    {
        if (current_thread->stat & RT_THREAD_STAT_SIGNAL_PENDING)
        {
            void *sig_context;

            current_thread->stat &= ~RT_THREAD_STAT_SIGNAL_PENDING;

            rt_hw_interrupt_enable(level);
            sig_context = rt_hw_stack_init((void *)_signal_entry, context,
                    (void *)(context - 32), RT_NULL);
            return sig_context;
        }
    }
    rt_hw_interrupt_enable(level);
    return context;
}
#endif

rt_sighandler_t rt_signal_install(int signo, rt_sighandler_t handler)
{
    rt_base_t level;
    rt_sighandler_t old = RT_NULL;
    rt_thread_t tid = rt_thread_self();

    if (!sig_valid(signo)) return SIG_ERR;

    level = rt_hw_interrupt_disable();
    if (tid->sig_vectors == RT_NULL)
    {
        rt_thread_alloc_sig(tid);
    }

    if (tid->sig_vectors)
    {
        old = tid->sig_vectors[signo];

        if (handler == SIG_IGN) tid->sig_vectors[signo] = RT_NULL;
        else if (handler == SIG_DFL) tid->sig_vectors[signo] = _signal_default_handler;
        else tid->sig_vectors[signo] = handler;
    }
    rt_hw_interrupt_enable(level);

    return old;
}

void rt_signal_mask(int signo)
{
    rt_base_t level;
    rt_thread_t tid = rt_thread_self();

    level = rt_hw_interrupt_disable();

    tid->sig_mask &= ~sig_mask(signo);

    rt_hw_interrupt_enable(level);
}

void rt_signal_unmask(int signo)
{
    rt_base_t level;
    rt_thread_t tid = rt_thread_self();

    level = rt_hw_interrupt_disable();

    tid->sig_mask |= sig_mask(signo);

    /* let thread handle pended signals */
    if (tid->sig_mask & tid->sig_pending)
    {
        rt_hw_interrupt_enable(level);
        _signal_deliver(tid);
    }
    else
    {
        rt_hw_interrupt_enable(level);
    }
}

int rt_signal_wait(const rt_sigset_t *set, rt_siginfo_t *si, rt_int32_t timeout)
{
    int ret = RT_EOK;
    rt_base_t   level;
    rt_thread_t tid = rt_thread_self();
    struct siginfo_node *si_node = RT_NULL, *si_prev = RT_NULL;

    /* current context checking */
    RT_DEBUG_IN_THREAD_CONTEXT;

    /* parameters check */
    if (set == NULL || *set == 0 || si == NULL )
    {
        ret = -RT_EINVAL;
        goto __done_return;
    }

    /* clear siginfo to avoid unknown value */
    memset(si, 0x0, sizeof(rt_siginfo_t));

    level = rt_hw_interrupt_disable();

    /* already pending */
    if (tid->sig_pending & *set) goto __done;

    if (timeout == 0)
    {
        ret = -RT_ETIMEOUT;
        goto __done_int;
    }

    /* suspend self thread */
    rt_thread_suspend(tid);
    /* set thread stat as waiting for signal */
    tid->stat |= RT_THREAD_STAT_SIGNAL_WAIT;

    /* start timeout timer */
    if (timeout != RT_WAITING_FOREVER)
    {
        /* reset the timeout of thread timer and start it */
        rt_timer_control(&(tid->thread_timer),
                         RT_TIMER_CTRL_SET_TIME,
                         &timeout);
        rt_timer_start(&(tid->thread_timer));
    }
    rt_hw_interrupt_enable(level);

    /* do thread scheduling */
    rt_schedule();

    level = rt_hw_interrupt_disable();

    /* remove signal waiting flag */
    tid->stat &= ~RT_THREAD_STAT_SIGNAL_WAIT;

    /* check errno of thread */
    if (tid->error == -RT_ETIMEOUT)
    {
        tid->error = RT_EOK;
        rt_hw_interrupt_enable(level);

        /* timer timeout */
        ret = -RT_ETIMEOUT;
        goto __done_return;
    }

__done:
    /* to get the first matched pending signals */
    si_node = (struct siginfo_node *)tid->si_list;
    while (si_node)
    {
        int signo;

        signo = si_node->si.si_signo;
        if (sig_mask(signo) & *set)
        {
            *si  = si_node->si;

            LOG_D("sigwait: %d sig raised!", signo);
            if (si_prev) si_prev->list.next = si_node->list.next;
            else
            {
                struct siginfo_node *node_next;

                if (si_node->list.next)
                {
                    node_next = (void *)rt_slist_entry(si_node->list.next, struct siginfo_node, list);
                    tid->si_list = node_next;
                }
                else
                {
                    tid->si_list = RT_NULL;
                }
            }

            /* clear pending */
            tid->sig_pending &= ~sig_mask(signo);
            rt_mp_free(si_node);
            break;
        }

        si_prev = si_node;
        if (si_node->list.next)
        {
            si_node = (void *)rt_slist_entry(si_node->list.next, struct siginfo_node, list);
        }
        else
        {
            si_node = RT_NULL;
        }
     }

__done_int:
    rt_hw_interrupt_enable(level);

__done_return:
    return ret;
}

void rt_thread_handle_sig(rt_bool_t clean_state)
{
    rt_base_t level;

    rt_thread_t tid = rt_thread_self();
    struct siginfo_node *si_node;

    level = rt_hw_interrupt_disable();
    if (tid->sig_pending & tid->sig_mask)
    {
        /* if thread is not waiting for signal */
        if (!(tid->stat & RT_THREAD_STAT_SIGNAL_WAIT))
        {
            while (tid->sig_pending & tid->sig_mask)
            {
                int signo, error;
                rt_sighandler_t handler;

                si_node = (struct siginfo_node *)tid->si_list;
                if (!si_node) break;

                /* remove this sig info node from list */
                if (si_node->list.next == RT_NULL)
                    tid->si_list = RT_NULL;
                else
                    tid->si_list = (void *)rt_slist_entry(si_node->list.next, struct siginfo_node, list);

                signo   = si_node->si.si_signo;
                handler = tid->sig_vectors[signo];
                tid->sig_pending &= ~sig_mask(signo);
                rt_hw_interrupt_enable(level);

                LOG_D("handle signal: %d, handler 0x%08x", signo, handler);
                if (handler) handler(signo);

                level = rt_hw_interrupt_disable();
                error = -RT_EINTR;

                rt_mp_free(si_node); /* release this siginfo node */
                /* set errno in thread tcb */
                tid->error = error;
            }

            /* whether clean signal status */
            if (clean_state == RT_TRUE)
            {
                tid->stat &= ~RT_THREAD_STAT_SIGNAL;
            }
            else
            {
                return;
            }
        }
    }
    rt_hw_interrupt_enable(level);
}

void rt_thread_alloc_sig(rt_thread_t tid)
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

void rt_thread_free_sig(rt_thread_t tid)
{
    rt_base_t level;
    struct siginfo_node *si_node;
    rt_sighandler_t *sig_vectors;

    level = rt_hw_interrupt_disable();
    si_node = (struct siginfo_node *)tid->si_list;
    tid->si_list = RT_NULL;

    sig_vectors = tid->sig_vectors;
    tid->sig_vectors = RT_NULL;
    rt_hw_interrupt_enable(level);

    if (si_node)
    {
        struct rt_slist_node *node;
        struct rt_slist_node *node_to_free;

        LOG_D("free signal info list");
        node = &(si_node->list);
        do
        {
            node_to_free = node;
            node = node->next;
            si_node = rt_slist_entry(node_to_free, struct siginfo_node, list);
            rt_mp_free(si_node);
        } while (node);
    }

    if (sig_vectors)
    {
        RT_KERNEL_FREE(sig_vectors);
    }
}
#define SIGQUIT 3
int rt_thread_kill(rt_thread_t tid, int sig)
{
    siginfo_t si;
    rt_base_t level;
    struct siginfo_node *si_node;

    RT_ASSERT(tid != RT_NULL);
    if (!sig_valid(sig)) return -RT_EINVAL;

    LOG_I("send signal: %d", sig);
    si.si_signo = sig;
    si.si_code  = SI_USER;
    si.si_value.sival_ptr = RT_NULL;

    level = rt_hw_interrupt_disable();

    if(sig == SIGQUIT)
    {
        rt_thread_delete(tid);
        //到这里都是特权级状态，直接删除tid，那么状态还是特权级下
        //rt_schedule();
        rt_hw_interrupt_enable(level);
        return RT_EOK;
    }

    if (tid->sig_pending & sig_mask(sig))
    {
        /* whether already emits this signal? */
        struct rt_slist_node *node;
        struct siginfo_node  *entry;

        si_node = (struct siginfo_node *)tid->si_list;
        if (si_node)
            node = (struct rt_slist_node *)&si_node->list;
        else
            node = RT_NULL;

        /* update sig info */
        for (; (node) != RT_NULL; node = node->next)
        {
            entry = rt_slist_entry(node, struct siginfo_node, list);
            if (entry->si.si_signo == sig)
            {
                memcpy(&(entry->si), &si, sizeof(siginfo_t));
                rt_hw_interrupt_enable(level);
                return 0;
            }
        }
    }
    rt_hw_interrupt_enable(level);

    si_node = (struct siginfo_node *) rt_mp_alloc(_rt_siginfo_pool, 0);
    if (si_node)
    {
        rt_slist_init(&(si_node->list));
        memcpy(&(si_node->si), &si, sizeof(siginfo_t));

        level = rt_hw_interrupt_disable();

        if (tid->si_list)
        {
            struct siginfo_node *si_list;

            si_list = (struct siginfo_node *)tid->si_list;
            rt_slist_append(&(si_list->list), &(si_node->list));
        }
        else
        {
            tid->si_list = si_node;
        }

        /* a new signal */
        tid->sig_pending |= sig_mask(sig);

        rt_hw_interrupt_enable(level);
    }
    else
    {
        LOG_E("The allocation of signal info node failed.");
    }

    /* deliver signal to this thread */
    _signal_deliver(tid);

    return RT_EOK;
}

int rt_system_signal_init(void)
{
    _rt_siginfo_pool = rt_mp_create("signal", RT_SIG_INFO_MAX, sizeof(struct siginfo_node));
    if (_rt_siginfo_pool == RT_NULL)
    {
        LOG_E("create memory pool for signal info failed.");
        RT_ASSERT(0);
    }

    return 0;
}

#include "lwt.h"

/**
 * 给线程发送信号
 * 比如 linux 中 kill -9 pid 是杀掉一个线程
 * 线程里面可能有多个进程,需要全部发送
 */
extern struct rt_lwt * lwt_get_lwt_from_pid(pid_t pid);
int lwt_kill(pid_t pid,int sig)
{
    rt_base_t level;
    struct rt_lwt* lwt = lwt_get_lwt_from_pid(pid);
    rt_thread_t tid = RT_NULL;

    struct rt_list_node *child;
    rt_thread_t thread = RT_NULL;

    if(lwt == RT_NULL)
    {
        rt_kprintf("PID %d not find!\n", pid);
    }else{
        /**
         * kill lwt, lwt maybe has many thread.
         * we need to kill the thead from the newest one to the oldest one
         * maybe because them are Parent Child Relationship
         * 
         * t_grp <=> oldest <=> ... <=> newest <=> t_grp
         **/
        rt_kprintf("PID %d find!\n", pid);
        child = lwt->t_grp.prev;
        //TODO: 还要考虑 lwt的child要不要一并kill掉, 这些场景是什么?
        while(child != &lwt->t_grp)
        {
            thread = rt_list_entry(child, struct rt_thread, sibling);
            rt_kprintf("kill thread %s\n", thread->name);
            child = child->prev;
            rt_thread_kill(thread, sig);

        }
        
        //rt_kprintf("thread %s\n", thread->name);

        //rt_thread_kill(tid, sig);
    }

    //level = rt_hw_interrupt_disable();

    //rt_hw_interrupt_enable(level);

    return 0;
}
/**
 * kill 2
 * kill 4
 */
void cmd_kill(int argc,char **argv)
{
    pid_t pid = -1;
    int sig = SIGQUIT;

    if(argc >= 2)
    {
        pid = atoi(argv[1]);
        if(argc > 4)
        {
            sig = atoi(argv[3]);
        }
        lwt_kill(pid, sig);
    }else{
        rt_kprintf("arg none!\n");
    }
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_kill, __cmd_kill, kill signal);

void cmd_killall(int argc,char **argv)
{
    pid_t pid = -1;
    
}

#endif

#define THREAD_PRIORITY         25
#define THREAD_STACK_SIZE       512
#define THREAD_TIMESLICE        5

static rt_thread_t tid1 = RT_NULL;

/* 线程 1 的信号处理函数 */
void thread1_signal_handler(int sig)
{
    rt_kprintf("thread1 received signal %d\n", sig);
}

/* 线程 1 的入口函数 */
static void thread1_entry(void *parameter)
{
    int cnt = 0;

    /* 安装信号 */
    rt_signal_install(SIGUSR1, thread1_signal_handler);
    rt_signal_unmask(SIGUSR1);

    /* 运行 10 次 */
    while (cnt < 10)
    {
        /* 线程 1 采用低优先级运行，一直打印计数值 */
        rt_kprintf("thread1 count : %d\n", cnt);

        cnt++;
        rt_thread_mdelay(100);
    }
}

/* 信号示例的初始化 */
int signal_sample(void)
{
    /* 创建线程 1 */
    tid1 = rt_thread_create("thread1",
                            thread1_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);

    if (tid1 != RT_NULL)
        rt_thread_startup(tid1);

    rt_thread_mdelay(300);

    /* 发送信号 SIGUSR1 给线程 1 */
    rt_thread_kill(tid1, SIGUSR1);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(signal_sample, signal sample);

