/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-14     Bernard      the first version
 * 2006-04-25     Bernard      implement semaphore
 * 2006-05-03     Bernard      add RT_IPC_DEBUG
 *                             modify the type of IPC waiting time to rt_int32_t
 * 2006-05-10     Bernard      fix the semaphore take bug and add IPC object
 * 2006-05-12     Bernard      implement mailbox and message queue
 * 2006-05-20     Bernard      implement mutex
 * 2006-05-23     Bernard      implement fast event
 * 2006-05-24     Bernard      implement event
 * 2006-06-03     Bernard      fix the thread timer init bug
 * 2006-06-05     Bernard      fix the mutex release bug
 * 2006-06-07     Bernard      fix the message queue send bug
 * 2006-08-04     Bernard      add hook support
 * 2009-05-21     Yi.qiu       fix the sem release bug
 * 2009-07-18     Bernard      fix the event clear bug
 * 2009-09-09     Bernard      remove fast event and fix ipc release bug
 * 2009-10-10     Bernard      change semaphore and mutex value to unsigned value
 * 2009-10-25     Bernard      change the mb/mq receive timeout to 0 if the
 *                             re-calculated delta tick is a negative number.
 * 2009-12-16     Bernard      fix the rt_ipc_object_suspend issue when IPC flag
 *                             is RT_IPC_FLAG_PRIO
 * 2010-01-20     mbbill       remove rt_ipc_object_decrease function.
 * 2010-04-20     Bernard      move memcpy outside interrupt disable in mq
 * 2010-10-26     yi.qiu       add module support in rt_mp_delete and rt_mq_delete
 * 2010-11-10     Bernard      add IPC reset command implementation.
 * 2011-12-18     Bernard      add more parameter checking in message queue
 * 2013-09-14     Grissiom     add an option check in rt_event_recv
 * 2018-10-02     Bernard      add 64bit support for mailbox
 * 2019-09-16     tyx          add send wait support for message queue
 */

#include <rtthread.h>
#include <rthw.h>

#ifdef RT_USING_HOOK
extern void (*rt_object_trytake_hook)(struct rt_object *object);
extern void (*rt_object_take_hook)(struct rt_object *object);
extern void (*rt_object_put_hook)(struct rt_object *object);
#endif

/**
 * @addtogroup IPC
 */

/**@{*/

/**
 * This function will initialize an IPC object
 *
 * @param ipc the IPC object
 *
 * @return the operation status, RT_EOK on successful
 */
rt_inline rt_err_t rt_ipc_object_init(struct rt_ipc_object *ipc)
{
    /* initialize ipc object */
    rt_list_init(&(ipc->suspend_thread));

    return RT_EOK;
}

/**
 * This function will suspend a thread to a specified list. IPC object or some
 * double-queue object (mailbox etc.) contains this kind of list.
 *
 * @param list the IPC suspended thread list
 * @param thread the thread object to be suspended
 * @param flag the IPC object flag,
 *        which shall be RT_IPC_FLAG_FIFO/RT_IPC_FLAG_PRIO.
 *
 * @return the operation status, RT_EOK on successful
 */
rt_inline rt_err_t rt_ipc_list_suspend(rt_list_t        *list,
                                       struct rt_thread *thread,
                                       rt_uint8_t        flag)
{
    /* suspend thread */
    rt_thread_suspend(thread);

    switch (flag)
    {
    case RT_IPC_FLAG_FIFO:
        rt_list_insert_before(list, &(thread->tlist));
        break;

    case RT_IPC_FLAG_PRIO:
        {
            struct rt_list_node *n;
            struct rt_thread *sthread;

            /* find a suitable position */
            for (n = list->next; n != list; n = n->next)
            {
                sthread = rt_list_entry(n, struct rt_thread, tlist);

                /* find out */
                if (thread->current_priority < sthread->current_priority)
                {
                    /* insert this thread before the sthread */
                    rt_list_insert_before(&(sthread->tlist), &(thread->tlist));
                    break;
                }
            }

            /*
             * not found a suitable position,
             * append to the end of suspend_thread list
             */
            if (n == list)
                rt_list_insert_before(list, &(thread->tlist));
        }
        break;
    }

    return RT_EOK;
}

/**
 * This function will resume the first thread in the list of a IPC object:
 * - remove the thread from suspend queue of IPC object
 * - put the thread into system ready queue
 *
 * @param list the thread list
 *
 * @return the operation status, RT_EOK on successful
 */
rt_inline rt_err_t rt_ipc_list_resume(rt_list_t *list)
{
    struct rt_thread *thread;

    /* get thread entry */
    thread = rt_list_entry(list->next, struct rt_thread, tlist);

    RT_DEBUG_LOG(RT_DEBUG_IPC, ("resume thread:%s\n", thread->name));

    /* resume it */
    rt_thread_resume(thread);

    return RT_EOK;
}

/**
 * This function will resume all suspended threads in a list, including
 * suspend list of IPC object and private list of mailbox etc.
 *
 * @param list of the threads to resume
 *
 * @return the operation status, RT_EOK on successful
 */
rt_inline rt_err_t rt_ipc_list_resume_all(rt_list_t *list)
{
    struct rt_thread *thread;
    register rt_ubase_t temp;

    /* wakeup all suspended threads */
    while (!rt_list_isempty(list))
    {
        /* disable interrupt */
        temp = rt_hw_interrupt_disable();

        /* get next suspended thread */
        thread = rt_list_entry(list->next, struct rt_thread, tlist);
        /* set error code to RT_ERROR */
        thread->error = -RT_ERROR;

        /*
         * resume thread
         * In rt_thread_resume function, it will remove current thread from
         * suspended list
         */
        rt_thread_resume(thread);

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);
    }

    return RT_EOK;
}

#ifdef RT_USING_SEMAPHORE
/**
 * This function will initialize a semaphore and put it under control of
 * resource management.
 *
 * @param sem the semaphore object
 * @param name the name of semaphore
 * @param value the initial value of semaphore
 * @param flag the flag of semaphore
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_sem_init(rt_sem_t    sem,
                     const char *name,
                     rt_uint32_t value,
                     rt_uint8_t  flag)
{
    RT_ASSERT(sem != RT_NULL);
    RT_ASSERT(value < 0x10000U);

    /* initialize object */
    rt_object_init(&(sem->parent.parent), RT_Object_Class_Semaphore, name);

    /* initialize ipc object */
    rt_ipc_object_init(&(sem->parent));

    /* set initial value */
    sem->value = (rt_uint16_t)value;

    /* set parent */
    sem->parent.parent.flag = flag;

    return RT_EOK;
}
RTM_EXPORT(rt_sem_init);

/**
 * This function will detach a semaphore from resource management
 *
 * @param sem the semaphore object
 *
 * @return the operation status, RT_EOK on successful
 *
 * @see rt_sem_delete
 */
rt_err_t rt_sem_detach(rt_sem_t sem)
{
    /* parameter check */
    RT_ASSERT(sem != RT_NULL);
    RT_ASSERT(rt_object_get_type(&sem->parent.parent) == RT_Object_Class_Semaphore);
    RT_ASSERT(rt_object_is_systemobject(&sem->parent.parent));

    /* wakeup all suspended threads */
    rt_ipc_list_resume_all(&(sem->parent.suspend_thread));

    /* detach semaphore object */
    rt_object_detach(&(sem->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_sem_detach);

#ifdef RT_USING_HEAP
/**
 * This function will create a semaphore from system resource
 *
 * @param name the name of semaphore
 * @param value the initial value of semaphore
 * @param flag the flag of semaphore
 *
 * @return the created semaphore, RT_NULL on error happen
 *
 * @see rt_sem_init
 */
rt_sem_t rt_sem_create(const char *name, rt_uint32_t value, rt_uint8_t flag)
{
    rt_sem_t sem;

    RT_DEBUG_NOT_IN_INTERRUPT;
    RT_ASSERT(value < 0x10000U);

    /* allocate object */
    sem = (rt_sem_t)rt_object_allocate(RT_Object_Class_Semaphore, name);
    if (sem == RT_NULL)
        return sem;

    /* initialize ipc object */
    rt_ipc_object_init(&(sem->parent));

    /* set initial value */
    sem->value = value;

    /* set parent */
    sem->parent.parent.flag = flag;

    return sem;
}
RTM_EXPORT(rt_sem_create);

/**
 * This function will delete a semaphore object and release the memory
 *
 * @param sem the semaphore object
 *
 * @return the error code
 *
 * @see rt_sem_detach
 */
rt_err_t rt_sem_delete(rt_sem_t sem)
{
    RT_DEBUG_NOT_IN_INTERRUPT;

    /* parameter check */
    RT_ASSERT(sem != RT_NULL);
    RT_ASSERT(rt_object_get_type(&sem->parent.parent) == RT_Object_Class_Semaphore);
    RT_ASSERT(rt_object_is_systemobject(&sem->parent.parent) == RT_FALSE);

    /* wakeup all suspended threads */
    rt_ipc_list_resume_all(&(sem->parent.suspend_thread));

    /* delete semaphore object */
    rt_object_delete(&(sem->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_sem_delete);
#endif

/**
 * This function will take a semaphore, if the semaphore is unavailable, the
 * thread shall wait for a specified time.
 *
 * @param sem the semaphore object
 * @param time the waiting time
 *
 * @return the error code
 */
rt_err_t rt_sem_take(rt_sem_t sem, rt_int32_t time)
{
    register rt_base_t temp;
    struct rt_thread *thread;

    /* parameter check */
    RT_ASSERT(sem != RT_NULL);
    RT_ASSERT(rt_object_get_type(&sem->parent.parent) == RT_Object_Class_Semaphore);

    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(sem->parent.parent)));

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    RT_DEBUG_LOG(RT_DEBUG_IPC, ("thread %s take sem:%s, which value is: %d\n",
                                rt_thread_self()->name,
                                ((struct rt_object *)sem)->name,
                                sem->value));

    if (sem->value > 0)
    {
        /* semaphore is available */
        sem->value --;

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);
    }
    else
    {
        /* no waiting, return with timeout */
        if (time == 0)
        {
            rt_hw_interrupt_enable(temp);

            return -RT_ETIMEOUT;
        }
        else
        {
            /* current context checking */
            RT_DEBUG_IN_THREAD_CONTEXT;

            /* semaphore is unavailable, push to suspend list */
            /* get current thread */
            thread = rt_thread_self();

            /* reset thread error number */
            thread->error = RT_EOK;

            RT_DEBUG_LOG(RT_DEBUG_IPC, ("sem take: suspend thread - %s\n",
                                        thread->name));

            /* suspend thread */
            rt_ipc_list_suspend(&(sem->parent.suspend_thread),
                                thread,
                                sem->parent.parent.flag);

            /* has waiting time, start thread timer */
            if (time > 0)
            {
                RT_DEBUG_LOG(RT_DEBUG_IPC, ("set thread:%s to timer list\n",
                                            thread->name));

                /* reset the timeout of thread timer and start it */
                rt_timer_control(&(thread->thread_timer),
                                 RT_TIMER_CTRL_SET_TIME,
                                 &time);
                rt_timer_start(&(thread->thread_timer));
            }

            /* enable interrupt */
            rt_hw_interrupt_enable(temp);

            /* do schedule */
            rt_schedule();

            if (thread->error != RT_EOK)
            {
                return thread->error;
            }
        }
    }

    RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(sem->parent.parent)));

    return RT_EOK;
}
RTM_EXPORT(rt_sem_take);

