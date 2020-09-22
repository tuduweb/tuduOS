# 进程间通讯(IPC)
## 印象

进程间通讯在RT-Thread中，有许多方式。为配合共享内存机制，传输共享内存地址，需要一种新的进程间通讯方式。在Linux中称为管道通信(pipe)。
https://www.cnblogs.com/wuyepeng/p/9747557.html

## 入手
### 源码分析

从mailbox类型入手，从ipc.c中找到rt_mb_create函数。

```c
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
```

类比于其他IPC类型，mainbox的创建函数中，有相似的：
```c
/* allocate mailbox object */
mb = (rt_mailbox_t)rt_object_allocate(RT_Object_Class_MailBox, name);
```
```c
/* allocate event object */
event = (rt_event_t)rt_object_allocate(RT_Object_Class_Event, name);
```

在手册中可以看到，RT-Thread 内核对象模型介绍如下：

`RT-Thread 内核采用面向对象的设计思想进行设计，系统级的基础设施都是一种内核对象，例如线程，信号量，互斥量，定时器等。`

`下图则显示了 RT-Thread 中各类内核对象的派生和继承关系。对于每一种具体内核对象和对象控制块，除了基本结构外，还有自己的扩展属性（私有属性），例如，对于线程控制块，在基类对象基础上进行扩展，增加了线程状态、优先级等属性。这些属性在基类对象的操作中不会用到，只有在与具体线程相关的操作中才会使用。因此从面向对象的观点，可以认为每一种具体对象是抽象对象的派生，继承了基本对象的属性并在此基础上扩展了与自己相关的属性。`

![内核对象模型](../images/core/03kernel_object2.png)

所有IPC对象都有相同的 `struct rt_ipc_object parent` 结构，故有很多相通性。