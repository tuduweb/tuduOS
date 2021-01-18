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
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);			        //开启MPU//MPU_PRIVILEGED_DEFAULT:表示使能了背景區，特權級模式可以正常訪問任何未使能MPU的區域。
    return 0;
}

MPU_Region_InitTypeDef MPU_Initure;
static int MPU_Init(void)
{
    //rt_uint32_t baseaddr, rt_uint32_t size, rt_uint32_t rnum, rt_uint32_t ap
    MPU_Initure.Enable=MPU_REGION_DISABLE;			        //使能该保护区域 
    //MPU_Initure.Number=rnum;			                    //设置保护区域
    //MPU_Initure.BaseAddress=baseaddr;	                    //设置基址
    //MPU_Initure.Size=size;				                    //设置保护区域大小
    MPU_Initure.SubRegionDisable=0X00;                      //禁止子区域
    MPU_Initure.TypeExtField=MPU_TEX_LEVEL0;                //设置类型扩展域为level0
    //MPU_Initure.AccessPermission=(rt_uint8_t)ap;		            //设置访问权限,
    MPU_Initure.DisableExec=MPU_INSTRUCTION_ACCESS_ENABLE;	//允许指令访问(允许读取指令)
    MPU_Initure.IsShareable=MPU_ACCESS_NOT_SHAREABLE;       //禁止共用
    MPU_Initure.IsCacheable=MPU_ACCESS_NOT_CACHEABLE;       //禁止cache  
    MPU_Initure.IsBufferable=MPU_ACCESS_BUFFERABLE;         //允许缓冲
    //MPU_Set_Protection(0x40000000, MPU_REGION_SIZE_512MB, MPU_REGION_NUMBER2, MPU_REGION_FULL_ACCESS);
    //HAL_NVIC_SetPriority(PendSV_IRQn, 0, 2);
    //HAL_NVIC_SetPriority(MemoryManagement_IRQn, 1, 2);

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
    //level = rt_hw_interrupt_disable();

    rt_thread_t thread = rt_thread_self();
    struct rt_lwt* lwp = (struct rt_lwt *)thread->lwp;

    if(lwp != RT_NULL)
    {
        LOG_E("process:%s hardfault",thread->name);//这里填入命令参数
        rt_kprintf("lwt text: %p - %p  %d\n", lwp->text_entry, lwp->text_entry + lwp->text_size, lwp->text_size);
        rt_kprintf("kernel stack %p - %p\n", thread->name, thread->stack_addr, (rt_uint32_t)thread->stack_addr + thread->stack_size);
        rt_kprintf("app stack %p - %p\n", thread->name, thread->user_stack, (rt_uint32_t)thread->user_stack + thread->user_stack_size);


        rt_kprintf("psr: 0x%08x\n", context->psr);

        rt_kprintf("r00: 0x%08x\n", context->r0);
        rt_kprintf("r01: 0x%08x\n", context->r1);
        rt_kprintf("r02: 0x%08x\n", context->r2);
        rt_kprintf("r03: 0x%08x\n", context->r3);
        rt_kprintf("r12: 0x%08x\n", context->r12);
        rt_kprintf(" lr: 0x%08x\n", context->lr);

        if((rt_uint32_t)lwp->text_entry <= context->lr && context->lr <= (rt_uint32_t)lwp->text_entry + lwp->text_size)
        {
            rt_kprintf("lr offset: 0x%08x\n", context->lr - (rt_uint32_t)lwp->text_entry);
        }

        rt_kprintf(" pc: 0x%08x\n", context->pc);

        if((rt_uint32_t)lwp->text_entry <= context->pc && context->pc <= (rt_uint32_t)lwp->text_entry + lwp->text_size)
        {
            rt_kprintf("pc offset: 0x%08x\n", context->pc - (rt_uint32_t)lwp->text_entry);
        }

        uint8_t* text_entry = lwp->text_entry;
        //判断一下 如果这里运行的是用户态app,那么打印app入口地址等
        //如何判断是用户态app?


        //用户态app发生错误 终止本线程即可 内核态不受影响
        //线程停止运行

        //执行调度器
        //rt_schedule();

        //rt_hw_interrupt_enable(level);
        return -1;
    }else{
        //内核发生错误 系统终止 无法挽回
        LOG_E("thread:%s memmanage fault in kernel",thread);
        //rt_hw_interrupt_enable(level);
        return 0;
    }

}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Main(struct exception_stack_frame * sp)
{
    rt_uint32_t level;

    level = rt_hw_interrupt_disable();
    
    exception_handle(sp);
    
    rt_thread_t thread = rt_thread_self();
    
    rt_thread_delete(thread);
    
    rt_schedule();

    rt_hw_interrupt_enable(level);

    rt_kprintf("memProtect\r\n");

}