/**
 * This function will try to take a semaphore and immediately return
 *
 * @param sem the semaphore object
 *
 * @return the error code
 */
rt_err_t rt_sem_trytake(rt_sem_t sem)
{
    return rt_sem_take(sem, 0);
}
RTM_EXPORT(rt_sem_trytake);

/**
 * This function will release a semaphore, if there are threads suspended on
 * semaphore, it will be waked up.
 *
 * @param sem the semaphore object
 *
 * @return the error code
 */
rt_err_t rt_sem_release(rt_sem_t sem)
{
    register rt_base_t temp;
    register rt_bool_t need_schedule;

    /* parameter check */
    RT_ASSERT(sem != RT_NULL);
    RT_ASSERT(rt_object_get_type(&sem->parent.parent) == RT_Object_Class_Semaphore);

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(sem->parent.parent)));

    need_schedule = RT_FALSE;

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    RT_DEBUG_LOG(RT_DEBUG_IPC, ("thread %s releases sem:%s, which value is: %d\n",
                                rt_thread_self()->name,
                                ((struct rt_object *)sem)->name,
                                sem->value));

    if (!rt_list_isempty(&sem->parent.suspend_thread))
    {
        /* resume the suspended thread */
        rt_ipc_list_resume(&(sem->parent.suspend_thread));
        need_schedule = RT_TRUE;
    }
    else
        sem->value ++; /* increase value */

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    /* resume a thread, re-schedule */
    if (need_schedule == RT_TRUE)
        rt_schedule();

    return RT_EOK;
}
RTM_EXPORT(rt_sem_release);

/**
 * This function can get or set some extra attributions of a semaphore object.
 *
 * @param sem the semaphore object
 * @param cmd the execution command
 * @param arg the execution argument
 *
 * @return the error code
 */
rt_err_t rt_sem_control(rt_sem_t sem, int cmd, void *arg)
{
    rt_ubase_t level;

    /* parameter check */
    RT_ASSERT(sem != RT_NULL);
    RT_ASSERT(rt_object_get_type(&sem->parent.parent) == RT_Object_Class_Semaphore);

    if (cmd == RT_IPC_CMD_RESET)
    {
        rt_ubase_t value;

        /* get value */
        value = (rt_ubase_t)arg;
        /* disable interrupt */
        level = rt_hw_interrupt_disable();

        /* resume all waiting thread */
        rt_ipc_list_resume_all(&sem->parent.suspend_thread);

        /* set new value */
        sem->value = (rt_uint16_t)value;

        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        rt_schedule();

        return RT_EOK;
    }

    return -RT_ERROR;
}
RTM_EXPORT(rt_sem_control);
#endif /* end of RT_USING_SEMAPHORE */

#ifdef RT_USING_MUTEX
/**
 * This function will initialize a mutex and put it under control of resource
 * management.
 *
 * @param mutex the mutex object
 * @param name the name of mutex
 * @param flag the flag of mutex
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_mutex_init(rt_mutex_t mutex, const char *name, rt_uint8_t flag)
{
    /* parameter check */
    RT_ASSERT(mutex != RT_NULL);

    /* initialize object */
    rt_object_init(&(mutex->parent.parent), RT_Object_Class_Mutex, name);

    /* initialize ipc object */
    rt_ipc_object_init(&(mutex->parent));

    mutex->value = 1;
    mutex->owner = RT_NULL;
    mutex->original_priority = 0xFF;
    mutex->hold  = 0;

    /* set flag */
    mutex->parent.parent.flag = flag;

    return RT_EOK;
}
RTM_EXPORT(rt_mutex_init);

/**
 * This function will detach a mutex from resource management
 *
 * @param mutex the mutex object
 *
 * @return the operation status, RT_EOK on successful
 *
 * @see rt_mutex_delete
 */
rt_err_t rt_mutex_detach(rt_mutex_t mutex)
{
    /* parameter check */
    RT_ASSERT(mutex != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mutex->parent.parent) == RT_Object_Class_Mutex);
    RT_ASSERT(rt_object_is_systemobject(&mutex->parent.parent));

    /* wakeup all suspended threads */
    rt_ipc_list_resume_all(&(mutex->parent.suspend_thread));

    /* detach semaphore object */
    rt_object_detach(&(mutex->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_mutex_detach);

#ifdef RT_USING_HEAP
/**
 * This function will create a mutex from system resource
 *
 * @param name the name of mutex
 * @param flag the flag of mutex
 *
 * @return the created mutex, RT_NULL on error happen
 *
 * @see rt_mutex_init
 */
rt_mutex_t rt_mutex_create(const char *name, rt_uint8_t flag)
{
    struct rt_mutex *mutex;

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* allocate object */
    mutex = (rt_mutex_t)rt_object_allocate(RT_Object_Class_Mutex, name);
    if (mutex == RT_NULL)
        return mutex;

    /* initialize ipc object */
    rt_ipc_object_init(&(mutex->parent));

    mutex->value              = 1;
    mutex->owner              = RT_NULL;
    mutex->original_priority  = 0xFF;
    mutex->hold               = 0;

    /* set flag */
    mutex->parent.parent.flag = flag;

    return mutex;
}
RTM_EXPORT(rt_mutex_create);

/**
 * This function will delete a mutex object and release the memory
 *
 * @param mutex the mutex object
 *
 * @return the error code
 *
 * @see rt_mutex_detach
 */
rt_err_t rt_mutex_delete(rt_mutex_t mutex)
{
    RT_DEBUG_NOT_IN_INTERRUPT;

    /* parameter check */
    RT_ASSERT(mutex != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mutex->parent.parent) == RT_Object_Class_Mutex);
    RT_ASSERT(rt_object_is_systemobject(&mutex->parent.parent) == RT_FALSE);

    /* wakeup all suspended threads */
    rt_ipc_list_resume_all(&(mutex->parent.suspend_thread));

    /* delete mutex object */
    rt_object_delete(&(mutex->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_mutex_delete);
#endif

/**
 * This function will take a mutex, if the mutex is unavailable, the
 * thread shall wait for a specified time.
 *
 * @param mutex the mutex object
 * @param time the waiting time
 *
 * @return the error code
 */
rt_err_t rt_mutex_take(rt_mutex_t mutex, rt_int32_t time)
{
    register rt_base_t temp;
    struct rt_thread *thread;

    /* this function must not be used in interrupt even if time = 0 */
    RT_DEBUG_IN_THREAD_CONTEXT;

    /* parameter check */
    RT_ASSERT(mutex != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mutex->parent.parent) == RT_Object_Class_Mutex);

    /* get current thread */
    thread = rt_thread_self();

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(mutex->parent.parent)));

    RT_DEBUG_LOG(RT_DEBUG_IPC,
                 ("mutex_take: current thread %s, mutex value: %d, hold: %d\n",
                  thread->name, mutex->value, mutex->hold));

    /* reset thread error */
    thread->error = RT_EOK;

    if (mutex->owner == thread)
    {
        /* it's the same thread */
        mutex->hold ++;
    }
    else
    {
__again:
        /* The value of mutex is 1 in initial status. Therefore, if the
         * value is great than 0, it indicates the mutex is avaible.
         */
        if (mutex->value > 0)
        {
            /* mutex is available */
            mutex->value --;

            /* set mutex owner and original priority */
            mutex->owner             = thread;
            mutex->original_priority = thread->current_priority;
            mutex->hold ++;
        }
        else
        {
            /* no waiting, return with timeout */
            if (time == 0)
            {
                /* set error as timeout */
                thread->error = -RT_ETIMEOUT;

                /* enable interrupt */
                rt_hw_interrupt_enable(temp);

                return -RT_ETIMEOUT;
            }
            else
            {
                /* mutex is unavailable, push to suspend list */
                RT_DEBUG_LOG(RT_DEBUG_IPC, ("mutex_take: suspend thread: %s\n",
                                            thread->name));

                /* change the owner thread priority of mutex */
                if (thread->current_priority < mutex->owner->current_priority)
                {
                    /* change the owner thread priority */
                    rt_thread_control(mutex->owner,
                                      RT_THREAD_CTRL_CHANGE_PRIORITY,
                                      &thread->current_priority);
                }

                /* suspend current thread */
                rt_ipc_list_suspend(&(mutex->parent.suspend_thread),
                                    thread,
                                    mutex->parent.parent.flag);

                /* has waiting time, start thread timer */
                if (time > 0)
                {
                    RT_DEBUG_LOG(RT_DEBUG_IPC,
                                 ("mutex_take: start the timer of thread:%s\n",
                                  thread->name));

                    /* reset the timeout of thread timer and start it */
                    rt_timer_control(&(thread->thread_timer),
                                     RT_TIMER_CTRL_SET_TIME,
                                     &time);
                    rt_timer_start(&(thread->thread_timer));
                }

                /* enable interrupt */
                rt_hw_interrupt_enable(temp);

                /* do schedule */
                rt_schedule();

                if (thread->error != RT_EOK)
                {
                    /* interrupt by signal, try it again */
                    if (thread->error == -RT_EINTR) goto __again;

                    /* return error */
                    return thread->error;
                }
                else
                {
                    /* the mutex is taken successfully. */
                    /* disable interrupt */
                    temp = rt_hw_interrupt_disable();
                }
            }
        }
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(mutex->parent.parent)));

    return RT_EOK;
}
RTM_EXPORT(rt_mutex_take);

