#include <rtthread.h>

//extern int syscall(int number, ...);
//extern void lwp_save_sp(void);

static void syscall_app_entry(void *parameter)
{
    //暂时无法测试,因为内核中的create没有运行在lwp下
		//lwp_save_sp(1);
    //lwp_save_sp();
		rt_thread_t thread = rt_thread_self();
    rt_kprintf("syscall APP test begin\n");
    char txt[] = "syscall\n";
		__asm("SVC #1");
    //syscall(0xff, txt, sizeof(txt));
    //syscall(1,1);
    rt_kprintf("syscall APP test end\n");
    // while(1)
    // {
    //   rt_kprintf("App on\n");
    //   rt_thread_mdelay(2000);
    // }
}


void syscall_app_main(uint8_t argc, char **argv)
{
    //暂时无法测试,因为内核中的create没有运行在lwp下
		//lwp_save_sp(1);
    //lwp_save_sp();

    rt_kprintf("syscall APP test begin\n");
    char txt[] = "syscall\n";
    //syscall(0xff, txt, sizeof(txt));
    //syscall(1,1);
    rt_kprintf("syscall APP test end\n");
    // while(1)
    // {
    //   rt_kprintf("App on\n");
    //   rt_thread_mdelay(2000);
    // }
}

static rt_thread_t tid1 = RT_NULL;

int syscall_thread(void)
{
    tid1 = rt_thread_create("app", syscall_app_entry, NULL, 1024, 10, 20);
    if(tid1 != RT_NULL)
        rt_thread_startup(tid1);

    return 0;
}

MSH_CMD_EXPORT(syscall_thread,syscall App!);
