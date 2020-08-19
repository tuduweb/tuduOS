/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-12-5      SummerGift   first version
 */

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <rtthread.h>
#include <board.h>


#define PKG_USING_FAL
#define FAL_DEBUG_CONFIG
#define FAL_DEBUG 1
#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define FAL_USING_NOR_FLASH_DEV_NAME "W25Q128"
#define PKG_USING_FAL_V00400
#define PKG_FAL_VER_NUM 0x00400
#define PKG_USING_CMSIS
#define PKG_USING_CMSIS_LATEST_VERSION
#define PKG_CMSIS_CORE
/*end : temp*/

//#define NOR_FLASH_DEV_NAME             "W25Q128"


#define FLASH_SIZE_GRANULARITY_16K   (4 * 16 * 1024)
#define FLASH_SIZE_GRANULARITY_64K   (64 * 1024)
#define FLASH_SIZE_GRANULARITY_128K  (7 * 128 * 1024)

#define STM32_FLASH_START_ADRESS_16K  STM32_FLASH_START_ADRESS
#define STM32_FLASH_START_ADRESS_64K  (STM32_FLASH_START_ADRESS_16K + FLASH_SIZE_GRANULARITY_16K)
#define STM32_FLASH_START_ADRESS_128K (STM32_FLASH_START_ADRESS_64K + FLASH_SIZE_GRANULARITY_64K)

extern const struct fal_flash_dev stm32_onchip_flash_16k;
extern const struct fal_flash_dev stm32_onchip_flash_64k;
extern const struct fal_flash_dev stm32_onchip_flash_128k;
extern struct fal_flash_dev nor_flash0;
/* flash device table */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &stm32_onchip_flash_16k,                                         \
    &stm32_onchip_flash_64k,                                         \
    &stm32_onchip_flash_128k,                                        \
    &nor_flash0,                                                     \
}
/* ====================== Partition Configuration ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG

/* partition table */
#define FAL_PART_TABLE                                                                                                     \
{                                                                                                                          \
    {FAL_PART_MAGIC_WROD, "bootloader", "onchip_flash_16k",  0 , FLASH_SIZE_GRANULARITY_16K , 0}, \
    {FAL_PART_MAGIC_WROD, "sys",      "onchip_flash_64k",  0 , FLASH_SIZE_GRANULARITY_64K , 0}, \
    {FAL_PART_MAGIC_WROD, "onchip",        "onchip_flash_128k", 0 , FLASH_SIZE_GRANULARITY_128K, 0}, \
    {FAL_PART_MAGIC_WROD, "exchip0", "exchip_flash",  0 , 8 * 1024 * 1024 , 0}, \
    {FAL_PART_MAGIC_WROD, "ef", "exchip_flash",  8 * 1024 * 1024 , 8 * 1024 * 1024 , 0}, \
}

#endif /* FAL_PART_HAS_TABLE_CFG */
#endif /* _FAL_CFG_H_ */