/**
 * This function will release a mutex, if there are threads suspended on mutex,
 * it will be waked up.
 *
 * @param mutex the mutex object
 *
 * @return the error code
 */
rt_err_t rt_mutex_release(rt_mutex_t mutex)
{
    register rt_base_t temp;
    struct rt_thread *thread;
    rt_bool_t need_schedule;

    /* parameter check */
    RT_ASSERT(mutex != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mutex->parent.parent) == RT_Object_Class_Mutex);

    need_schedule = RT_FALSE;

    /* only thread could release mutex because we need test the ownership */
    RT_DEBUG_IN_THREAD_CONTEXT;

    /* get current thread */
    thread = rt_thread_self();

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    RT_DEBUG_LOG(RT_DEBUG_IPC,
                 ("mutex_release:current thread %s, mutex value: %d, hold: %d\n",
                  thread->name, mutex->value, mutex->hold));

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(mutex->parent.parent)));

    /* mutex only can be released by owner */
    if (thread != mutex->owner)
    {
        thread->error = -RT_ERROR;

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        return -RT_ERROR;
    }

    /* decrease hold */
    mutex->hold --;
    /* if no hold */
    if (mutex->hold == 0)
    {
        /* change the owner thread to original priority */
        if (mutex->original_priority != mutex->owner->current_priority)
        {
            rt_thread_control(mutex->owner,
                              RT_THREAD_CTRL_CHANGE_PRIORITY,
                              &(mutex->original_priority));
        }

        /* wakeup suspended thread */
        if (!rt_list_isempty(&mutex->parent.suspend_thread))
        {
            /* get suspended thread */
            thread = rt_list_entry(mutex->parent.suspend_thread.next,
                                   struct rt_thread,
                                   tlist);

            RT_DEBUG_LOG(RT_DEBUG_IPC, ("mutex_release: resume thread: %s\n",
                                        thread->name));

            /* set new owner and priority */
            mutex->owner             = thread;
            mutex->original_priority = thread->current_priority;
            mutex->hold ++;

            /* resume thread */
            rt_ipc_list_resume(&(mutex->parent.suspend_thread));

            need_schedule = RT_TRUE;
        }
        else
        {
            /* increase value */
            mutex->value ++;

            /* clear owner */
            mutex->owner             = RT_NULL;
            mutex->original_priority = 0xff;
        }
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    /* perform a schedule */
    if (need_schedule == RT_TRUE)
        rt_schedule();

    return RT_EOK;
}
RTM_EXPORT(rt_mutex_release);

/**
 * This function can get or set some extra attributions of a mutex object.
 *
 * @param mutex the mutex object
 * @param cmd the execution command
 * @param arg the execution argument
 *
 * @return the error code
 */
rt_err_t rt_mutex_control(rt_mutex_t mutex, int cmd, void *arg)
{
    /* parameter check */
    RT_ASSERT(mutex != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mutex->parent.parent) == RT_Object_Class_Mutex);

    return -RT_ERROR;
}
RTM_EXPORT(rt_mutex_control);
#endif /* end of RT_USING_MUTEX */

#ifdef RT_USING_EVENT
/**
 * This function will initialize an event and put it under control of resource
 * management.
 *
 * @param event the event object
 * @param name the name of event
 * @param flag the flag of event
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_event_init(rt_event_t event, const char *name, rt_uint8_t flag)
{
    /* parameter check */
    RT_ASSERT(event != RT_NULL);

    /* initialize object */
    rt_object_init(&(event->parent.parent), RT_Object_Class_Event, name);

    /* set parent flag */
    event->parent.parent.flag = flag;

    /* initialize ipc object */
    rt_ipc_object_init(&(event->parent));

    /* initialize event */
    event->set = 0;

    return RT_EOK;
}
RTM_EXPORT(rt_event_init);

/**
 * This function will detach an event object from resource management
 *
 * @param event the event object
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_event_detach(rt_event_t event)
{
    /* parameter check */
    RT_ASSERT(event != RT_NULL);
    RT_ASSERT(rt_object_get_type(&event->parent.parent) == RT_Object_Class_Event);
    RT_ASSERT(rt_object_is_systemobject(&event->parent.parent));

    /* resume all suspended thread */
    rt_ipc_list_resume_all(&(event->parent.suspend_thread));

    /* detach event object */
    rt_object_detach(&(event->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_event_detach);

#ifdef RT_USING_HEAP
/**
 * This function will create an event object from system resource
 *
 * @param name the name of event
 * @param flag the flag of event
 *
 * @return the created event, RT_NULL on error happen
 */
rt_event_t rt_event_create(const char *name, rt_uint8_t flag)
{
    rt_event_t event;

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* allocate object */
    event = (rt_event_t)rt_object_allocate(RT_Object_Class_Event, name);
    if (event == RT_NULL)
        return event;

    /* set parent */
    event->parent.parent.flag = flag;

    /* initialize ipc object */
    rt_ipc_object_init(&(event->parent));

    /* initialize event */
    event->set = 0;

    return event;
}
RTM_EXPORT(rt_event_create);

/**
 * This function will delete an event object and release the memory
 *
 * @param event the event object
 *
 * @return the error code
 */
rt_err_t rt_event_delete(rt_event_t event)
{
    /* parameter check */
    RT_ASSERT(event != RT_NULL);
    RT_ASSERT(rt_object_get_type(&event->parent.parent) == RT_Object_Class_Event);
    RT_ASSERT(rt_object_is_systemobject(&event->parent.parent) == RT_FALSE);

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* resume all suspended thread */
    rt_ipc_list_resume_all(&(event->parent.suspend_thread));

    /* delete event object */
    rt_object_delete(&(event->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_event_delete);
#endif

/**
 * This function will send an event to the event object, if there are threads
 * suspended on event object, it will be waked up.
 *
 * @param event the event object
 * @param set the event set
 *
 * @return the error code
 */
rt_err_t rt_event_send(rt_event_t event, rt_uint32_t set)
{
    struct rt_list_node *n;
    struct rt_thread *thread;
    register rt_ubase_t level;
    register rt_base_t status;
    rt_bool_t need_schedule;

    /* parameter check */
    RT_ASSERT(event != RT_NULL);
    RT_ASSERT(rt_object_get_type(&event->parent.parent) == RT_Object_Class_Event);

    if (set == 0)
        return -RT_ERROR;

    need_schedule = RT_FALSE;

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /* set event */
    event->set |= set;

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(event->parent.parent)));

    if (!rt_list_isempty(&event->parent.suspend_thread))
    {
        /* search thread list to resume thread */
        n = event->parent.suspend_thread.next;
        while (n != &(event->parent.suspend_thread))
        {
            /* get thread */
            thread = rt_list_entry(n, struct rt_thread, tlist);

            status = -RT_ERROR;
            if (thread->event_info & RT_EVENT_FLAG_AND)
            {
                if ((thread->event_set & event->set) == thread->event_set)
                {
                    /* received an AND event */
                    status = RT_EOK;
                }
            }
            else if (thread->event_info & RT_EVENT_FLAG_OR)
            {
                if (thread->event_set & event->set)
                {
                    /* save the received event set */
                    thread->event_set = thread->event_set & event->set;

                    /* received an OR event */
                    status = RT_EOK;
                }
            }

            /* move node to the next */
            n = n->next;

            /* condition is satisfied, resume thread */
            if (status == RT_EOK)
            {
                /* clear event */
                if (thread->event_info & RT_EVENT_FLAG_CLEAR)
                    event->set &= ~thread->event_set;

                /* resume thread, and thread list breaks out */
                rt_thread_resume(thread);

                /* need do a scheduling */
                need_schedule = RT_TRUE;
            }
        }
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    /* do a schedule */
    if (need_schedule == RT_TRUE)
        rt_schedule();

    return RT_EOK;
}
RTM_EXPORT(rt_event_send);

/**
 * This function will receive an event from event object, if the event is
 * unavailable, the thread shall wait for a specified time.
 *
 * @param event the fast event object
 * @param set the interested event set
 * @param option the receive option, either RT_EVENT_FLAG_AND or
 *        RT_EVENT_FLAG_OR should be set.
 * @param timeout the waiting time
 * @param recved the received event, if you don't care, RT_NULL can be set.
 *
 * @return the error code
 */
rt_err_t rt_event_recv(rt_event_t   event,
                       rt_uint32_t  set,
                       rt_uint8_t   option,
                       rt_int32_t   timeout,
                       rt_uint32_t *recved)
{
    struct rt_thread *thread;
    register rt_ubase_t level;
    register rt_base_t status;

    RT_DEBUG_IN_THREAD_CONTEXT;

    /* parameter check */
    RT_ASSERT(event != RT_NULL);
    RT_ASSERT(rt_object_get_type(&event->parent.parent) == RT_Object_Class_Event);

    if (set == 0)
        return -RT_ERROR;

    /* initialize status */
    status = -RT_ERROR;
    /* get current thread */
    thread = rt_thread_self();
    /* reset thread error */
    thread->error = RT_EOK;

    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(event->parent.parent)));

    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /* check event set */
    if (option & RT_EVENT_FLAG_AND)
    {
        if ((event->set & set) == set)
            status = RT_EOK;
    }
    else if (option & RT_EVENT_FLAG_OR)
    {
        if (event->set & set)
            status = RT_EOK;
    }
    else
    {
        /* either RT_EVENT_FLAG_AND or RT_EVENT_FLAG_OR should be set */
        RT_ASSERT(0);
    }

    if (status == RT_EOK)
    {
        /* set received event */
        if (recved)
            *recved = (event->set & set);

        /* received event */
        if (option & RT_EVENT_FLAG_CLEAR)
            event->set &= ~set;
    }
    else if (timeout == 0)
    {
        /* no waiting */
        thread->error = -RT_ETIMEOUT;
        
        /* enable interrupt */
        rt_hw_interrupt_enable(level);
        
        return -RT_ETIMEOUT;
    }
    else
    {
        /* fill thread event info */
        thread->event_set  = set;
        thread->event_info = option;

        /* put thread to suspended thread list */
        rt_ipc_list_suspend(&(event->parent.suspend_thread),
                            thread,
                            event->parent.parent.flag);

        /* if there is a waiting timeout, active thread timer */
        if (timeout > 0)
        {
            /* reset the timeout of thread timer and start it */
            rt_timer_control(&(thread->thread_timer),
                             RT_TIMER_CTRL_SET_TIME,
                             &timeout);
            rt_timer_start(&(thread->thread_timer));
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        /* do a schedule */
        rt_schedule();

        if (thread->error != RT_EOK)
        {
            /* return error */
            return thread->error;
        }

        /* received an event, disable interrupt to protect */
        level = rt_hw_interrupt_disable();

        /* set received event */
        if (recved)
            *recved = thread->event_set;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(event->parent.parent)));

    return thread->error;
}
RTM_EXPORT(rt_event_recv);

