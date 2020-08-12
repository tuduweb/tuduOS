/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2014-2019, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: It is an head file for this library. You can see all be called functions.
 * Created on: 2014-09-10
 */


#ifndef EASYFLASH_H_
#define EASYFLASH_H_

#include <ef_cfg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


/* EasyFlash debug print function. Must be implement by user. */
#ifdef PRINT_DEBUG
#define EF_DEBUG(...) ef_log_debug(__FILE__, __LINE__, __VA_ARGS__)
#else
#define EF_DEBUG(...)
#endif
/* EasyFlash routine print function. Must be implement by user. */
#define EF_INFO(...)  ef_log_info(__VA_ARGS__)
/* EasyFlash assert for developer. */
#define EF_ASSERT(EXPR)                                                       \
if (!(EXPR))                                                                  \
{                                                                             \
    EF_DEBUG("(%s) has assert failed at %s.\n", #EXPR, __FUNCTION__);         \
    while (1);                                                                \
}

/**
 * ENV version number defined by user.
 * Please change it when your firmware add a new ENV to default_env_set.
 */
#ifndef EF_ENV_VER_NUM
#define EF_ENV_VER_NUM                 0
#endif

/* EasyFlash software version number */
#define EF_SW_VERSION                  "4.0.0"
#define EF_SW_VERSION_NUM              0x40000

typedef struct _ef_env {
    char *key;
    void *value;
    size_t value_len;
} ef_env, *ef_env_t;

/* EasyFlash error code */
typedef enum {
    EF_NO_ERR,
    EF_ERASE_ERR,
    EF_READ_ERR,
    EF_WRITE_ERR,
    EF_ENV_NAME_ERR,
    EF_ENV_NAME_EXIST,
    EF_ENV_FULL,
    EF_ENV_INIT_FAILED,
} EfErrCode;

/* the flash sector current status */
typedef enum {
    EF_SECTOR_EMPTY,
    EF_SECTOR_USING,
    EF_SECTOR_FULL,
} EfSecrorStatus;


/* move something from .c to .h */

enum env_status {
    ENV_UNUSED,
    ENV_PRE_WRITE,//准备写入
    ENV_WRITE,//已写入
    ENV_PRE_DELETE,//准备删除
    ENV_DELETED,//已删除
    ENV_ERR_HDR,
    ENV_STATUS_NUM,
};
typedef enum env_status env_status_t;

struct env_cache_node {
    uint16_t name_crc;                           /**< ENV name's CRC32 low 16bit value */
    uint16_t active;                             /**< ENV node access active degree */
    uint32_t addr;                               /**< ENV node address */
};
typedef struct env_cache_node *env_cache_node_t;

struct sector_cache_node {
    uint32_t addr;                               /**< sector start address */
    uint32_t empty_addr;                         /**< sector empty address */
};
typedef struct sector_cache_node *sector_cache_node_t;

/* the ENV max name length must less then it */
#ifndef EF_ENV_NAME_MAX
#define EF_ENV_NAME_MAX                          32
#endif

struct env_meta_data {
    env_status_t status;                         /**< ENV node status, @see node_status_t */
    bool crc_is_ok;                              /**< ENV node CRC32 check is OK */
    uint8_t name_len;                            /**< name length */
    uint32_t magic;                              /**< magic word(`K`, `V`, `4`, `0`) */
    uint32_t len;                                /**< ENV node total length (header + name + value), must align by EF_WRITE_GRAN */
    uint32_t value_len;                          /**< value length */
    char name[EF_ENV_NAME_MAX];                  /**< name */
    struct {
        uint32_t start;                          /**< ENV node start address */
        uint32_t value;                          /**< value start address */
    } addr;
};
typedef struct env_meta_data *env_meta_data_t;

/* easyflash.c */
EfErrCode easyflash_init(void);

#ifdef EF_USING_ENV
/* only supported on ef_env.c */
size_t ef_get_env_blob(const char *key, void *value_buf, size_t buf_len, size_t *saved_value_len);
EfErrCode ef_set_env_blob(const char *key, const void *value_buf, size_t buf_len);

/* ef_env.c, ef_env_legacy_wl.c and ef_env_legacy.c */
EfErrCode ef_load_env(void);
void ef_print_env(void);
char *ef_get_env(const char *key);
EfErrCode ef_set_env(const char *key, const char *value);
EfErrCode ef_del_env(const char *key);
EfErrCode ef_save_env(void);
EfErrCode ef_env_set_default(void);
size_t ef_get_env_write_bytes(void);
EfErrCode ef_set_and_save_env(const char *key, const char *value);
EfErrCode ef_del_and_save_env(const char *key);
#endif

#ifdef EF_USING_IAP
/* ef_iap.c */
EfErrCode ef_erase_bak_app(size_t app_size);
EfErrCode ef_erase_user_app(uint32_t user_app_addr, size_t user_app_size);
EfErrCode ef_erase_spec_user_app(uint32_t user_app_addr, size_t app_size,
                                 EfErrCode (*app_erase)(uint32_t addr, size_t size));
EfErrCode ef_erase_bl(uint32_t bl_addr, size_t bl_size);
EfErrCode ef_write_data_to_bak(uint8_t *data, size_t size, size_t *cur_size,
                               size_t total_size);
EfErrCode ef_copy_app_from_bak(uint32_t user_app_addr, size_t app_size);
EfErrCode ef_copy_spec_app_from_bak(uint32_t user_app_addr, size_t app_size,
                                    EfErrCode (*app_write)(uint32_t addr, const uint32_t *buf, size_t size));
EfErrCode ef_copy_bl_from_bak(uint32_t bl_addr, size_t bl_size);
uint32_t ef_get_bak_app_start_addr(void);
#endif

#ifdef EF_USING_LOG
/* ef_log.c */
EfErrCode ef_log_read(size_t index, uint32_t *log, size_t size);
EfErrCode ef_log_write(const uint32_t *log, size_t size);
EfErrCode ef_log_clean(void);
size_t ef_log_get_used_size(void);
#endif

/* ef_utils.c */
uint32_t ef_calc_crc32(uint32_t crc, const void *buf, size_t size);

/* ef_port.c */
EfErrCode ef_port_read(uint32_t addr, uint32_t *buf, size_t size);
EfErrCode ef_port_erase(uint32_t addr, size_t size);
EfErrCode ef_port_write(uint32_t addr, const uint32_t *buf, size_t size);
void ef_port_env_lock(void);
void ef_port_env_unlock(void);
void ef_log_debug(const char *file, const long line, const char *format, ...);
void ef_log_info(const char *format, ...);
void ef_print(const char *format, ...);

#ifdef __cplusplus
}
#endif

#include <dfs_fs.h>
#include <dfs_file.h>

/* 跟easyflash原始文件的区别是，把一些全局变量封装到了这个结构体中,可以实现多xipfs分区! */
struct ef_env_dev{
    uint32_t env_start_addr;
    const ef_env *default_env_set;//key->value关系变量 跟下面的size对应的
    size_t default_env_set_size;//default env 组的大小
    _Bool init_ok;
    _Bool gc_request;
    _Bool in_recovery_check;
    rt_device_t flash;    //此结构体等同于初始化时候的addr，用于ef_port的首地址
    const struct fal_partition* part;//挂载在哪个分区,新增
    size_t sector_size;
    struct env_cache_node env_cache_table[16];
    struct sector_cache_node sector_cache_table[4];
};
typedef struct ef_env_dev *ef_env_dev_t;

#endif /* EASYFLASH_H_ */
