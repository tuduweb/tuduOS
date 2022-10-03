#ifndef __BIN_MPU_H__
#define __BIN_MPU_H__
#include "rtthread.h"

//定义mpu保护结构体,attach到lwp上
struct lwt_mpu_info{
    rt_uint32_t baseaddr;
    rt_uint32_t size;
    rt_uint32_t rnum;
    rt_uint32_t ap;
};

#endif