/**
 * This function can get or set some extra attributions of an event object.
 *
 * @param event the event object
 * @param cmd the execution command
 * @param arg the execution argument
 *
 * @return the error code
 */
rt_err_t rt_event_control(rt_event_t event, int cmd, void *arg)
{
    rt_ubase_t level;

    /* parameter check */
    RT_ASSERT(event != RT_NULL);
    RT_ASSERT(rt_object_get_type(&event->parent.parent) == RT_Object_Class_Event);

    if (cmd == RT_IPC_CMD_RESET)
    {
        /* disable interrupt */
        level = rt_hw_interrupt_disable();

        /* resume all waiting thread */
        rt_ipc_list_resume_all(&event->parent.suspend_thread);

        /* initialize event set */
        event->set = 0;

        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        rt_schedule();

        return RT_EOK;
    }

    return -RT_ERROR;
}
RTM_EXPORT(rt_event_control);
#endif /* end of RT_USING_EVENT */

#ifdef RT_USING_MAILBOX
/**
 * This function will initialize a mailbox and put it under control of resource
 * management.
 *
 * @param mb the mailbox object
 * @param name the name of mailbox
 * @param msgpool the begin address of buffer to save received mail
 * @param size the size of mailbox
 * @param flag the flag of mailbox
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_mb_init(rt_mailbox_t mb,
                    const char  *name,
                    void        *msgpool,
                    rt_size_t    size,
                    rt_uint8_t   flag)
{
    RT_ASSERT(mb != RT_NULL);

    /* initialize object */
    rt_object_init(&(mb->parent.parent), RT_Object_Class_MailBox, name);

    /* set parent flag */
    mb->parent.parent.flag = flag;

    /* initialize ipc object */
    rt_ipc_object_init(&(mb->parent));

    /* initialize mailbox */
    mb->msg_pool   = (rt_ubase_t *)msgpool;
    mb->size       = size;
    mb->entry      = 0;
    mb->in_offset  = 0;
    mb->out_offset = 0;

    /* initialize an additional list of sender suspend thread */
    rt_list_init(&(mb->suspend_sender_thread));

    return RT_EOK;
}
RTM_EXPORT(rt_mb_init);

/**
 * This function will detach a mailbox from resource management
 *
 * @param mb the mailbox object
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_mb_detach(rt_mailbox_t mb)
{
    /* parameter check */
    RT_ASSERT(mb != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mb->parent.parent) == RT_Object_Class_MailBox);
    RT_ASSERT(rt_object_is_systemobject(&mb->parent.parent));

    /* resume all suspended thread */
    rt_ipc_list_resume_all(&(mb->parent.suspend_thread));
    /* also resume all mailbox private suspended thread */
    rt_ipc_list_resume_all(&(mb->suspend_sender_thread));

    /* detach mailbox object */
    rt_object_detach(&(mb->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_mb_detach);

#ifdef RT_USING_HEAP
/**
 * This function will create a mailbox object from system resource
 *
 * @param name the name of mailbox
 * @param size the size of mailbox
 * @param flag the flag of mailbox
 *
 * @return the created mailbox, RT_NULL on error happen
 */
rt_mailbox_t rt_mb_create(const char *name, rt_size_t size, rt_uint8_t flag)
{
    rt_mailbox_t mb;

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* allocate object */
    mb = (rt_mailbox_t)rt_object_allocate(RT_Object_Class_MailBox, name);
    if (mb == RT_NULL)
        return mb;

    /* set parent */
    mb->parent.parent.flag = flag;

    /* initialize ipc object */
    rt_ipc_object_init(&(mb->parent));

    /* initialize mailbox */
    mb->size     = size;
    mb->msg_pool = (rt_ubase_t *)RT_KERNEL_MALLOC(mb->size * sizeof(rt_ubase_t));
    if (mb->msg_pool == RT_NULL)
    {
        /* delete mailbox object */
        rt_object_delete(&(mb->parent.parent));

        return RT_NULL;
    }
    mb->entry      = 0;
    mb->in_offset  = 0;
    mb->out_offset = 0;

    /* initialize an additional list of sender suspend thread */
    rt_list_init(&(mb->suspend_sender_thread));

    return mb;
}
RTM_EXPORT(rt_mb_create);

/**
 * This function will delete a mailbox object and release the memory
 *
 * @param mb the mailbox object
 *
 * @return the error code
 */
rt_err_t rt_mb_delete(rt_mailbox_t mb)
{
    RT_DEBUG_NOT_IN_INTERRUPT;

    /* parameter check */
    RT_ASSERT(mb != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mb->parent.parent) == RT_Object_Class_MailBox);
    RT_ASSERT(rt_object_is_systemobject(&mb->parent.parent) == RT_FALSE);

    /* resume all suspended thread */
    rt_ipc_list_resume_all(&(mb->parent.suspend_thread));

    /* also resume all mailbox private suspended thread */
    rt_ipc_list_resume_all(&(mb->suspend_sender_thread));

    /* free mailbox pool */
    RT_KERNEL_FREE(mb->msg_pool);

    /* delete mailbox object */
    rt_object_delete(&(mb->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_mb_delete);
#endif

/**
 * This function will send a mail to mailbox object. If the mailbox is full,
 * current thread will be suspended until timeout.
 *
 * @param mb the mailbox object
 * @param value the mail
 * @param timeout the waiting time
 *
 * @return the error code
 */
rt_err_t rt_mb_send_wait(rt_mailbox_t mb,
                         rt_ubase_t   value,
                         rt_int32_t   timeout)
{
    struct rt_thread *thread;
    register rt_ubase_t temp;
    rt_uint32_t tick_delta;

    /* parameter check */
    RT_ASSERT(mb != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mb->parent.parent) == RT_Object_Class_MailBox);

    /* initialize delta tick */
    tick_delta = 0;
    /* get current thread */
    thread = rt_thread_self();

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(mb->parent.parent)));

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* for non-blocking call */
    if (mb->entry == mb->size && timeout == 0)
    {
        rt_hw_interrupt_enable(temp);

        return -RT_EFULL;
    }

    /* mailbox is full */
    while (mb->entry == mb->size)
    {
        /* reset error number in thread */
        thread->error = RT_EOK;

        /* no waiting, return timeout */
        if (timeout == 0)
        {
            /* enable interrupt */
            rt_hw_interrupt_enable(temp);

            return -RT_EFULL;
        }

        RT_DEBUG_IN_THREAD_CONTEXT;
        /* suspend current thread */
        rt_ipc_list_suspend(&(mb->suspend_sender_thread),
                            thread,
                            mb->parent.parent.flag);

        /* has waiting time, start thread timer */
        if (timeout > 0)
        {
            /* get the start tick of timer */
            tick_delta = rt_tick_get();

            RT_DEBUG_LOG(RT_DEBUG_IPC, ("mb_send_wait: start timer of thread:%s\n",
                                        thread->name));

            /* reset the timeout of thread timer and start it */
            rt_timer_control(&(thread->thread_timer),
                             RT_TIMER_CTRL_SET_TIME,
                             &timeout);
            rt_timer_start(&(thread->thread_timer));
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        /* re-schedule */
        rt_schedule();

        /* resume from suspend state */
        if (thread->error != RT_EOK)
        {
            /* return error */
            return thread->error;
        }

        /* disable interrupt */
        temp = rt_hw_interrupt_disable();

        /* if it's not waiting forever and then re-calculate timeout tick */
        if (timeout > 0)
        {
            tick_delta = rt_tick_get() - tick_delta;
            timeout -= tick_delta;
            if (timeout < 0)
                timeout = 0;
        }
    }

    /* set ptr */
    mb->msg_pool[mb->in_offset] = value;
    /* increase input offset */
    ++ mb->in_offset;
    if (mb->in_offset >= mb->size)
        mb->in_offset = 0;
    /* increase message entry */
    mb->entry ++;

    /* resume suspended thread */
    if (!rt_list_isempty(&mb->parent.suspend_thread))
    {
        rt_ipc_list_resume(&(mb->parent.suspend_thread));

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        rt_schedule();

        return RT_EOK;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    return RT_EOK;
}
RTM_EXPORT(rt_mb_send_wait);

/**
 * This function will send a mail to mailbox object, if there are threads
 * suspended on mailbox object, it will be waked up. This function will return
 * immediately, if you want blocking send, use rt_mb_send_wait instead.
 *
 * @param mb the mailbox object
 * @param value the mail
 *
 * @return the error code
 */
rt_err_t rt_mb_send(rt_mailbox_t mb, rt_ubase_t value)
{
    return rt_mb_send_wait(mb, value, 0);
}
RTM_EXPORT(rt_mb_send);

/**
 * This function will receive a mail from mailbox object, if there is no mail
 * in mailbox object, the thread shall wait for a specified time.
 *
 * @param mb the mailbox object
 * @param value the received mail will be saved in
 * @param timeout the waiting time
 *
 * @return the error code
 */
rt_err_t rt_mb_recv(rt_mailbox_t mb, rt_ubase_t *value, rt_int32_t timeout)
{
    struct rt_thread *thread;
    register rt_ubase_t temp;
    rt_uint32_t tick_delta;

    /* parameter check */
    RT_ASSERT(mb != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mb->parent.parent) == RT_Object_Class_MailBox);

    /* initialize delta tick */
    tick_delta = 0;
    /* get current thread */
    thread = rt_thread_self();

    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(mb->parent.parent)));

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* for non-blocking call */
    if (mb->entry == 0 && timeout == 0)
    {
        rt_hw_interrupt_enable(temp);

        return -RT_ETIMEOUT;
    }

    /* mailbox is empty */
    while (mb->entry == 0)
    {
        /* reset error number in thread */
        thread->error = RT_EOK;

        /* no waiting, return timeout */
        if (timeout == 0)
        {
            /* enable interrupt */
            rt_hw_interrupt_enable(temp);

            thread->error = -RT_ETIMEOUT;

            return -RT_ETIMEOUT;
        }

        RT_DEBUG_IN_THREAD_CONTEXT;
        /* suspend current thread */
        rt_ipc_list_suspend(&(mb->parent.suspend_thread),
                            thread,
                            mb->parent.parent.flag);

        /* has waiting time, start thread timer */
        if (timeout > 0)
        {
            /* get the start tick of timer */
            tick_delta = rt_tick_get();

            RT_DEBUG_LOG(RT_DEBUG_IPC, ("mb_recv: start timer of thread:%s\n",
                                        thread->name));

            /* reset the timeout of thread timer and start it */
            rt_timer_control(&(thread->thread_timer),
                             RT_TIMER_CTRL_SET_TIME,
                             &timeout);
            rt_timer_start(&(thread->thread_timer));
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        /* re-schedule */
        rt_schedule();

        /* resume from suspend state */
        if (thread->error != RT_EOK)
        {
            /* return error */
            return thread->error;
        }

        /* disable interrupt */
        temp = rt_hw_interrupt_disable();

        /* if it's not waiting forever and then re-calculate timeout tick */
        if (timeout > 0)
        {
            tick_delta = rt_tick_get() - tick_delta;
            timeout -= tick_delta;
            if (timeout < 0)
                timeout = 0;
        }
    }

    /* fill ptr */
    *value = mb->msg_pool[mb->out_offset];

    /* increase output offset */
    ++ mb->out_offset;
    if (mb->out_offset >= mb->size)
        mb->out_offset = 0;
    /* decrease message entry */
    mb->entry --;

    /* resume suspended thread */
    if (!rt_list_isempty(&(mb->suspend_sender_thread)))
    {
        rt_ipc_list_resume(&(mb->suspend_sender_thread));

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(mb->parent.parent)));

        rt_schedule();

        return RT_EOK;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(mb->parent.parent)));

    return RT_EOK;
}
RTM_EXPORT(rt_mb_recv);

