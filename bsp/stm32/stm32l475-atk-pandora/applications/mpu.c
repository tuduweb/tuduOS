#include "mpu.h"
#include "rtthread.h"
#include <rthw.h>
#include "lwt.h"
//---->以下为日志单元的配置项
#define LOG_TAG     "mpu"     // 该模块对应的标签。不定义时，默认：NO_TAG
//#define LOG_LVL     LOG_LVL_DBG   // 该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>                 // 必须在 LOG_TAG 与 LOG_LVL 下面
//日志单元配置项结束<----
#include "stm32l4xx_hal.h"

int MPU_Set_Protection(rt_uint32_t baseaddr, rt_uint32_t size, rt_uint32_t rnum, rt_uint32_t ap)
{
    MPU_Region_InitTypeDef MPU_Initure;

    HAL_MPU_Disable();								        //配置MPU之前先关闭MPU,配置完成以后在使能MPU

    MPU_Initure.Enable=MPU_REGION_ENABLE;			        //使能该保护区域 
    MPU_Initure.Number=rnum;			                    //设置保护区域
    MPU_Initure.BaseAddress=baseaddr;	                    //设置基址
    MPU_Initure.Size=size;				                    //设置保护区域大小
    MPU_Initure.SubRegionDisable=0X00;                      //禁止子区域
    MPU_Initure.TypeExtField=MPU_TEX_LEVEL0;                //设置类型扩展域为level0
    MPU_Initure.AccessPermission=(rt_uint8_t)ap;		            //设置访问权限,
    MPU_Initure.DisableExec=MPU_INSTRUCTION_ACCESS_ENABLE;	//允许指令访问(允许读取指令)
    MPU_Initure.IsShareable=MPU_ACCESS_NOT_SHAREABLE;       //禁止共用
    MPU_Initure.IsCacheable=MPU_ACCESS_NOT_CACHEABLE;       //禁止cache  
    MPU_Initure.IsBufferable=MPU_ACCESS_BUFFERABLE;         //允许缓冲
    HAL_MPU_ConfigRegion(&MPU_Initure);                     //配置MPU
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);			        //开启MPU
    return 0;
}

static int MPU_Init(void)
{

    //MPU_Set_Protection(0x60000000,MPU_REGION_SIZE_64MB,MPU_REGION_NUMBER0,MPU_REGION_NO_ACCESS);
    return 0;
}
INIT_APP_EXPORT(MPU_Init);

struct exception_stack_frame
{
    rt_uint32_t r0;
    rt_uint32_t r1;
    rt_uint32_t r2;
    rt_uint32_t r3;
    rt_uint32_t r12;
    rt_uint32_t lr;
    rt_uint32_t pc;
    rt_uint32_t psr;
};
/**
 * 硬件错误处理 在硬件错误的时候会调用此方法
 **/
rt_err_t exception_handle(struct exception_stack_frame *context)
{
    rt_uint32_t level;
    level = rt_hw_interrupt_disable();

    rt_thread_t thread = rt_thread_self();
    struct rt_lwt* lwp = (struct rt_lwt *)thread->lwp;
    

    if(lwp != RT_NULL)
    {
        LOG_I("process:%s hardfault",thread->name);//这里填入命令参数
        rt_kprintf("psr: 0x%08x\n", context->psr);

        rt_kprintf("r00: 0x%08x\n", context->r0);
        rt_kprintf("r01: 0x%08x\n", context->r1);
        rt_kprintf("r02: 0x%08x\n", context->r2);
        rt_kprintf("r03: 0x%08x\n", context->r3);
        rt_kprintf("r12: 0x%08x\n", context->r12);
        rt_kprintf(" lr: 0x%08x\n", context->lr);
        rt_kprintf(" pc: 0x%08x\n", context->pc);


        uint8_t* text_entry = lwp->text_entry;
        //判断一下 如果这里运行的是用户态app,那么打印app入口地址等
        //如何判断是用户态app?


        //用户态app发生错误 终止本线程即可 内核态不受影响
        //线程停止运行

        //执行调度器
        rt_schedule();

        rt_hw_interrupt_enable(level);
        return -1;
    }else{
        //内核发生错误 系统终止 无法挽回
        LOG_E("thread:%s hard fault in kernel",thread);
        rt_hw_interrupt_enable(level);
        return 0;
    }

}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Main(void)
{
    rt_uint32_t level;
    level = rt_hw_interrupt_disable();
    rt_thread_t thread = rt_thread_self();
    
    rt_thread_delete(thread);
    
    rt_schedule();

    rt_hw_interrupt_enable(level);

    rt_kprintf("memProtect\r\n");

}

/**
 * https://www.keil.com/pack/doc/cmsis/Core/html/group__mpu__functions.html
 */
void bin_lwt_mpu_switch(rt_thread_t from, rt_thread_t to)
{
    //HAL_MPU_Disable();

    if(to->lwp != NULL)
    {
        //MPU_switch
    }else if(1)
    {
        //clean
    }


    //HAL_MPU_Enable();
}
