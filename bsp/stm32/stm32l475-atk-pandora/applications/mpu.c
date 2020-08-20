#include "mpu.h"
#include "rtthread.h"
#include <rthw.h>
#include "lwt.h"
//---->����Ϊ��־��Ԫ��������
#define LOG_TAG     "mpu"     // ��ģ���Ӧ�ı�ǩ��������ʱ��Ĭ�ϣ�NO_TAG
//#define LOG_LVL     LOG_LVL_DBG   // ��ģ���Ӧ����־������𡣲�����ʱ��Ĭ�ϣ����Լ���
#include <ulog.h>                 // ������ LOG_TAG �� LOG_LVL ����
//��־��Ԫ���������<----
#include "stm32l4xx_hal.h"

int MPU_Set_Protection(rt_uint32_t baseaddr, rt_uint32_t size, rt_uint32_t rnum, rt_uint32_t ap)
{
    MPU_Region_InitTypeDef MPU_Initure;

    HAL_MPU_Disable();								        //����MPU֮ǰ�ȹر�MPU,��������Ժ���ʹ��MPU

    MPU_Initure.Enable=MPU_REGION_ENABLE;			        //ʹ�ܸñ������� 
    MPU_Initure.Number=rnum;			                    //���ñ�������
    MPU_Initure.BaseAddress=baseaddr;	                    //���û�ַ
    MPU_Initure.Size=size;				                    //���ñ��������С
    MPU_Initure.SubRegionDisable=0X00;                      //��ֹ������
    MPU_Initure.TypeExtField=MPU_TEX_LEVEL0;                //����������չ��Ϊlevel0
    MPU_Initure.AccessPermission=(rt_uint8_t)ap;		            //���÷���Ȩ��,
    MPU_Initure.DisableExec=MPU_INSTRUCTION_ACCESS_ENABLE;	//����ָ�����(�����ȡָ��)
    MPU_Initure.IsShareable=MPU_ACCESS_NOT_SHAREABLE;       //��ֹ����
    MPU_Initure.IsCacheable=MPU_ACCESS_NOT_CACHEABLE;       //��ֹcache  
    MPU_Initure.IsBufferable=MPU_ACCESS_BUFFERABLE;         //������
    HAL_MPU_ConfigRegion(&MPU_Initure);                     //����MPU
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);			        //����MPU
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
 * Ӳ�������� ��Ӳ�������ʱ�����ô˷���
 **/
rt_err_t exception_handle(struct exception_stack_frame *context)
{
    rt_uint32_t level;
    level = rt_hw_interrupt_disable();

    rt_thread_t thread = rt_thread_self();
    struct rt_lwt* lwp = (struct rt_lwt *)thread->lwp;
    

    if(lwp != RT_NULL)
    {
        LOG_I("process:%s hardfault",thread->name);//���������������
        rt_kprintf("psr: 0x%08x\n", context->psr);

        rt_kprintf("r00: 0x%08x\n", context->r0);
        rt_kprintf("r01: 0x%08x\n", context->r1);
        rt_kprintf("r02: 0x%08x\n", context->r2);
        rt_kprintf("r03: 0x%08x\n", context->r3);
        rt_kprintf("r12: 0x%08x\n", context->r12);
        rt_kprintf(" lr: 0x%08x\n", context->lr);
        rt_kprintf(" pc: 0x%08x\n", context->pc);


        uint8_t* text_entry = lwp->text_entry;
        //�ж�һ�� ����������е����û�̬app,��ô��ӡapp��ڵ�ַ��
        //����ж����û�̬app?


        //�û�̬app�������� ��ֹ���̼߳��� �ں�̬����Ӱ��
        //�߳�ֹͣ����

        //ִ�е�����
        rt_schedule();

        rt_hw_interrupt_enable(level);
        return -1;
    }else{
        //�ں˷������� ϵͳ��ֹ �޷����
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