/**
 * This function can get or set some extra attributions of a mailbox object.
 *
 * @param mb the mailbox object
 * @param cmd the execution command
 * @param arg the execution argument
 *
 * @return the error code
 */
rt_err_t rt_mb_control(rt_mailbox_t mb, int cmd, void *arg)
{
    rt_ubase_t level;

    /* parameter check */
    RT_ASSERT(mb != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mb->parent.parent) == RT_Object_Class_MailBox);

    if (cmd == RT_IPC_CMD_RESET)
    {
        /* disable interrupt */
        level = rt_hw_interrupt_disable();

        /* resume all waiting thread */
        rt_ipc_list_resume_all(&(mb->parent.suspend_thread));
        /* also resume all mailbox private suspended thread */
        rt_ipc_list_resume_all(&(mb->suspend_sender_thread));

        /* re-init mailbox */
        mb->entry      = 0;
        mb->in_offset  = 0;
        mb->out_offset = 0;

        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        rt_schedule();

        return RT_EOK;
    }

    return -RT_ERROR;
}
RTM_EXPORT(rt_mb_control);
#endif /* end of RT_USING_MAILBOX */

#ifdef RT_USING_MESSAGEQUEUE
struct rt_mq_message
{
    struct rt_mq_message *next;
};

/**
 * This function will initialize a message queue and put it under control of
 * resource management.
 *
 * @param mq the message object
 * @param name the name of message queue
 * @param msgpool the beginning address of buffer to save messages
 * @param msg_size the maximum size of message
 * @param pool_size the size of buffer to save messages
 * @param flag the flag of message queue
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_mq_init(rt_mq_t     mq,
                    const char *name,
                    void       *msgpool,
                    rt_size_t   msg_size,
                    rt_size_t   pool_size,
                    rt_uint8_t  flag)
{
    struct rt_mq_message *head;
    register rt_base_t temp;

    /* parameter check */
    RT_ASSERT(mq != RT_NULL);

    /* initialize object */
    rt_object_init(&(mq->parent.parent), RT_Object_Class_MessageQueue, name);

    /* set parent flag */
    mq->parent.parent.flag = flag;

    /* initialize ipc object */
    rt_ipc_object_init(&(mq->parent));

    /* set message pool */
    mq->msg_pool = msgpool;

    /* get correct message size */
    mq->msg_size = RT_ALIGN(msg_size, RT_ALIGN_SIZE);
    mq->max_msgs = pool_size / (mq->msg_size + sizeof(struct rt_mq_message));

    /* initialize message list */
    mq->msg_queue_head = RT_NULL;
    mq->msg_queue_tail = RT_NULL;

    /* initialize message empty list */
    mq->msg_queue_free = RT_NULL;
    for (temp = 0; temp < mq->max_msgs; temp ++)
    {
        head = (struct rt_mq_message *)((rt_uint8_t *)mq->msg_pool +
                                        temp * (mq->msg_size + sizeof(struct rt_mq_message)));
        head->next = (struct rt_mq_message *)mq->msg_queue_free;
        mq->msg_queue_free = head;
    }

    /* the initial entry is zero */
    mq->entry = 0;

    /* initialize an additional list of sender suspend thread */
    rt_list_init(&(mq->suspend_sender_thread));

    return RT_EOK;
}
RTM_EXPORT(rt_mq_init);

/**
 * This function will detach a message queue object from resource management
 *
 * @param mq the message queue object
 *
 * @return the operation status, RT_EOK on successful
 */
rt_err_t rt_mq_detach(rt_mq_t mq)
{
    /* parameter check */
    RT_ASSERT(mq != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mq->parent.parent) == RT_Object_Class_MessageQueue);
    RT_ASSERT(rt_object_is_systemobject(&mq->parent.parent));

    /* resume all suspended thread */
    rt_ipc_list_resume_all(&mq->parent.suspend_thread);
    /* also resume all message queue private suspended thread */
    rt_ipc_list_resume_all(&(mq->suspend_sender_thread));

    /* detach message queue object */
    rt_object_detach(&(mq->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_mq_detach);

#ifdef RT_USING_HEAP
/**
 * This function will create a message queue object from system resource
 *
 * @param name the name of message queue
 * @param msg_size the size of message
 * @param max_msgs the maximum number of message in queue
 * @param flag the flag of message queue
 *
 * @return the created message queue, RT_NULL on error happen
 */
rt_mq_t rt_mq_create(const char *name,
                     rt_size_t   msg_size,
                     rt_size_t   max_msgs,
                     rt_uint8_t  flag)
{
    struct rt_messagequeue *mq;
    struct rt_mq_message *head;
    register rt_base_t temp;

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* allocate object */
    mq = (rt_mq_t)rt_object_allocate(RT_Object_Class_MessageQueue, name);
    if (mq == RT_NULL)
        return mq;

    /* set parent */
    mq->parent.parent.flag = flag;

    /* initialize ipc object */
    rt_ipc_object_init(&(mq->parent));

    /* initialize message queue */

    /* get correct message size */
    mq->msg_size = RT_ALIGN(msg_size, RT_ALIGN_SIZE);
    mq->max_msgs = max_msgs;

    /* allocate message pool */
    mq->msg_pool = RT_KERNEL_MALLOC((mq->msg_size + sizeof(struct rt_mq_message)) * mq->max_msgs);
    if (mq->msg_pool == RT_NULL)
    {
        rt_object_delete(&(mq->parent.parent));

        return RT_NULL;
    }

    /* initialize message list */
    mq->msg_queue_head = RT_NULL;
    mq->msg_queue_tail = RT_NULL;

    /* initialize message empty list */
    mq->msg_queue_free = RT_NULL;
    for (temp = 0; temp < mq->max_msgs; temp ++)
    {
        head = (struct rt_mq_message *)((rt_uint8_t *)mq->msg_pool +
                                        temp * (mq->msg_size + sizeof(struct rt_mq_message)));
        head->next = (struct rt_mq_message *)mq->msg_queue_free;
        mq->msg_queue_free = head;
    }

    /* the initial entry is zero */
    mq->entry = 0;

    /* initialize an additional list of sender suspend thread */
    rt_list_init(&(mq->suspend_sender_thread));

    return mq;
}
RTM_EXPORT(rt_mq_create);

/**
 * This function will delete a message queue object and release the memory
 *
 * @param mq the message queue object
 *
 * @return the error code
 */
rt_err_t rt_mq_delete(rt_mq_t mq)
{
    RT_DEBUG_NOT_IN_INTERRUPT;

    /* parameter check */
    RT_ASSERT(mq != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mq->parent.parent) == RT_Object_Class_MessageQueue);
    RT_ASSERT(rt_object_is_systemobject(&mq->parent.parent) == RT_FALSE);

    /* resume all suspended thread */
    rt_ipc_list_resume_all(&(mq->parent.suspend_thread));
    /* also resume all message queue private suspended thread */
    rt_ipc_list_resume_all(&(mq->suspend_sender_thread));

    /* free message queue pool */
    RT_KERNEL_FREE(mq->msg_pool);

    /* delete message queue object */
    rt_object_delete(&(mq->parent.parent));

    return RT_EOK;
}
RTM_EXPORT(rt_mq_delete);
#endif

/**
 * This function will send a message to message queue object. If the message queue is full,
 * current thread will be suspended until timeout.
 *
 * @param mq the message queue object
 * @param buffer the message
 * @param size the size of buffer
 * @param timeout the waiting time
 *
 * @return the error code
 */