// void HAL_MPU_ConfigRegion(MPU_Region_InitTypeDef *MPU_Init)
// {
//   /* Check the parameters */
//   assert_param(IS_MPU_REGION_NUMBER(MPU_Init->Number));
//   assert_param(IS_MPU_REGION_ENABLE(MPU_Init->Enable));

//   /* Set the Region number */
//   MPU->RNR = MPU_Init->Number;

//   if ((MPU_Init->Enable) != RESET)
//   {
//     /* Check the parameters */
//     assert_param(IS_MPU_INSTRUCTION_ACCESS(MPU_Init->DisableExec));
//     assert_param(IS_MPU_REGION_PERMISSION_ATTRIBUTE(MPU_Init->AccessPermission));
//     assert_param(IS_MPU_TEX_LEVEL(MPU_Init->TypeExtField));
//     assert_param(IS_MPU_ACCESS_SHAREABLE(MPU_Init->IsShareable));
//     assert_param(IS_MPU_ACCESS_CACHEABLE(MPU_Init->IsCacheable));
//     assert_param(IS_MPU_ACCESS_BUFFERABLE(MPU_Init->IsBufferable));
//     assert_param(IS_MPU_SUB_REGION_DISABLE(MPU_Init->SubRegionDisable));
//     assert_param(IS_MPU_REGION_SIZE(MPU_Init->Size));

//     MPU->RBAR = MPU_Init->BaseAddress;
//     MPU->RASR = ((uint32_t)MPU_Init->DisableExec        << MPU_RASR_XN_Pos)   |
//                 ((uint32_t)MPU_Init->AccessPermission   << MPU_RASR_AP_Pos)   |
//                 ((uint32_t)MPU_Init->TypeExtField       << MPU_RASR_TEX_Pos)  |
//                 ((uint32_t)MPU_Init->IsShareable        << MPU_RASR_S_Pos)    |
//                 ((uint32_t)MPU_Init->IsCacheable        << MPU_RASR_C_Pos)    |
//                 ((uint32_t)MPU_Init->IsBufferable       << MPU_RASR_B_Pos)    |
//                 ((uint32_t)MPU_Init->SubRegionDisable   << MPU_RASR_SRD_Pos)  |
//                 ((uint32_t)MPU_Init->Size               << MPU_RASR_SIZE_Pos) |
//                 ((uint32_t)MPU_Init->Enable             << MPU_RASR_ENABLE_Pos);
//   }
//   else
//   {
//     MPU->RBAR = 0x00;
//     MPU->RASR = 0x00;
//   }
// }

// typedef struct
// {
//   __IM  uint32_t TYPE;                   /*!< Offset: 0x000 (R/ )  MPU Type Register */
//   __IOM uint32_t CTRL;//MPU->CTRL = MPU_Control | MPU_CTRL_ENABLE_Msk;                   /*!< Offset: 0x004 (R/W)  MPU Control Register */
//   __IOM uint32_t RNR;//MPU->RNR = MPU_Init->Number;                    /*!< Offset: 0x008 (R/W)  MPU Region RNRber Register */
//   __IOM uint32_t RBAR;//MPU->RBAR = MPU_Init->BaseAddress;                   /*!< Offset: 0x00C (R/W)  MPU Region Base Address Register */
//   __IOM uint32_t RASR;//MPU->RASR = {CONFIG}                   /*!< Offset: 0x010 (R/W)  MPU Region Attribute and Size Register */
// } MPU_Type;
/**
 * https://www.keil.com/pack/doc/cmsis/Core/html/group__mpu__functions.html
 */