rt_err_t rt_mq_send_wait(rt_mq_t     mq,
                         const void *buffer,
                         rt_size_t   size,
                         rt_int32_t  timeout)
{
    register rt_ubase_t temp;
    struct rt_mq_message *msg;
    rt_uint32_t tick_delta;
    struct rt_thread *thread;

    /* parameter check */
    RT_ASSERT(mq != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mq->parent.parent) == RT_Object_Class_MessageQueue);
    RT_ASSERT(buffer != RT_NULL);
    RT_ASSERT(size != 0);

    /* greater than one message size */
    if (size > mq->msg_size)
        return -RT_ERROR;

    /* initialize delta tick */
    tick_delta = 0;
    /* get current thread */
    thread = rt_thread_self();

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(mq->parent.parent)));

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* get a free list, there must be an empty item */
    msg = (struct rt_mq_message *)mq->msg_queue_free;
    /* for non-blocking call */
    if (msg == RT_NULL && timeout == 0)
    {
        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        return -RT_EFULL;
    }

    /* message queue is full */
    while ((msg = mq->msg_queue_free) == RT_NULL)
    {
        /* reset error number in thread */
        thread->error = RT_EOK;

        /* no waiting, return timeout */
        if (timeout == 0)
        {
            /* enable interrupt */
            rt_hw_interrupt_enable(temp);

            return -RT_EFULL;
        }

        RT_DEBUG_IN_THREAD_CONTEXT;
        /* suspend current thread */
        rt_ipc_list_suspend(&(mq->suspend_sender_thread),
                            thread,
                            mq->parent.parent.flag);

        /* has waiting time, start thread timer */
        if (timeout > 0)
        {
            /* get the start tick of timer */
            tick_delta = rt_tick_get();

            RT_DEBUG_LOG(RT_DEBUG_IPC, ("mq_send_wait: start timer of thread:%s\n",
                                        thread->name));

            /* reset the timeout of thread timer and start it */
            rt_timer_control(&(thread->thread_timer),
                             RT_TIMER_CTRL_SET_TIME,
                             &timeout);
            rt_timer_start(&(thread->thread_timer));
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        /* re-schedule */
        rt_schedule();

        /* resume from suspend state */
        if (thread->error != RT_EOK)
        {
            /* return error */
            return thread->error;
        }

        /* disable interrupt */
        temp = rt_hw_interrupt_disable();

        /* if it's not waiting forever and then re-calculate timeout tick */
        if (timeout > 0)
        {
            tick_delta = rt_tick_get() - tick_delta;
            timeout -= tick_delta;
            if (timeout < 0)
                timeout = 0;
        }
    }

    /* move free list pointer */
    mq->msg_queue_free = msg->next;

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    /* the msg is the new tailer of list, the next shall be NULL */
    msg->next = RT_NULL;
    /* copy buffer */
    rt_memcpy(msg + 1, buffer, size);

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();
    /* link msg to message queue */
    if (mq->msg_queue_tail != RT_NULL)
    {
        /* if the tail exists, */
        ((struct rt_mq_message *)mq->msg_queue_tail)->next = msg;
    }

    /* set new tail */
    mq->msg_queue_tail = msg;
    /* if the head is empty, set head */
    if (mq->msg_queue_head == RT_NULL)
        mq->msg_queue_head = msg;

    /* increase message entry */
    mq->entry ++;

    /* resume suspended thread */
    if (!rt_list_isempty(&mq->parent.suspend_thread))
    {
        rt_ipc_list_resume(&(mq->parent.suspend_thread));

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        rt_schedule();

        return RT_EOK;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    return RT_EOK;
}
RTM_EXPORT(rt_mq_send_wait)

/**
 * This function will send a message to message queue object, if there are
 * threads suspended on message queue object, it will be waked up.
 *
 * @param mq the message queue object
 * @param buffer the message
 * @param size the size of buffer
 *
 * @return the error code
 */
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size)
{
    return rt_mq_send_wait(mq, buffer, size, 0);
}
RTM_EXPORT(rt_mq_send);

/**
 * This function will send an urgent message to message queue object, which
 * means the message will be inserted to the head of message queue. If there
 * are threads suspended on message queue object, it will be waked up.
 *
 * @param mq the message queue object
 * @param buffer the message
 * @param size the size of buffer
 *
 * @return the error code
 */
rt_err_t rt_mq_urgent(rt_mq_t mq, const void *buffer, rt_size_t size)
{
    register rt_ubase_t temp;
    struct rt_mq_message *msg;

    /* parameter check */
    RT_ASSERT(mq != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mq->parent.parent) == RT_Object_Class_MessageQueue);
    RT_ASSERT(buffer != RT_NULL);
    RT_ASSERT(size != 0);

    /* greater than one message size */
    if (size > mq->msg_size)
        return -RT_ERROR;

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(mq->parent.parent)));

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* get a free list, there must be an empty item */
    msg = (struct rt_mq_message *)mq->msg_queue_free;
    /* message queue is full */
    if (msg == RT_NULL)
    {
        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        return -RT_EFULL;
    }
    /* move free list pointer */
    mq->msg_queue_free = msg->next;

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    /* copy buffer */
    rt_memcpy(msg + 1, buffer, size);

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* link msg to the beginning of message queue */
    msg->next = (struct rt_mq_message *)mq->msg_queue_head;
    mq->msg_queue_head = msg;

    /* if there is no tail */
    if (mq->msg_queue_tail == RT_NULL)
        mq->msg_queue_tail = msg;

    /* increase message entry */
    mq->entry ++;

    /* resume suspended thread */
    if (!rt_list_isempty(&mq->parent.suspend_thread))
    {
        rt_ipc_list_resume(&(mq->parent.suspend_thread));

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        rt_schedule();

        return RT_EOK;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    return RT_EOK;
}
RTM_EXPORT(rt_mq_urgent);

/**
 * This function will receive a message from message queue object, if there is
 * no message in message queue object, the thread shall wait for a specified
 * time.
 *
 * @param mq the message queue object
 * @param buffer the received message will be saved in
 * @param size the size of buffer
 * @param timeout the waiting time
 *
 * @return the error code
 */
rt_err_t rt_mq_recv(rt_mq_t    mq,
                    void      *buffer,
                    rt_size_t  size,
                    rt_int32_t timeout)
{
    struct rt_thread *thread;
    register rt_ubase_t temp;
    struct rt_mq_message *msg;
    rt_uint32_t tick_delta;

    /* parameter check */
    RT_ASSERT(mq != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mq->parent.parent) == RT_Object_Class_MessageQueue);
    RT_ASSERT(buffer != RT_NULL);
    RT_ASSERT(size != 0);

    /* initialize delta tick */
    tick_delta = 0;
    /* get current thread */
    thread = rt_thread_self();
    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(mq->parent.parent)));

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();

    /* for non-blocking call */
    if (mq->entry == 0 && timeout == 0)
    {
        rt_hw_interrupt_enable(temp);

        return -RT_ETIMEOUT;
    }

    /* message queue is empty */
    while (mq->entry == 0)
    {
        RT_DEBUG_IN_THREAD_CONTEXT;

        /* reset error number in thread */
        thread->error = RT_EOK;

        /* no waiting, return timeout */
        if (timeout == 0)
        {
            /* enable interrupt */
            rt_hw_interrupt_enable(temp);

            thread->error = -RT_ETIMEOUT;

            return -RT_ETIMEOUT;
        }

        /* suspend current thread */
        rt_ipc_list_suspend(&(mq->parent.suspend_thread),
                            thread,
                            mq->parent.parent.flag);

        /* has waiting time, start thread timer */
        if (timeout > 0)
        {
            /* get the start tick of timer */
            tick_delta = rt_tick_get();

            RT_DEBUG_LOG(RT_DEBUG_IPC, ("set thread:%s to timer list\n",
                                        thread->name));

            /* reset the timeout of thread timer and start it */
            rt_timer_control(&(thread->thread_timer),
                             RT_TIMER_CTRL_SET_TIME,
                             &timeout);
            rt_timer_start(&(thread->thread_timer));
        }

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        /* re-schedule */
        rt_schedule();

        /* recv message */
        if (thread->error != RT_EOK)
        {
            /* return error */
            return thread->error;
        }

        /* disable interrupt */
        temp = rt_hw_interrupt_disable();

        /* if it's not waiting forever and then re-calculate timeout tick */
        if (timeout > 0)
        {
            tick_delta = rt_tick_get() - tick_delta;
            timeout -= tick_delta;
            if (timeout < 0)
                timeout = 0;
        }
    }

    /* get message from queue */
    msg = (struct rt_mq_message *)mq->msg_queue_head;

    /* move message queue head */
    mq->msg_queue_head = msg->next;
    /* reach queue tail, set to NULL */
    if (mq->msg_queue_tail == msg)
        mq->msg_queue_tail = RT_NULL;

    /* decrease message entry */
    mq->entry --;

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    /* copy message */
    rt_memcpy(buffer, msg + 1, size > mq->msg_size ? mq->msg_size : size);

    /* disable interrupt */
    temp = rt_hw_interrupt_disable();
    /* put message to free list */
    msg->next = (struct rt_mq_message *)mq->msg_queue_free;
    mq->msg_queue_free = msg;

    /* resume suspended thread */
    if (!rt_list_isempty(&(mq->suspend_sender_thread)))
    {
        rt_ipc_list_resume(&(mq->suspend_sender_thread));

        /* enable interrupt */
        rt_hw_interrupt_enable(temp);

        RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(mq->parent.parent)));

        rt_schedule();

        return RT_EOK;
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(temp);

    RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(mq->parent.parent)));

    return RT_EOK;
}
RTM_EXPORT(rt_mq_recv);

/**
 * This function can get or set some extra attributions of a message queue
 * object.
 *
 * @param mq the message queue object
 * @param cmd the execution command
 * @param arg the execution argument
 *
 * @return the error code
 */
rt_err_t rt_mq_control(rt_mq_t mq, int cmd, void *arg)
{
    rt_ubase_t level;
    struct rt_mq_message *msg;

    /* parameter check */
    RT_ASSERT(mq != RT_NULL);
    RT_ASSERT(rt_object_get_type(&mq->parent.parent) == RT_Object_Class_MessageQueue);

    if (cmd == RT_IPC_CMD_RESET)
    {
        /* disable interrupt */
        level = rt_hw_interrupt_disable();

        /* resume all waiting thread */
        rt_ipc_list_resume_all(&mq->parent.suspend_thread);
        /* also resume all message queue private suspended thread */
        rt_ipc_list_resume_all(&(mq->suspend_sender_thread));

        /* release all message in the queue */
        while (mq->msg_queue_head != RT_NULL)
        {
            /* get message from queue */
            msg = (struct rt_mq_message *)mq->msg_queue_head;

            /* move message queue head */
            mq->msg_queue_head = msg->next;
            /* reach queue tail, set to NULL */
            if (mq->msg_queue_tail == msg)
                mq->msg_queue_tail = RT_NULL;

            /* put message to free list */
            msg->next = (struct rt_mq_message *)mq->msg_queue_free;
            mq->msg_queue_free = msg;
        }

        /* clean entry */
        mq->entry = 0;

        /* enable interrupt */
        rt_hw_interrupt_enable(level);

        rt_schedule();

        return RT_EOK;
    }

    return -RT_ERROR;
}
RTM_EXPORT(rt_mq_control);
#endif /* end of RT_USING_MESSAGEQUEUE */