void bin_lwt_mpu_switch(rt_uint32_t from_sp, rt_uint32_t to_sp)
{
    /**
     * 在LWT线程中需要切换MPU保护
     * 
     */
    //HAL_MPU_Disable();
    //TODO:首次from会为空 这里的可以优化
    if(from_sp == NULL) return;
    rt_thread_t from = (struct rt_thread*)(from_sp - struct_offset(struct rt_thread, sp) );
    rt_thread_t to = (struct rt_thread*)(to_sp - struct_offset(struct rt_thread, sp) );

    MPU_Region_InitTypeDef MPU_Initure;

    if(to->lwp != NULL)
    {
        //MPU_switch
        //切换过去的是LWP线程,那么需要启动隔离相关的东西..
        //判断from和to是不是同一个东西
        rt_kprintf("swtich from %s to LWT %s\r\n", from->name, to->name);

    }else if((from->lwp != NULL))
    {
        //普通线程..
        rt_kprintf("swtich from LWT %s to %s\r\n", from->name, to->name);
    }else{
        //from/to not lwp thread
        // MPU_Set_Protection(0x08000000, MPU_REGION_SIZE_512KB, MPU_REGION_NUMBER0, MPU_REGION_FULL_ACCESS);
        // //SRAM1 RAM:128kb
        // MPU_Set_Protection(0x20000000, MPU_REGION_SIZE_128KB, MPU_REGION_NUMBER1, MPU_REGION_FULL_ACCESS);
        // //Peripherals:
        // MPU_Set_Protection(0x40000000, MPU_REGION_SIZE_512MB, MPU_REGION_NUMBER2, MPU_REGION_FULL_ACCESS);
        // //Cortex-M4 Internal Peripherals
        // MPU_Set_Protection(0xE0000000, MPU_REGION_SIZE_512MB, MPU_REGION_NUMBER3, MPU_REGION_FULL_ACCESS);
    
        //TODO:改成寄存器操作
        MPU_Initure.BaseAddress=0x08000000;//设置基址
        MPU_Initure.Size=MPU_REGION_SIZE_512KB;//设置保护区域大小
        MPU_Initure.Number=MPU_REGION_NUMBER0;//设置保护区域
        MPU_Initure.AccessPermission=(rt_uint8_t)MPU_REGION_FULL_ACCESS;//设置访问权限,
        HAL_MPU_ConfigRegion(&MPU_Initure);//配置MPU

        MPU_Initure.BaseAddress=0x20000000;//设置基址
        MPU_Initure.Size=MPU_REGION_SIZE_128KB;//设置保护区域大小
        MPU_Initure.Number=MPU_REGION_NUMBER1;//设置保护区域
        HAL_MPU_ConfigRegion(&MPU_Initure);//配置MPU

        MPU_Initure.BaseAddress=0x40000000;//设置基址
        MPU_Initure.Size=MPU_REGION_SIZE_512MB;//设置保护区域大小
        MPU_Initure.Number=MPU_REGION_NUMBER2;//设置保护区域
        HAL_MPU_ConfigRegion(&MPU_Initure);//配置MPU

        MPU_Initure.BaseAddress=0xE0000000;//设置基址
        MPU_Initure.Size=MPU_REGION_SIZE_512MB;//设置保护区域大小
        MPU_Initure.Number=MPU_REGION_NUMBER3;//设置保护区域
        HAL_MPU_ConfigRegion(&MPU_Initure);//配置MPU
        HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);			        //开启MPU//MPU_PRIVILEGED_DEFAULT:表示使能了背景區，特權級模式可以正常訪問任何未使能MPU的區域。

    }


    //HAL_MPU_Enable();
}

static void mpu_test_entry(void *parameter)
{
    int* testPointer = (int *)0x20012000;

    *testPointer = 1234;

    rt_kprintf("MPU test %d\n", *testPointer);
}
rt_thread_t tid1;
int mpu_test(int argc, char **argv)
{
    if (argc == 1)
    {
        tid1 = rt_thread_create("mpu_test", mpu_test_entry, NULL, 512, 10, 20);
        if(tid1 != RT_NULL)
            rt_thread_startup(tid1);

    }else{
        //Flash memory:512kb
        MPU_Set_Protection(0x08000000, MPU_REGION_SIZE_512KB, MPU_REGION_NUMBER0, MPU_REGION_FULL_ACCESS);
        //SRAM1 RAM:128kb
        MPU_Set_Protection(0x20000000, MPU_REGION_SIZE_128KB, MPU_REGION_NUMBER1, MPU_REGION_FULL_ACCESS);
        //Peripherals:
        MPU_Set_Protection(0x40000000, MPU_REGION_SIZE_512MB, MPU_REGION_NUMBER2, MPU_REGION_FULL_ACCESS);
        //Cortex-M4 Internal Peripherals
        MPU_Set_Protection(0xE0000000, MPU_REGION_SIZE_512MB, MPU_REGION_NUMBER3, MPU_REGION_FULL_ACCESS);
        //MPU_Set_Protection(0x20012000, MPU_REGION_SIZE_1KB, MPU_REGION_NUMBER1, MPU_REGION_NO_ACCESS);
        //
    }

    return 0;
}



MSH_CMD_EXPORT(mpu_test, mpu_test!);