#include "dfs.h"
#include "dfs_file.h"

static const struct dfs_file_ops dfs_channel_file_ops =
{
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
    RT_NULL,
};

#define IPC_MSG_SIZE 100
struct bin_ipc_msg ipc_msg[IPC_MSG_SIZE];
int ipc_msg_pos = 0;//当前空闲的pos

/**
 * IPC MSG全局初始化
 */
rt_err_t bin_ipc_msg_init()
{
    return -RT_ERROR;
}

bin_ipc_msg_t ipc_msg_alloc()
{
    bin_ipc_msg_t msg = RT_NULL;

    if(ipc_msg_pos < IPC_MSG_SIZE)
    {
        //msg = (bin_ipc_msg_t)rt_malloc(sizeof(struct bin_ipc_msg));
        msg = ipc_msg + ipc_msg_pos;
        ipc_msg_pos++;
        //初始化
    }else{
        rt_kprintf("IPC MSG FULL\n");
    }

    return msg;
}

rt_err_t ipc_msg_free(bin_ipc_msg_t msg)
{
    return -RT_ERROR;
}

void ipc_msg_init(bin_ipc_msg_t msg, bin_channel_msg_t data, rt_uint8_t need_reply)
{
    //TODO:完善本结构
    msg->msg.type = data->type;

    msg->msg.sender = rt_thread_self();//?

    memcpy(&msg->msg.u, &data->u, sizeof(data->u));

    //链表初始化
    rt_list_init(&msg->mlist);
}

/**
 * This function will create a mutex from system resource
 *
 * @param name the name of mutex
 * @param flag the flag of mutex
 *
 * @return the created mutex, RT_NULL on error happen
 *
 * @see rt_mutex_init
 */
int bin_channel_open(const char *name, rt_uint8_t flag)
{
    bin_channel_t channel = RT_NULL;
    rt_object_t obj;
    int fd;
    struct dfs_fd* d;

    RT_DEBUG_NOT_IN_INTERRUPT;

    //查找是否已经申明过,在链表中查找是否有这个name的channel
    struct rt_object_information *information;
    struct rt_object *object;
    struct rt_list_node *node;

    /* try to find device object */
    information = rt_object_get_information(RT_Object_Class_Channel);
    RT_ASSERT(information != RT_NULL);
    for (node  = information->object_list.next;
         node != &(information->object_list);
         node  = node->next)
    {
        object = rt_list_entry(node, struct rt_object, list);
        if (rt_strncmp(object->name, name, RT_NAME_MAX) == 0)
        {
            channel = (bin_channel_t)object;
            break;
        }
    }

    if(channel != RT_NULL)
    {
        //找到老的channel的 处理逻辑

        rt_kprintf("CH %s has been found\r\n", channel->parent.parent.name);

        struct dfs_fd *d;
        struct dfs_fdtable *fdt;

        fdt = dfs_fdtable_get();

        for(fd = 0; fd < fdt->maxfd; ++fd)
        {
            d = fdt->fds[fd];
            if( channel == (bin_channel_t) d->data)
                break;
        }

        if(fd != fdt->maxfd)
        {
            //找到了这个fd
            fd = fd + DFS_FD_OFFSET;
            d->ref_count++;
            return fd;
        }
    }

    fd = 0;


    /* allocate object */
    channel = (bin_channel_t)rt_object_allocate(RT_Object_Class_Channel, name);//还需要在函数中添加相应的Channel大小等初始化
    if (channel == RT_NULL)
        return -RT_ERROR;
    
    fd  = fd_new();//ref_count=1
    if(fd >= 0)
    {

        /* initialize ipc object */
        rt_ipc_object_init(&(channel->parent));

        channel->value              = 1;
        channel->original_priority  = 0xFF;
        channel->hold               = 0;

        channel->wait_thread.prev   = &channel->wait_thread;
        channel->wait_thread.next   = &channel->wait_thread;

        //消息队列
        channel->wait_msg.prev      = &channel->wait_msg;
        channel->wait_msg.next      = &channel->wait_msg;

        rt_list_init(&channel->parent.suspend_thread);
        rt_list_init(&channel->reader_queue.waiting_list);

        //如果是初始化,那么ref = 1;如果不是,则为++
        channel->ref                = 1;

        /* set flag */
        channel->parent.parent.flag = flag;

        /* 放入fds操作集 */
        d = fd_get(fd);//ref_count++, =2
        d->type = FT_USER;
        d->path = NULL;
        d->flags = O_RDWR; /* set flags as read and write */
        d->size = 0;
        d->pos  = 0;
        d->data = (void *)channel;
        d->fops = &dfs_channel_file_ops;
        /* release the ref-count of fd */
        fd_put(d);//ref_count=1

    }






    return fd;
}

rt_err_t bin_channel_close(int fd)
{
    bin_channel_t channel;
    struct dfs_fd* d;

    RT_DEBUG_NOT_IN_INTERRUPT;
    d = fd_get(fd);
    //清除一次
    fd_put(d);

    if(d->ref_count == 1)
    {
        channel = (bin_channel_t)d->data;

        /* parameter check */
        RT_ASSERT(channel != RT_NULL);
        RT_ASSERT(rt_object_get_type(&channel->parent.parent) == RT_Object_Class_Channel);
        RT_ASSERT(rt_object_is_systemobject(&channel->parent.parent) == RT_FALSE);

        /* wakeup all suspended threads */
        rt_ipc_list_resume_all(&(channel->parent.suspend_thread));

        /* delete channel object ,using kernel_free delete channel object */
        rt_object_delete(&(channel->parent.parent));


    }

    fd_put(d);
    return RT_EOK;
}


rt_err_t bin_channel_recv(int fd, rt_int32_t timeout, bin_channel_msg_t msg)
{
    register rt_ubase_t level;
    struct rt_thread *thread;

    bin_channel_t channel;
    struct dfs_fd* d;

    /* get current thread */
    thread = rt_thread_self();

    d = fd_get(fd);
    channel = (bin_channel_t)d->data;
    fd_put(d);

    //阻塞接收
    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(channel->parent.parent)));


    /* disable interrupt */
    level = rt_hw_interrupt_disable();


    //check status



    //suspend
    /* put thread to suspended thread list */
    rt_ipc_list_suspend(&(channel->parent.suspend_thread),
                        thread,
                        channel->parent.parent.flag);
    //如果有timeout则打开定时器
    if(timeout > 0)
    {
        /* reset the timeout of thread timer and start it */
        rt_timer_control(&(thread->thread_timer),
                            RT_TIMER_CTRL_SET_TIME,
                            &timeout);
        rt_timer_start(&(thread->thread_timer));
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    /* do a schedule */
    rt_schedule();

    //程序会在这里断开..恢复也是恢复到这里!?

    rt_kprintf("%s resume\n", thread->name);

    if (thread->error != RT_EOK)
    {
        /* return error */
        return thread->error;
    }

    /* received an event, disable interrupt to protect */
    level = rt_hw_interrupt_disable();

    /* 从 thread->msg_ret 中接受消息 */
    if(thread->msg_ret != RT_NULL)
    {
        memcpy(msg, &((bin_ipc_msg_t)thread->msg_ret)->msg, sizeof(*msg));
        rt_kprintf("%s -> %s received %x\n", ipc_msg->msg.sender, thread->name, ipc_msg->msg.u.d);

        //已经取出消息, 清空收件箱
        if(channel->reply == RT_NULL)
        {
            thread->msg_ret = RT_NULL;
        }else{
            //需要回复,暂时不清除
        }
        

        //已经取出消息, 那么待阅读队列里删除掉这个thread
        //rt_list_remove(&thread->tlist);

        //如果需要回复,那么加入到回复队列中//?如果都用tlist做表示,那么怎么确定当前的状态在哪呢?所以需要个东西标记下状态吧?
        //rt_list_insert_after(&channel->wait_thread, &thread->tlist);

        //如果队列空, 那么改变待阅读队列的状态
        if(channel->reader_queue.waiting_list.next == &channel->reader_queue.waiting_list)
            channel->reader_queue.flag = 0;
    }else{
        rt_kprintf("%s MSG_RET empty!\n", thread->name);
    }

    /* 需要回复的消息,使用别的函数回复 */
    if(channel->reply != RT_NULL)
    {
        //
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    RT_OBJECT_HOOK_CALL(rt_object_take_hook, (&(channel->parent.parent)));


    return RT_EOK;
}

/**
 * @param msg 临时变量,需要添加到信息队列里面
 * TODO:里面有流程导致系统错误
 * bus fault:
 * SCB_CFSR_BFSR:0x82 PRECISERR SCB->BFAR:DEADBEF0
 */
rt_err_t bin_channel_send(int fd, struct bin_channel_msg* msg, int need_reply)
{
    struct rt_thread *thread;
    register rt_ubase_t level;
    rt_bool_t need_schedule;
    register rt_base_t status;

    bin_channel_t channel;
    struct dfs_fd* d;

    bin_ipc_msg_t ipc_msg = RT_NULL;

    RT_DEBUG_NOT_IN_INTERRUPT;

    level = rt_hw_interrupt_disable();

    ipc_msg = ipc_msg_alloc();
    if(ipc_msg == RT_NULL)
    {
        rt_hw_interrupt_enable(level);
        return -RT_ENOMEM;
    }
    
    //msg->sender = rt_thread_self();

    d = fd_get(fd);
    channel = (bin_channel_t)d->data;
    fd_put(d);//release the ref-count of fd

    //INIT
    ipc_msg_init(ipc_msg, msg, need_reply);


    if(need_reply)
        channel->reply = rt_thread_self();

    //msg挂载在等待列表

    

    //rt_list_insert_after(&channel->wait_msg, );

    //如果需要回复,那么阻塞等待
    
    /* disable interrupt */
    //level = rt_hw_interrupt_disable();

    RT_OBJECT_HOOK_CALL(rt_object_put_hook, (&(channel->parent.parent)));


    /* map suspend thread in this channel, send msg */
    struct rt_list_node *n;

    if (!rt_list_isempty(&channel->parent.suspend_thread))
    {
        /* search thread list to resume thread */
        n = channel->parent.suspend_thread.next;

        while (n != &(channel->parent.suspend_thread))
        {
            /* get thread */
            thread = rt_list_entry(n, struct rt_thread, tlist);

            status = -RT_ERROR;


            //statement
            /* 一个thread只会被阻塞在一个channel_recv上,不可能同时阻塞在两个上; msg_ret取走,那么就ok了; 那么还需要一个结构, 阅读了即清 */
            /* 如果msg_ret不为空, 那么证明这个thread还没有接受上次的 */
            if(thread->msg_ret != RT_NULL)
            {
                /* 需要增加处理逻辑,比如等待失败时间 */
                rt_kprintf("%s has unread msg, send suspend.\n",thread->name);
                while(thread->msg_ret != RT_NULL)
                {
                    rt_kprintf("%s has unread msg, send suspend.\n",thread->name);
                    rt_thread_mdelay(500);
                }
            }

            //don't change the thread's status
            //发送的消息
            thread->msg_ret = (void*)ipc_msg;



            status = RT_EOK;

            rt_kprintf("CH%d - Thread %s\r\n", fd, thread->name);

            /* move node to the next *///先移动可能是怕这个n被销毁了
            n = n->next;

            /* condition is satisfied, resume thread */
            if (status == RT_EOK)
            {
                /* clear event */
                // if (thread->event_info & RT_EVENT_FLAG_CLEAR)
                //     event->set &= ~thread->event_set;

                /* resume thread, and thread list breaks out */
                /* 准备恢复这个thread, 然后调用shcedule执行调度恢复;remove from suspend list */
                rt_thread_resume(thread);

                //改变队列状态
                channel->reader_queue.flag = 1;
                //增加到待阅读队列,但是好像不能用tlist?可以使用?因为已经从suspend的状态中读取了//在resume中tlist清空了
                //////使用tlist程序跑飞，tlist跟调度相关，切勿使用
                //////rt_list_insert_after(&channel->wait_thread, &(thread->tlist));

                if(need_reply)
                {
                    bin_ipc_msg_t reply_msg = ipc_msg_alloc();
                    reply_msg->msg.sender = thread;
                    reply_msg->msg.type = 0;//还没接受到消息
                    rt_list_insert_after(&channel->reader_queue.waiting_list, &reply_msg->mlist);
                }

                /* need do a scheduling */
                need_schedule = RT_TRUE;
            }

        }

    }

    if(need_reply)
    {
        /* 如果需要回复,那么阻塞此进程,等待回复 */
        rt_thread_suspend(rt_thread_self());
    }

    /* enable interrupt */
    rt_hw_interrupt_enable(level);

    /* do a schedule */
    if (need_schedule == RT_TRUE)
        rt_schedule();
    
    if(need_reply)
    {
        /* 从阻塞中恢复 */
        //判断是否全部回复,或者得到了其它可以中止接受全局回复的指令?
        //设计是回复一个激活一下还是全部回复完毕再激活?————————————————答：看我们的需求

        while(1)
        {
            level = rt_hw_interrupt_disable();

            if (!rt_list_isempty(&channel->wait_msg))
            {
                /* search thread list to resume thread */
                n = channel->wait_msg.next;

                while (n != &(channel->wait_msg))
                {
                    /* get thread */
                    ipc_msg = rt_list_entry(n, struct bin_ipc_msg, mlist);

                    n = n->next;
                
                    //暂时把接收到的消息逻辑放在这里..

                    rt_kprintf("[%s]reply: %d\n", ipc_msg->msg.sender->name, ipc_msg->msg.u.d);

                    rt_list_remove(&ipc_msg->mlist);

                    //ipc_msg暂时不能free,意思一下
                    ipc_msg_free(ipc_msg);
                }
                //能从这里出来,说明能free(remove)的都free完了..

            }else{
                //走这里出来,说明出问题了
                rt_hw_interrupt_enable(level);
                while(1)
                {
                    rt_kprintf("%s channel recv reply error!\n", channel->reply->name);
                    rt_thread_mdelay(1000);
                }
                break;
            }


            if(rt_list_isempty(&channel->wait_msg) && (rt_list_isempty(&channel->reader_queue.waiting_list)))
            {
                //这里说明循环完毕,需要退出while循环
                rt_hw_interrupt_enable(level);
                break;
            }

            //继续挂起
            rt_thread_suspend(rt_thread_self());
            rt_hw_interrupt_enable(level);
            rt_schedule();
        }

        //only break can out

    }



    
    // if (!rt_list_isempty(&channel->parent.suspend_thread))
    // {
    //     /* search thread list to resume thread */
    //     n = channel->parent.suspend_thread.next;

    //     while (n != &(channel->parent.suspend_thread))
    //     {
    //         /* get thread */
    //         thread = rt_list_entry(n, struct rt_thread, tlist);

    //         rt_kprintf("%s test!\n", thread->name);
    //         n = n->next;
    //     }
    // }

    return RT_EOK;
}

/**
 * 消息回复
 * @param fd fd_table
 * @param msg 回复的消息
 */
rt_err_t bin_channel_reply(int fd, bin_channel_msg_t msg)
{
    register rt_ubase_t level;
    struct rt_thread *thread;

    bin_channel_t channel;
    struct dfs_fd* d;

    bin_ipc_msg_t ipc_msg;

    /* get current thread */
    thread = rt_thread_self();

    d = fd_get(fd);
    channel = (bin_channel_t)d->data;
    fd_put(d);

    //阻塞接收
    RT_OBJECT_HOOK_CALL(rt_object_trytake_hook, (&(channel->parent.parent)));


    /* disable interrupt */
    level = rt_hw_interrupt_disable();

    /** //这是在reply中申请的方式,现在转换为在send阶段申请
    ipc_msg = ipc_msg_alloc();

    if(ipc_msg == RT_NULL)
        return -RT_ENOMEM;
    
    ipc_msg_init(ipc_msg, msg, 0);
    **/
    struct rt_list_node *n;
    int find_ok = RT_FALSE;
    bin_ipc_msg_t current_msg = RT_NULL;

    if (!rt_list_isempty(&channel->reader_queue.waiting_list))
    {
        /* search thread list to resume thread */
        n = channel->reader_queue.waiting_list.next;


        while (n != &(channel->reader_queue.waiting_list))
        {
            current_msg = rt_list_entry(n, struct bin_ipc_msg, mlist);

						//n迭代，防止丢失n->next信息
            n = n->next; 
					
            if(current_msg->msg.sender != thread)
                continue;

            rt_kprintf("%s Reply msg found\n", thread->name);




            //找到了这个东西,那么从waiting_list中删除
            rt_list_remove(&current_msg->mlist);

            find_ok = RT_TRUE;

            //消息初始化
            ipc_msg_init(current_msg, msg, 0);

            //初始化完成,加入wait_msg中
            rt_list_insert_after(&channel->wait_msg, &current_msg->mlist);

            //跳出while循环,因为reply是针对单个recv的。
            break;
        }
    }

    if(find_ok == RT_TRUE)
    {

        /* 激活回复的线程,后面要升级成在send时候，是等待全部消息都reply再激活线程还是回复一次激活一次,但是这种方式就不能在send里面wait了，需要结合其它函数一起使用 */
        rt_thread_t reply_thread = (rt_thread_t) channel->reply;

        rt_thread_resume(reply_thread);
    }

    rt_hw_interrupt_enable(level);


    return find_ok == RT_TRUE ? RT_EOK : -RT_ERROR;
}





int ch = -1;

static void channel_send_entry(void *parameter)
{
    int ch2;
    ch2 = bin_channel_open("ch_t", 0);
    rt_kprintf("send entry\r\n", ch);

    struct bin_channel_msg msg;
    rt_err_t err = RT_EOK;

    msg.u.d = (void *)0x12345678;

    err = bin_channel_send(ch2, &msg, 1);

    rt_kprintf("SEND:ch%d errcode: %d\r\n", ch2, err);

    //while(1);
}

static void channel_recv_entry(void *parameter)
{
    //ch = bin_channel_open("ch_t", 0);
    //rt_kprintf("ch%d add.\r\n", ch);


    ch = bin_channel_open("ch_t", 0);
    rt_kprintf("recv entry\r\n");

    struct bin_channel_msg msg;
    rt_err_t err = RT_EOK;

    err = bin_channel_recv(ch, -1, &msg);

    rt_kprintf("RECV:ch%d errcode: %d\r\n", ch, err);

    bin_channel_reply(ch, &msg);

    //rt_list_remove(&(rt_thread_self()->tlist));

    //while(1);
}

static void channel_recv2_entry(void *parameter)
{
    int ch2;
    ch2 = bin_channel_open("ch_t", 0);
    rt_kprintf("recv entry\r\n");

    struct bin_channel_msg msg;
    rt_err_t err = RT_EOK;

    err = bin_channel_recv(ch, -1, &msg);

    rt_kprintf("RECV:ch%d errcode: %d\r\n", ch2, err);

    msg.u.d = (void *)1234;

    bin_channel_reply(ch, &msg);

    //while(1);
}



static rt_thread_t tid1 = RT_NULL, tid2 = RT_NULL, tid3 = RT_NULL;

int channel_test(int argc, char **argv)
{

    if (argc == 1)
    {
        tid1 = rt_thread_create("ch_recv", channel_recv_entry, NULL, 512, 10, 20);
        if(tid1 != RT_NULL)
            rt_thread_startup(tid1);

        tid3 = rt_thread_create("ch_recv2", channel_recv2_entry, NULL, 512, 10, 20);
        if(tid3 != RT_NULL)
            rt_thread_startup(tid3);

        rt_thread_mdelay(500);


        tid2 = rt_thread_create("ch_send", channel_send_entry, NULL, 512, 10, 20);
        if(tid2 != RT_NULL)
            rt_thread_startup(tid2);

        return 0;
    }else{

        if(argc > 1)
        {

            rt_kprintf("ch delete: %x\r\n", bin_channel_close(ch));
					
            ch--;

            return 0;
        }

    }
		
		return 0;
}


MSH_CMD_EXPORT(channel_test, channel_test!);



/**@}*/
