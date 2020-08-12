/*
 * This file is part of the EasyFlash Library.
 *
 * Copyright (c) 2019, Armink, <armink.ztl@gmail.com>
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
 * Function: Environment variables operating interface. This is the Next Generation version.
 * Created on: 2019-02-02
 */

#include <string.h>
#include <easyflash.h>

#if defined(EF_USING_ENV) && !defined(EF_ENV_USING_LEGACY_MODE)

#ifndef EF_WRITE_GRAN
#error "Please configure flash write granularity (in ef_cfg.h)"
#endif

#if EF_WRITE_GRAN != 1 && EF_WRITE_GRAN != 8 && EF_WRITE_GRAN != 32 && EF_WRITE_GRAN != 64
#error "the write gran can be only setting as 1, 8, 32 and 64"
#endif

/* the ENV max name length must less then it */
#ifndef EF_ENV_NAME_MAX
#define EF_ENV_NAME_MAX                          32
#endif

/* magic word(`E`, `F`, `4`, `0`) */
#define SECTOR_MAGIC_WORD                        0x30344645
/* magic word(`K`, `V`, `4`, `0`) */
#define ENV_MAGIC_WORD                           0x3034564B

/* the using status sector table length */
#ifndef USING_SECTOR_TABLE_LEN
#define USING_SECTOR_TABLE_LEN                   4
#endif

/* the string ENV value buffer size for legacy ef_get_env() function */
#ifndef EF_STR_ENV_VALUE_MAX_SIZE
#define EF_STR_ENV_VALUE_MAX_SIZE                128
#endif

/* the sector remain threshold before full status */
#ifndef EF_SEC_REMAIN_THRESHOLD
#define EF_SEC_REMAIN_THRESHOLD                  (ENV_HDR_DATA_SIZE + EF_ENV_NAME_MAX)
#endif

/* the total remain empty sector threshold before GC */
#ifndef EF_GC_EMPTY_SEC_THRESHOLD
#define EF_GC_EMPTY_SEC_THRESHOLD                1
#endif

/* the ENV cache table size, it will improve ENV search speed when using cache */
#ifndef EF_ENV_CACHE_TABLE_SIZE
#define EF_ENV_CACHE_TABLE_SIZE                  0/*16*/
#endif

/* the sector cache table size, it will improve ENV save speed when using cache */
#ifndef EF_SECTOR_CACHE_TABLE_SIZE
#define EF_SECTOR_CACHE_TABLE_SIZE               0/*4*/
#endif

#if EF_ENV_CACHE_TABLE_SIZE > 0xFFFF
#error "The ENV cache table size must less than 0xFFFF"
#endif

#if (EF_ENV_CACHE_TABLE_SIZE > 0) && (EF_SECTOR_CACHE_TABLE_SIZE > 0)
#define EF_ENV_USING_CACHE
#endif

/* the sector is not combined value */
#define SECTOR_NOT_COMBINED                      0xFFFFFFFF
/* the next address is get failed */
#define FAILED_ADDR                              0xFFFFFFFF

/* Return the most contiguous size aligned at specified width. RT_ALIGN(13, 4)
 * would return 16.
 */
#define EF_ALIGN(size, align)                    (((size) + (align) - 1) & ~((align) - 1))
/* align by write granularity */
#define EF_WG_ALIGN(size)                        (EF_ALIGN(size, (EF_WRITE_GRAN + 7)/8))
/**
 * Return the down number of aligned at specified width. RT_ALIGN_DOWN(13, 4)
 * would return 12.
 */
#define EF_ALIGN_DOWN(size, align)               ((size) & ~((align) - 1))
/* align down by write granularity */
#define EF_WG_ALIGN_DOWN(size)                   (EF_ALIGN_DOWN(size, (EF_WRITE_GRAN + 7)/8))

#if (EF_WRITE_GRAN == 1)
#define STATUS_TABLE_SIZE(status_number)         ((status_number * EF_WRITE_GRAN + 7)/8)
#else
#define STATUS_TABLE_SIZE(status_number)         (((status_number - 1) * EF_WRITE_GRAN + 7)/8)
#endif

#define STORE_STATUS_TABLE_SIZE                  STATUS_TABLE_SIZE(SECTOR_STORE_STATUS_NUM)
#define DIRTY_STATUS_TABLE_SIZE                  STATUS_TABLE_SIZE(SECTOR_DIRTY_STATUS_NUM)
#define ENV_STATUS_TABLE_SIZE                    STATUS_TABLE_SIZE(ENV_STATUS_NUM)

#define SECTOR_SIZE                              EF_ERASE_MIN_SIZE
#define SECTOR_NUM                               (ENV_AREA_SIZE / (EF_ERASE_MIN_SIZE))

#if (SECTOR_NUM < 2)
#error "The sector number must lager then or equal to 2"
#endif

#if (EF_GC_EMPTY_SEC_THRESHOLD == 0 || EF_GC_EMPTY_SEC_THRESHOLD >= SECTOR_NUM)
#error "There is at least one empty sector for GC."
#endif

#define SECTOR_HDR_DATA_SIZE                     (EF_WG_ALIGN(sizeof(struct sector_hdr_data)))
#define SECTOR_DIRTY_OFFSET                      ((unsigned long)(&((struct sector_hdr_data *)0)->status_table.dirty))
#define SECTOR_COMBINED_OFFSET                   ((unsigned long)(&((struct sector_hdr_data *)0)->combined))
#define ENV_HDR_DATA_SIZE                        (EF_WG_ALIGN(sizeof(struct env_hdr_data)))
#define ENV_MAGIC_OFFSET                         ((unsigned long)(&((struct env_hdr_data *)0)->magic))
#define ENV_LEN_OFFSET                           ((unsigned long)(&((struct env_hdr_data *)0)->len))
#define ENV_NAME_LEN_OFFSET                      ((unsigned long)(&((struct env_hdr_data *)0)->name_len))

#define VER_NUM_ENV_NAME                         "__ver_num__"

enum sector_store_status {
    SECTOR_STORE_UNUSED,
    SECTOR_STORE_EMPTY,//扇区空
    SECTOR_STORE_USING,//扇区正在使用
    SECTOR_STORE_FULL,//扇区全满
    SECTOR_STORE_STATUS_NUM,
};
typedef enum sector_store_status sector_store_status_t;

enum sector_dirty_status {
    SECTOR_DIRTY_UNUSED,
    SECTOR_DIRTY_FALSE,//干净
    SECTOR_DIRTY_TRUE,//已脏
    SECTOR_DIRTY_GC,//正在执行辣鸡回收
    SECTOR_DIRTY_STATUS_NUM,
};
typedef enum sector_dirty_status sector_dirty_status_t;



struct sector_hdr_data {
    struct {
        uint8_t store[STORE_STATUS_TABLE_SIZE];  /**< sector store status @see sector_store_status_t */
        uint8_t dirty[DIRTY_STATUS_TABLE_SIZE];  /**< sector dirty status @see sector_dirty_status_t */
    } status_table;
    uint32_t magic;                              /**< magic word(`E`, `F`, `4`, `0`) */
    uint32_t combined;                           /**< the combined next sector number, 0xFFFFFFFF: not combined */
    uint32_t reserved;
};//扇区原始数据
typedef struct sector_hdr_data *sector_hdr_data_t;

struct sector_meta_data {
    bool check_ok;                               /**< sector header check is OK */
    struct {
        sector_store_status_t store;             /**< sector store status @see sector_store_status_t */
        sector_dirty_status_t dirty;             /**< sector dirty status @see sector_dirty_status_t */
    } status;
    uint32_t addr;                               /**< sector start address */
    uint32_t magic;                              /**< magic word(`E`, `F`, `4`, `0`) */
    uint32_t combined;                           /**< the combined next sector number, 0xFFFFFFFF: not combined */
    size_t remain;                               /**< remain size */
    uint32_t empty_env;                          /**< the next empty ENV node start address */
};//扇区节数据
typedef struct sector_meta_data *sector_meta_data_t;

struct env_hdr_data {
    uint8_t status_table[ENV_STATUS_TABLE_SIZE]; /**< ENV node status, @see node_status_t */
    uint32_t magic;                              /**< magic word(`K`, `V`, `4`, `0`) */
    uint32_t len;                                /**< ENV node total length (header + name + value), must align by EF_WRITE_GRAN */
    uint32_t crc32;                              /**< ENV node crc32(name_len + data_len + name + value) */
    uint8_t name_len;                            /**< name length */
    uint32_t value_len;                          /**< value length */
};//ENV原始数据
typedef struct env_hdr_data *env_hdr_data_t;





static void gc_collect(void);

/* ENV start address in flash */
static uint32_t env_start_addr = 0;
/* default ENV set, must be initialized by user */
static ef_env const *default_env_set;
/* default ENV set size, must be initialized by user */
static size_t default_env_set_size = 0;
/* initialize OK flag */
static bool init_ok = false;
/* request a GC check */
static bool gc_request = false;
/* is in recovery check status when first reboot */
static bool in_recovery_check = false;

#ifdef EF_ENV_USING_CACHE
/* ENV cache table */
struct env_cache_node env_cache_table[EF_ENV_CACHE_TABLE_SIZE] = { 0 };
/* sector cache table, it caching the sector info which status is current using */
struct sector_cache_node sector_cache_table[EF_SECTOR_CACHE_TABLE_SIZE] = { 0 };
#endif /* EF_ENV_USING_CACHE */

static size_t set_status(uint8_t status_table[], size_t status_num, size_t status_index)
{
    size_t byte_index = ~0UL;
    /*
     * | write garn |       status0       |       status1       |      status2         |
     * ---------------------------------------------------------------------------------
     * |    1bit    | 0xFF                | 0x7F                |  0x3F                |
     * |    8bit    | 0xFFFF              | 0x00FF              |  0x0000              |
     * |   32bit    | 0xFFFFFFFF FFFFFFFF | 0x00FFFFFF FFFFFFFF |  0x00FFFFFF 00FFFFFF |
     */
    memset(status_table, 0xFF, STATUS_TABLE_SIZE(status_num));
    if (status_index > 0) {
#if (EF_WRITE_GRAN == 1)
        byte_index = (status_index - 1) / 8;
        status_table[byte_index] &= ~(0x80 >> ((status_index - 1) % 8));
#else
        byte_index = (status_index - 1) * (EF_WRITE_GRAN / 8);
        status_table[byte_index] = 0x00;
#endif /* EF_WRITE_GRAN == 1 */
    }

    return byte_index;
}

static size_t get_status(uint8_t status_table[], size_t status_num)
{
    size_t i = 0, status_num_bak = --status_num;

    while (status_num --) {
        /* get the first 0 position from end address to start address */
#if (EF_WRITE_GRAN == 1)
        if ((status_table[status_num / 8] & (0x80 >> (status_num % 8))) == 0x00) {
            break;
        }
#else /*  (EF_WRITE_GRAN == 8) ||  (EF_WRITE_GRAN == 32) ||  (EF_WRITE_GRAN == 64) */
        if (status_table[status_num * EF_WRITE_GRAN / 8] == 0x00) {
            break;
        }
#endif /* EF_WRITE_GRAN == 1 */
        i++;
    }

    return status_num_bak - i;
}
/***
 * @name 写入状态
 * @param addr 实际写入的起始地址
 */
static EfErrCode write_status(uint32_t addr, uint8_t status_table[], size_t status_num, size_t status_index)
{
    EfErrCode result = EF_NO_ERR;
    size_t byte_index;

    EF_ASSERT(status_index < status_num);
    EF_ASSERT(status_table);

    /* set the status first */
    byte_index = set_status(status_table, status_num, status_index);

    /* the first status table value is all 1, so no need to write flash */
    if (byte_index == ~0UL) {
        return EF_NO_ERR;
    }
#if (EF_WRITE_GRAN == 1)
    result = ef_port_write(addr + byte_index, (uint32_t *)&status_table[byte_index], 1);
#else /*  (EF_WRITE_GRAN == 8) ||  (EF_WRITE_GRAN == 32) ||  (EF_WRITE_GRAN == 64) */
    /* write the status by write granularity
     * some flash (like stm32 onchip) NOT supported repeated write before erase */
    result = ef_port_write(addr + byte_index, (uint32_t *) &status_table[byte_index], EF_WRITE_GRAN / 8);
#endif /* EF_WRITE_GRAN == 1 */

    return result;
}

static size_t read_status(uint32_t addr, uint8_t status_table[], size_t total_num)
{
    EF_ASSERT(status_table);

    ef_port_read(addr, (uint32_t *) status_table, STATUS_TABLE_SIZE(total_num));

    return get_status(status_table, total_num);
}

#ifdef EF_ENV_USING_CACHE
/*
 * It's only caching the current using status sector's empty_addr
 */
static void update_sector_cache(uint32_t sec_addr, uint32_t empty_addr)
{
    size_t i, empty_index = EF_SECTOR_CACHE_TABLE_SIZE;

    for (i = 0; i < EF_SECTOR_CACHE_TABLE_SIZE; i++) {
        if ((empty_addr > sec_addr) && (empty_addr < sec_addr + SECTOR_SIZE)) {
            /* update the sector empty_addr in cache */
            if (sector_cache_table[i].addr == sec_addr) {
                sector_cache_table[i].addr = sec_addr;
                sector_cache_table[i].empty_addr = empty_addr;
                return;
            } else if ((sector_cache_table[i].addr == FAILED_ADDR) && (empty_index == EF_SECTOR_CACHE_TABLE_SIZE)) {
                empty_index = i;
            }
        } else if (sector_cache_table[i].addr == sec_addr) {
            /* delete the sector which status is not current using */
            sector_cache_table[i].addr = FAILED_ADDR;
            return;
        }
    }
    /* add the sector empty_addr to cache */
    if (empty_index < EF_SECTOR_CACHE_TABLE_SIZE) {
        sector_cache_table[empty_index].addr = sec_addr;
        sector_cache_table[empty_index].empty_addr = empty_addr;
    }
}

/*
 * Get sector info from cache. It's return true when cache is hit.
 */
static bool get_sector_from_cache(uint32_t sec_addr, uint32_t *empty_addr)
{
    size_t i;

    for (i = 0; i < EF_SECTOR_CACHE_TABLE_SIZE; i++) {
        if (sector_cache_table[i].addr == sec_addr) {
            if (empty_addr) {
                *empty_addr = sector_cache_table[i].empty_addr;
            }
            return true;
        }
    }

    return false;
}

static void update_env_cache(const char *name, size_t name_len, uint32_t addr)
{
    size_t i, empty_index = EF_ENV_CACHE_TABLE_SIZE, min_activity_index = EF_ENV_CACHE_TABLE_SIZE;
    uint16_t name_crc = (uint16_t) (ef_calc_crc32(0, name, name_len) >> 16), min_activity = 0xFFFF;

    for (i = 0; i < EF_ENV_CACHE_TABLE_SIZE; i++) {
        if (addr != FAILED_ADDR) {
            /* update the ENV address in cache */
            if (env_cache_table[i].name_crc == name_crc) {
                env_cache_table[i].addr = addr;
                return;
            } else if ((env_cache_table[i].addr == FAILED_ADDR) && (empty_index == EF_ENV_CACHE_TABLE_SIZE)) {
                empty_index = i;
            } else if (env_cache_table[i].addr != FAILED_ADDR) {
                if (env_cache_table[i].active > 0) {
                    env_cache_table[i].active--;
                }
                if (env_cache_table[i].active < min_activity) {
                    min_activity_index = i;
                    min_activity = env_cache_table[i].active;
                }
            }
        } else if (env_cache_table[i].name_crc == name_crc) {
            /* delete the ENV */
            env_cache_table[i].addr = FAILED_ADDR;
            env_cache_table[i].active = 0;
            return;
        }
    }
    /* add the ENV to cache, using LRU (Least Recently Used) like algorithm */
    if (empty_index < EF_ENV_CACHE_TABLE_SIZE) {
        env_cache_table[empty_index].addr = addr;
        env_cache_table[empty_index].name_crc = name_crc;
        env_cache_table[empty_index].active = 0;
    } else if (min_activity_index < EF_ENV_CACHE_TABLE_SIZE) {
        env_cache_table[min_activity_index].addr = addr;
        env_cache_table[min_activity_index].name_crc = name_crc;
        env_cache_table[min_activity_index].active = 0;
    }
}

/*
 * Get ENV info from cache. It's return true when cache is hit.
 */
static bool get_env_from_cache(const char *name, size_t name_len, uint32_t *addr)
{
    size_t i;
    uint16_t name_crc = (uint16_t) (ef_calc_crc32(0, name, name_len) >> 16);

    for (i = 0; i < EF_ENV_CACHE_TABLE_SIZE; i++) {
        if ((env_cache_table[i].addr != FAILED_ADDR) && (env_cache_table[i].name_crc == name_crc)) {
            char saved_name[EF_ENV_NAME_MAX];
            /* read the ENV name in flash */
            ef_port_read(env_cache_table[i].addr + ENV_HDR_DATA_SIZE, (uint32_t *) saved_name, EF_ENV_NAME_MAX);
            if (!strncmp(name, saved_name, name_len)) {
                *addr = env_cache_table[i].addr;
                if (env_cache_table[i].active >= 0xFFFF - EF_ENV_CACHE_TABLE_SIZE) {
                    env_cache_table[i].active = 0xFFFF;
                } else {
                    env_cache_table[i].active += EF_ENV_CACHE_TABLE_SIZE;
                }
                return true;
            }
        }
    }

    return false;
}
#endif /* EF_ENV_USING_CACHE */

/*
 * find the continue 0xFF flash address to end address
 */
static uint32_t continue_ff_addr(uint32_t start, uint32_t end)
{
    uint8_t buf[32], last_data = 0x00;
    size_t i, addr = start;

    for (; start < end; start += sizeof(buf)) {
        ef_port_read(start, (uint32_t *) buf, sizeof(buf));
        for (i = 0; i < sizeof(buf) && start + i < end; i++) {
            if (last_data != 0xFF && buf[i] == 0xFF) {
                addr = start + i;
            }
            last_data = buf[i];
        }
    }

    if (last_data == 0xFF) {
        return EF_WG_ALIGN(addr);
    } else {
        return end;
    }
}

/*
 * find the next ENV address by magic word on the flash
 */
static uint32_t find_next_env_addr(uint32_t start, uint32_t end)
{
    uint8_t buf[32];
    uint32_t start_bak = start, i;
    uint32_t magic;

#ifdef EF_ENV_USING_CACHE
    uint32_t empty_env;

    if (get_sector_from_cache(EF_ALIGN_DOWN(start, SECTOR_SIZE), &empty_env) && start == empty_env) {
        return FAILED_ADDR;
    }
#endif /* EF_ENV_USING_CACHE */

    for (; start < end; start += (sizeof(buf) - sizeof(uint32_t))) {
        ef_port_read(start, (uint32_t *) buf, sizeof(buf));
        for (i = 0; i < sizeof(buf) - sizeof(uint32_t) && start + i < end; i++) {
            magic = buf[i] + (buf[i + 1] << 8) + (buf[i + 2] << 16) + (buf[i + 3] << 24);
            if (magic == ENV_MAGIC_WORD && (start + i - ENV_MAGIC_OFFSET) >= start_bak) {
                return start + i - ENV_MAGIC_OFFSET;
            }
        }
    }

    return FAILED_ADDR;
}

static uint32_t get_next_env_addr(sector_meta_data_t sector, env_meta_data_t pre_env)
{
    uint32_t addr = FAILED_ADDR;

    if (sector->status.store == SECTOR_STORE_EMPTY) {
        return FAILED_ADDR;
    }

    if (pre_env->addr.start == FAILED_ADDR) {//标致为FAILED_ADDR 证明是第一次搜索
        /* the first ENV address */
        addr = sector->addr + SECTOR_HDR_DATA_SIZE;
    } else {
        if (  (sector->combined == SECTOR_NOT_COMBINED && pre_env->addr.start <= sector->addr + SECTOR_SIZE)
            || (sector->combined != SECTOR_NOT_COMBINED && pre_env->addr.start <= sector->addr + sector->combined * SECTOR_SIZE) ) {//这里的SECTOR_SIZE 都要改成combined的形式
            if (pre_env->crc_is_ok) {
                addr = pre_env->addr.start + pre_env->len;
            } else {
                /* when pre_env CRC check failed, maybe the flash has error data
                 * find_next_env_addr after pre_env address */
                addr = pre_env->addr.start + EF_WG_ALIGN(1);
            }
            /* check and find next ENV address */
            if( sector->combined == SECTOR_NOT_COMBINED)
            {
                addr = find_next_env_addr(addr, sector->addr + SECTOR_SIZE - SECTOR_HDR_DATA_SIZE);//start,end// end为没有combined的尾 // end这里为什么要减去SECTOR_HDR_DATA_SIZE?
                ////ef_print("find_next_env_addr 0x%x , 0x%x\n", addr, sector->addr);
                if (addr > sector->addr + SECTOR_SIZE || pre_env->len == 0) {//next-addr超出了扇区范围
                    return FAILED_ADDR;
                }
            }else{
                addr = find_next_env_addr(addr, sector->addr + sector->combined * SECTOR_SIZE - SECTOR_HDR_DATA_SIZE);//start,end// end为没有combined的尾
                if (addr > sector->addr + sector->combined * SECTOR_SIZE || pre_env->len == 0) {//next-addr超出了扇区范围
                    //TODO 扇区连续模式
                    //跨扇区 所以需要combine?
                    //先考虑不跨扇区的情况 看看能不能得到头
                    ////ef_print("TODO Continuous mode\n");
                    
                    return FAILED_ADDR;
                }
            }

        } else {//扇区中,没有下一个ENV
            /* no ENV */
            return FAILED_ADDR;
        }
    }

    return addr;
}
/**
 * 从env.addr.start中读取env详细数据
 * @param env env指针
 */
static EfErrCode read_env(env_meta_data_t env)
{
    struct env_hdr_data env_hdr;
    uint8_t buf[32];
    uint32_t calc_crc32 = 0, crc_data_len, env_name_addr;
    EfErrCode result = EF_NO_ERR;
    size_t len, size;
    /* read ENV header raw data */
    ef_port_read(env->addr.start, (uint32_t *)&env_hdr, sizeof(struct env_hdr_data));//传入的env指针中包含了env_meta.addr.start,这种env起始地址
    env->status = (env_status_t) get_status(env_hdr.status_table, ENV_STATUS_NUM);
    env->len = env_hdr.len;

    if (env->len == ~0UL || env->len > ENV_AREA_SIZE || env->len < ENV_NAME_LEN_OFFSET) {
        /* the ENV length was not write, so reserved the meta data for current ENV */
        env->len = ENV_HDR_DATA_SIZE;
        if (env->status != ENV_ERR_HDR) {
            env->status = ENV_ERR_HDR;
            EF_DEBUG("Error: The ENV @0x%08X length has an error.\n", env->addr.start);
            write_status(env->addr.start, env_hdr.status_table, ENV_STATUS_NUM, ENV_ERR_HDR);
        }
        env->crc_is_ok = false;
        return EF_READ_ERR;
    } else if (env->len > SECTOR_SIZE - SECTOR_HDR_DATA_SIZE && env->len < ENV_AREA_SIZE) {//ENV长度超出一个扇区的范围,需要跨扇区读取
        //TODO 扇区连续模式，或者写入长度没有写入完整
        
        //EF_ASSERT(0);
    }
    //读取CRC,校验CRC
    /* CRC32 data len(header.name_len + header.value_len + name + value) */
    crc_data_len = env->len - ENV_NAME_LEN_OFFSET;
    /* calculate the CRC32 value */
    for (len = 0, size = 0; len < crc_data_len; len += size) {
        if (len + sizeof(buf) < crc_data_len) {
            size = sizeof(buf);
        } else {
            size = crc_data_len - len;
        }

        ef_port_read(env->addr.start + ENV_NAME_LEN_OFFSET + len, (uint32_t *) buf, EF_WG_ALIGN(size));//32位CRC校验
        calc_crc32 = ef_calc_crc32(calc_crc32, buf, size);
    }
    /* check CRC32 */
    if (calc_crc32 != env_hdr.crc32) {//检测失败
        env->crc_is_ok = false;
        result = EF_READ_ERR;
    } else {//检测成功，可以取出数据
        env->crc_is_ok = true;
        /* the name is behind aligned ENV header */
        env_name_addr = env->addr.start + ENV_HDR_DATA_SIZE;
        ef_port_read(env_name_addr, (uint32_t *) env->name, EF_WG_ALIGN(env_hdr.name_len));
        /* the value is behind aligned name */
        env->addr.value = env_name_addr + EF_WG_ALIGN(env_hdr.name_len);
        env->value_len = env_hdr.value_len;
        env->name_len = env_hdr.name_len;
    }

    return result;
}

/**
 * 读取扇区meta信息
 * @param traversal 是否遍历内部ENV情况,统计信息.
 * @param traversal<false> 只有addr,magic,check_ok,combined,status
 */
static EfErrCode read_sector_meta_data(uint32_t addr, sector_meta_data_t sector, bool traversal)
{//把信息读到sector指针实体中
    EfErrCode result = EF_NO_ERR;
    struct sector_hdr_data sec_hdr;

    EF_ASSERT(addr % SECTOR_SIZE == 0);
    EF_ASSERT(sector);

    /* read sector header raw data */
    ef_port_read(addr, (uint32_t *)&sec_hdr, sizeof(struct sector_hdr_data));//sec_hdr是原始数据

    sector->addr = addr;
    sector->magic = sec_hdr.magic;
    /* check magic word */
    if (sector->magic != SECTOR_MAGIC_WORD) {
        sector->check_ok = false;
        return EF_ENV_INIT_FAILED;
    }
    sector->check_ok = true;
    /* get other sector meta data */
    sector->combined = sec_hdr.combined;
    sector->status.store = (sector_store_status_t) get_status(sec_hdr.status_table.store, SECTOR_STORE_STATUS_NUM);//取状态
    sector->status.dirty = (sector_dirty_status_t) get_status(sec_hdr.status_table.dirty, SECTOR_DIRTY_STATUS_NUM);//取状态
    /* traversal all ENV and calculate the remain space size */
    if (traversal) {
        sector->remain = 0;
        sector->empty_env = sector->addr + SECTOR_HDR_DATA_SIZE;//空置的env地址(临时)
        if (sector->status.store == SECTOR_STORE_EMPTY) {
            if(sector->combined != SECTOR_NOT_COMBINED)
            {
                sector->remain = sector->combined * SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;
            }else{
                sector->remain = SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;//如果状态为空,那么可以直接计算
            }

        } else if (sector->status.store == SECTOR_STORE_USING) {
            struct env_meta_data env_meta;

#ifdef EF_ENV_USING_CACHE
            if (get_sector_from_cache(addr, &sector->empty_env)) {
                sector->remain = SECTOR_SIZE - (sector->empty_env - sector->addr);
                return result;
            }
#endif /* EF_ENV_USING_CACHE */

            //sector->remain = SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;//判断combined
            if(sector->combined != SECTOR_NOT_COMBINED)
            {
                sector->remain = sector->combined * SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;
            }else{
                sector->remain = SECTOR_SIZE - SECTOR_HDR_DATA_SIZE;//如果状态为空,那么可以直接计算
            }
            env_meta.addr.start = FAILED_ADDR;//上一个地址
            while ((env_meta.addr.start = get_next_env_addr(sector, &env_meta)) != FAILED_ADDR) {
                read_env(&env_meta);
                if (!env_meta.crc_is_ok) {
                    if (env_meta.status != ENV_PRE_WRITE && env_meta.status!= ENV_ERR_HDR) {
                        EF_INFO("Error: The ENV (@0x%08X) CRC32 check failed!\n", env_meta.addr.start);
                        sector->remain = 0;
                        result = EF_READ_ERR;
                        break;
                    }
                }
                sector->empty_env += env_meta.len;//空置的ENV地址(就是可以写入的,这是个临时变量)
                sector->remain -= env_meta.len;//扇区剩余空间
            }
            /* check the empty ENV address by read continue 0xFF on flash  */
            {
                uint32_t ff_addr;
                if(sector->combined != SECTOR_NOT_COMBINED)
                {
                    ff_addr = continue_ff_addr(sector->empty_env, sector->addr + sector->combined * SECTOR_SIZE);//终止地址为起始地址+n*sector_size
                }else{
                    ff_addr = continue_ff_addr(sector->empty_env, sector->addr + SECTOR_SIZE);
                }
                
                /* check the flash data is clean */
                if (sector->empty_env != ff_addr) {
                    /* update the sector information */
                    sector->empty_env = ff_addr;//continue 0xFF address
                    if(sector->combined != SECTOR_NOT_COMBINED)
                    {
                        sector->remain = sector->combined * SECTOR_SIZE - (ff_addr - sector->addr);//SECTOR_SIZE -> combined
                    }else{
                        sector->remain = SECTOR_SIZE - (ff_addr - sector->addr);//SECTOR_SIZE -> combined
                    }
                }
            }

#ifdef EF_ENV_USING_CACHE
            update_sector_cache(sector->addr, sector->empty_env);
#endif
        }
    }

    return result;
}

static uint32_t get_next_sector_addr(sector_meta_data_t pre_sec)
{
    uint32_t next_addr;

    if (pre_sec->addr == FAILED_ADDR) {
        return env_start_addr;
    } else {
        /* check ENV sector combined */
        if (pre_sec->combined == SECTOR_NOT_COMBINED) {
            next_addr = pre_sec->addr + SECTOR_SIZE;
        } else {
            next_addr = pre_sec->addr + pre_sec->combined * SECTOR_SIZE;//把combined+1个扇区合成一个扇区
        }
        /* check range */
        if (next_addr < env_start_addr + ENV_AREA_SIZE) {
            return next_addr;
        } else {
            /* no sector */
            return FAILED_ADDR;
        }
    }
}

static void env_iterator(env_meta_data_t env, void *arg1, void *arg2,
        bool (*callback)(env_meta_data_t env, void *arg1, void *arg2))
{
    struct sector_meta_data sector;
    uint32_t sec_addr;

    sector.addr = FAILED_ADDR;
    /* search all sectors */
    while ((sec_addr = get_next_sector_addr(&sector)) != FAILED_ADDR) {
        if (read_sector_meta_data(sec_addr, &sector, false) != EF_NO_ERR) {
            continue;
        }
        if (callback == NULL) {
            continue;
        }
        /* sector has ENV */
        if (sector.status.store == SECTOR_STORE_USING || sector.status.store == SECTOR_STORE_FULL) {
            env->addr.start = FAILED_ADDR;
            /* search all ENV */
            while ((env->addr.start = get_next_env_addr(&sector, env)) != FAILED_ADDR) {
                read_env(env);
                /* iterator is interrupted when callback return true */
                if (callback(env, arg1, arg2)) {
                    return;
                }
            }
        }
    }
}

static bool find_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    const char *key = arg1;
    bool *find_ok = arg2;
    uint8_t max_len = strlen(key);

    if (max_len < env->name_len) {
        max_len = env->name_len;
    }
    /* check ENV */
    if (env->crc_is_ok && env->status == ENV_WRITE && !strncmp(env->name, key, max_len)) {
        *find_ok = true;
        return true;
    }
    return false;
}

static bool find_env_no_cache(const char *key, env_meta_data_t env)
{
    bool find_ok = false;

    env_iterator(env, (void *)key, &find_ok, find_env_cb);

    return find_ok;
}
//static
bool find_env(const char *key, env_meta_data_t env)
{
    bool find_ok = false;

#ifdef EF_ENV_USING_CACHE
    size_t key_len = strlen(key);

    if (get_env_from_cache(key, key_len, &env->addr.start)) {
        read_env(env);
        return true;
    }
#endif /* EF_ENV_USING_CACHE */

    find_ok = find_env_no_cache(key, env);

#ifdef EF_ENV_USING_CACHE
    if (find_ok) {
        update_env_cache(key, key_len, env->addr.start);
    }
#endif /* EF_ENV_USING_CACHE */

    return find_ok;
}

static bool ef_is_str(uint8_t *value, size_t len)
{
#define __is_print(ch)       ((unsigned int)((ch) - ' ') < 127u - ' ')
    size_t i;

    for (i = 0; i < len; i++) {
        if (!__is_print(value[i])) {
            return false;
        }
    }
    return true;
}

static size_t get_env(const char *key, void *value_buf, size_t buf_len, size_t *value_len)
{
    struct env_meta_data env;
    size_t read_len = 0;

    if (find_env(key, &env)) {
        if (value_len) {
            *value_len = env.value_len;
        }
        if (buf_len > env.value_len) {
            read_len = env.value_len;
        } else {
            read_len = buf_len;
        }
        ef_port_read(env.addr.value, (uint32_t *) value_buf, read_len);
    }

    return read_len;
}

/**
 * Get a blob ENV value by key name.
 *
 * @param key ENV name
 * @param value_buf ENV blob buffer
 * @param buf_len ENV blob buffer length
 * @param saved_value_len return the length of the value saved on the flash, 0: NOT found
 *
 * @return the actually get size on successful
 */
size_t ef_get_env_blob(const char *key, void *value_buf, size_t buf_len, size_t *saved_value_len)
{
    size_t read_len = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return 0;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    read_len = get_env(key, value_buf, buf_len, saved_value_len);

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return read_len;
}

/**
 * Get an ENV value by key name.
 *
 * @note this function is NOT supported reentrant
 * @note this function is DEPRECATED
 *
 * @param key ENV name
 *
 * @return value
 */
char *ef_get_env(const char *key)
{
    static char value[EF_STR_ENV_VALUE_MAX_SIZE + 1];
    size_t get_size;

    if ((get_size = ef_get_env_blob(key, value, EF_STR_ENV_VALUE_MAX_SIZE, NULL)) > 0) {
        /* the return value must be string */
        if (ef_is_str((uint8_t *)value, get_size)) {
            value[get_size] = '\0';
            return value;
        } else {
            EF_INFO("Warning: The ENV value isn't string. Could not be returned\n");
            return NULL;
        }
    }

    return NULL;
}
/***
 * @param addr 实际写入的地址
 * @param env_hdr 写入的参数指针
 */
static EfErrCode write_env_hdr(uint32_t addr, env_hdr_data_t env_hdr) {
    EfErrCode result = EF_NO_ERR;
    /* write the status will by write granularity */
    result = write_status(addr, env_hdr->status_table, ENV_STATUS_NUM, ENV_PRE_WRITE);
    if (result != EF_NO_ERR) {
        return result;
    }
    /* write other header data */
    result = ef_port_write(addr + ENV_MAGIC_OFFSET, &env_hdr->magic, sizeof(struct env_hdr_data) - ENV_MAGIC_OFFSET);

    return result;
}

static EfErrCode format_sector(uint32_t addr, uint32_t combined_value)
{
    EfErrCode result = EF_NO_ERR;
    struct sector_hdr_data sec_hdr;

    EF_ASSERT(addr % SECTOR_SIZE == 0);

    result = ef_port_erase(addr, SECTOR_SIZE);
    if (result == EF_NO_ERR) {
        /* initialize the header data */
        memset(&sec_hdr, 0xFF, sizeof(struct sector_hdr_data));
        set_status(sec_hdr.status_table.store, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_EMPTY);//status1
        set_status(sec_hdr.status_table.dirty, SECTOR_DIRTY_STATUS_NUM, SECTOR_DIRTY_FALSE);
        sec_hdr.magic = SECTOR_MAGIC_WORD;
        sec_hdr.combined = combined_value;
        sec_hdr.reserved = 0xFFFFFFFF;
        /* save the header */
        result = ef_port_write(addr, (uint32_t *)&sec_hdr, sizeof(struct sector_hdr_data));

#ifdef EF_ENV_USING_CACHE
        /* delete the sector cache */
        update_sector_cache(addr, addr + SECTOR_SIZE);
#endif /* EF_ENV_USING_CACHE */
    }

    return result;
}
//增加的函数
static EfErrCode erase_sector(uint32_t addr)
{
    EF_ASSERT(addr % SECTOR_SIZE == 0);

    return ef_port_erase( addr, SECTOR_SIZE);
}


/***
 * @name 更新扇区状态
 * @param sector 实际更新的扇区
 */
static EfErrCode update_sec_status(sector_meta_data_t sector, size_t new_env_len, bool *is_full)
{
    uint8_t status_table[STORE_STATUS_TABLE_SIZE];
    EfErrCode result = EF_NO_ERR;
    /* change the current sector status */
    if (sector->status.store == SECTOR_STORE_EMPTY) {
        /* change the sector status to using */
        result = write_status(sector->addr, status_table, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_USING);
    } else if (sector->status.store == SECTOR_STORE_USING) {
        /* check remain size */
        if (sector->remain < EF_SEC_REMAIN_THRESHOLD || sector->remain - new_env_len < EF_SEC_REMAIN_THRESHOLD) {
            /* change the sector status to full */
            result = write_status(sector->addr, status_table, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_FULL);

#ifdef EF_ENV_USING_CACHE
            /* delete the sector cache */
            update_sector_cache(sector->addr, sector->addr + SECTOR_SIZE);
#endif /* EF_ENV_USING_CACHE */

            if (is_full) {
                *is_full = true;
            }
        } else if (is_full) {
            *is_full = false;
        }
    }

    return result;
}

static void sector_iterator(sector_meta_data_t sector, sector_store_status_t status, void *arg1, void *arg2,
        bool (*callback)(sector_meta_data_t sector, void *arg1, void *arg2), bool traversal_env) {
    uint32_t sec_addr;

    /* search all sectors */
    sector->addr = FAILED_ADDR;
    while ((sec_addr = get_next_sector_addr(sector)) != FAILED_ADDR) {
        read_sector_meta_data(sec_addr, sector, false);//读扇区meta信息
        if (status == SECTOR_STORE_UNUSED || status == sector->status.store) {
            if (traversal_env) {//traversal遍历
                read_sector_meta_data(sec_addr, sector, traversal_env);//函数内含连续模式的东西,sector是传入指针，结果反映在sector里
            }
            /* iterator is interrupted when callback return true */
            if (callback && callback(sector, arg1, arg2)) {
                return;
            }
        }
    }
}

static bool sector_statistics_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    size_t *empty_sector = arg1, *using_sector = arg2;

    if (sector->check_ok && sector->status.store == SECTOR_STORE_EMPTY) {
        (*empty_sector)++;
    } else if (sector->check_ok && sector->status.store == SECTOR_STORE_USING) {
        (*using_sector)++;
    }

    return false;
}

static bool alloc_env_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    size_t *env_size = arg1;
    uint32_t *empty_env = arg2;

    /* 1. sector has space
     * 2. the NO dirty sector
     * 3. the dirty sector only when the gc_request is false */
    if (sector->check_ok && sector->remain > *env_size
            && ((sector->status.dirty == SECTOR_DIRTY_FALSE)
                    || (sector->status.dirty == SECTOR_DIRTY_TRUE && !gc_request))) {
        *empty_env = sector->empty_env;
        return true;//return true will interrrupt iterator
    }

    return false;
}

//新增回调函数 改动[删除!&& sector->remain > *env_size]
static bool alloc_cont_env_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    size_t *env_size = arg1;
    uint32_t *empty_env = arg2;

    /* 1. sector has space
     * 2. the NO dirty sector
     * 3. the dirty sector only when the gc_request is false */
    if (sector->check_ok //&& sector->remain > *env_size
            && ((sector->status.dirty == SECTOR_DIRTY_FALSE)
                    || (sector->status.dirty == SECTOR_DIRTY_TRUE && !gc_request))) {
        *empty_env = sector->empty_env;
        return true;
    }

    return false;
}
/**
 * 新增 连续扇区测试程序
 * @name 连续扇区测试
 */
static void sector_wander()
{
    struct sector_meta_data sector;
    uint32_t sec_addr;
    size_t i = 0;
    sector.addr = FAILED_ADDR;
    //ef_port_env_lock();
    ef_print("========================================\n");

    while ((sec_addr = get_next_sector_addr(&sector)) != FAILED_ADDR) {
        i++;
        read_sector_meta_data(sec_addr, &sector, true);//读扇区meta信息
        ef_print("%d\t[%c]\t", sector.remain, sector.status.store == SECTOR_STORE_EMPTY ? 'E' : (sector.status.store == SECTOR_STORE_FULL ? 'F' : 'O') );//如果输出0可能是sector的状态为Full (有阈值可以来调整)
        //遍历sector里面的env
        if(i % 3 == 0)
            ef_print("\n");
    }
    ef_print("\n========================================\n");
    //ef_port_env_unlock();
}
/**
 * 寻找连续的扇区
 * @param sector 返回值
 * @param env_size 寻找的扇区大小
 * @bugFix 20200717修复首扇区问题
 */
static uint32_t contsector_wander_find(sector_meta_data_t sector, size_t env_size)
{
    uint32_t sec_addr,first_sec_addr = FAILED_ADDR,last_sec_addr = FAILED_ADDR;
    int contCnt = 0;
    int i = 0;
    int env_size_temp = (int)env_size;
    EF_ASSERT(env_size > SECTOR_SIZE - SECTOR_HDR_DATA_SIZE);

    ef_print("FIND %d size\n", env_size_temp);


    while ((sec_addr = get_next_sector_addr(sector)) != FAILED_ADDR) {
        read_sector_meta_data(sec_addr, sector, true);//读扇区meta信息
        if(sector->check_ok == true)
        {
            //需要解决首sector为empty的情况
            //条件还需要改造.是combined的也可以存储.
            if(sector->status.store == SECTOR_STORE_EMPTY && sector->combined == SECTOR_NOT_COMBINED)
            {
                //记录第一个位置的addr
                // if(env_size_temp == env_size)
                //     first_sec_addr = sec_addr;
                // if(first_sec_addr == FAILED_ADDR)
                //     first_sec_addr = sec_addr;
                env_size_temp -= SECTOR_SIZE;

                //不转换成Int型无法比较..
                //TODO:最好的办法在计算之前加上,减少类型转换
                if(env_size_temp < -(int)SECTOR_HDR_DATA_SIZE)
                {
                    /* 判断首扇区是否为FAIL_ADDR */
                    if(first_sec_addr == FAILED_ADDR)
                        first_sec_addr = 0;
                    ef_print("[%d] find okk, [0x%x,0x%x]\n", i, first_sec_addr, sec_addr);
                    break;
                }
            }else{
                if(sector->combined == SECTOR_NOT_COMBINED)
                    first_sec_addr = sec_addr;//记录为..
                else
                    first_sec_addr = sec_addr + sector->combined;//后面用是否整除判断是否那啥
                
                env_size_temp = (int)env_size;
            }
        }

        i++;
    }

    //这种情况可能无法完成存储任务..
    if(env_size_temp > - (int)SECTOR_HDR_DATA_SIZE)//env_size_temp > SECTOR_SIZE - SECTOR_HDR_DATA_SIZE
    {
        ef_print("No enough space to save ENV&HDR(%dbyte)", env_size);
        return FAILED_ADDR;
    }
    /* 判断首扇区是否是合并的扇区(合并的扇区暂时跳过,因需要改造combined结构) 算出来了first_sec_addr说明后面还有扇区?*/
    if(first_sec_addr % SECTOR_SIZE > 0)
        first_sec_addr = first_sec_addr - (first_sec_addr % SECTOR_SIZE) + (first_sec_addr % SECTOR_SIZE) * SECTOR_SIZE + SECTOR_HDR_DATA_SIZE;
    else{
        //首sec不是combined的.那么可能有空间可以利用..查找这个空间
        read_sector_meta_data(first_sec_addr, sector, true);//读扇区meta信息
        if(sector->remain >= env_size_temp + SECTOR_SIZE)
        {
            //这个SECTOR大小够用,不需要再往后多占一个,返回当前空地址
            first_sec_addr = sector->empty_env;

        }else{
            //大小不够..需要往后占用.那么就是EMPTY扇区的empty_env.也就是偏移
            first_sec_addr = sector->addr + SECTOR_SIZE + SECTOR_HDR_DATA_SIZE;
        }
    }
    ef_print("[%d] find final, [0x%x,0x%x]\n", i, first_sec_addr, sec_addr);
    return first_sec_addr;//返回值是empty_env地址..
    
    

}

/***
 * @name 给ENV分配内存
 * @param sector 扇区指针
 * @param env_size 分配类型
 * @return 可以写入的空EMPTY_ENV地址
 */
static uint32_t alloc_env(sector_meta_data_t sector, size_t env_size)
{
    uint32_t empty_env = FAILED_ADDR;
    size_t empty_sector = 0, using_sector = 0;

    /* sector status statistics */
    sector_iterator(sector, SECTOR_STORE_UNUSED, &empty_sector, &using_sector, sector_statistics_cb, false);
    if (using_sector > 0) {
        /* alloc the ENV from the using status sector first */
        sector_iterator(sector, SECTOR_STORE_USING, &env_size, &empty_env, alloc_env_cb, true);//env_size参数的作用是限制查找using-sector的条件
        //从USING状态的sector中寻找合适大小
        //那么可能有的combined的sector是EMPTY,会被忽略
    }
    if (empty_sector > 0 && empty_env == FAILED_ADDR) {//上面没有找到合适的empty_env，此时还有empty_sector
    
        //默认不需要合并,在全部空闲扇区中查找合适的
        if (empty_sector > EF_GC_EMPTY_SEC_THRESHOLD || gc_request) {//empty_sector数量大于阈值,阈值sector是留着给gc的
            sector_iterator(sector, SECTOR_STORE_EMPTY, &env_size, &empty_env, alloc_env_cb, true);//[env_size] is alloced param size
            //如果ENV大于sector_size，需要执行合并操作..
            if(empty_env != FAILED_ADDR)
            {
                ef_print("empty_sector: ENV 0x%x\n", empty_env);
            }
        } else {
            //空间不足,所以需要执行gc
            /* no space for new ENV now will GC and retry */
            EF_DEBUG("Trigger a GC check after alloc ENV failed.\n");
            gc_request = true;
        }

        //正常的里面没找到，那么尝试合并解决问题
        if(empty_env == FAILED_ADDR)
        {

            //需要连续扇区的大小
            uint32_t contSectorSize = env_size / SECTOR_SIZE + 1;

            //查找连续扇区

            //env_size比较大的情况,那么需要跨区!查找连续扇区
            //这个过程应该弄成缓存,在空闲的时候执行
            EF_INFO("env_size %d Too Big\nNEED Cont-Sector<%d> to Write\n", env_size, contSectorSize);//edit:add
            sector_wander();
            sector->addr = FAILED_ADDR;
            //没找到
            if( (empty_env = contsector_wander_find(sector, env_size)) == FAILED_ADDR)
                return FAILED_ADDR;
            //非FAILED_ADDR
            //执行更改combined参数
            //更改combined参数
            uint32_t sector_addr = EF_ALIGN_DOWN(empty_env, SECTOR_SIZE);//(empty_env >> 12) << 12;
            EF_INFO("SECTOR START ADDR 0x%x\n", sector_addr);//EF_ALIGN_DOWN(old_env->addr.start, SECTOR_SIZE) + SECTOR_DIRTY_OFFSET
            
            //sector->status
            //ef_port_write(sector_addr + SECTOR_COMBINED_OFFSET,);
            uint32_t buf = 0;
            EfErrCode result;
            ef_port_read(sector_addr + SECTOR_COMBINED_OFFSET, &buf, 4);
            ef_print("Test Read[0x%x] 0x%x\n", sector_addr + SECTOR_COMBINED_OFFSET, buf);

            //擦除后面几个…
            //ANSWER:不擦除出问题了..暂时擦除,后面再判断不擦除的作用
            //TODO:修改,怎么样合理的使擦除次数变少呢(延长硬件使用寿命)
            {
                //从后一个扇区开始擦除.无论什么情况，第一个扇区应该都不需要擦除的,只需要修改combined(BUG Fix)
                int eraseI = 1;
                result = EF_NO_ERR;
                for(; eraseI < contSectorSize && result == EF_NO_ERR; eraseI++)
                {
                    result = erase_sector(sector_addr + eraseI * SECTOR_SIZE);
                }
                if(eraseI < contSectorSize)
                {
                    ef_print("ALLOC erase sector 0x%x error\n",sector_addr + eraseI * SECTOR_SIZE - SECTOR_SIZE);//这里的变量可能需要斟酌一下
                    return FAILED_ADDR;
                }
            }
            //改变sector
            read_sector_meta_data(sector_addr, sector, true);
            result = ef_port_write(sector_addr + SECTOR_COMBINED_OFFSET, &contSectorSize, 4);
            if(result != EF_NO_ERR)
            {
                ef_print("Change sector CONBINED error\n");
                return FAILED_ADDR;
            }
            
            //return empty_env;
        }


    }
    //返回的得是.空empty地址.不是别的!
    EF_INFO("empty_env 0x%x\n", empty_env);//edit:add
    return empty_env;
}
/***
 * 假删除变量
 * @param complete_del 是否完全删除(预删除/已删除) 区分不同状态,防止数据丢失
 */
static EfErrCode del_env(const char *key, env_meta_data_t old_env, bool complete_del) {
    EfErrCode result = EF_NO_ERR;
    uint32_t dirty_status_addr;
    static bool last_is_complete_del = false;

#if (ENV_STATUS_TABLE_SIZE >= DIRTY_STATUS_TABLE_SIZE)
    uint8_t status_table[ENV_STATUS_TABLE_SIZE];
#else
    uint8_t status_table[DIRTY_STATUS_TABLE_SIZE];
#endif

    /* need find ENV */
    if (!old_env) {
        struct env_meta_data env;
        /* find ENV */
        if (find_env(key, &env)) {
            old_env = &env;
        } else {
            EF_DEBUG("Not found '%s' in ENV.\n", key);
            return EF_ENV_NAME_ERR;
        }
    }
    /* change and save the new status */
    if (!complete_del) {
        result = write_status(old_env->addr.start, status_table, ENV_STATUS_NUM, ENV_PRE_DELETE);
        last_is_complete_del = true;
    } else {
        result = write_status(old_env->addr.start, status_table, ENV_STATUS_NUM, ENV_DELETED);

        if (!last_is_complete_del && result == EF_NO_ERR) {
#ifdef EF_ENV_USING_CACHE
            /* only delete the ENV in flash and cache when only using del_env(key, env, true) in ef_del_env() */
            update_env_cache(key, strlen(key), FAILED_ADDR);
#endif /* EF_ENV_USING_CACHE */
        }

        last_is_complete_del = false;
    }

    dirty_status_addr = EF_ALIGN_DOWN(old_env->addr.start, SECTOR_SIZE) + SECTOR_DIRTY_OFFSET;//计算dirty_status的地址
    /* read and change the sector dirty status */
    if (result == EF_NO_ERR
            && read_status(dirty_status_addr, status_table, SECTOR_DIRTY_STATUS_NUM) == SECTOR_DIRTY_FALSE) {
        result = write_status(dirty_status_addr, status_table, SECTOR_DIRTY_STATUS_NUM, SECTOR_DIRTY_TRUE);//把扇区状态改成已脏
    }

    return result;
}

/**
 * move the ENV to new space
 * 搬运变量,搬哪里呢
 * 基本都满了才执行clean,那么搬运肯定是往阈值扇区里面搬了,阈值扇区满了那么也应该有空闲了
 * @param env 需要搬运的变量结构体
 */
static EfErrCode move_env(env_meta_data_t env)
{
    EfErrCode result = EF_NO_ERR;
    uint8_t status_table[ENV_STATUS_TABLE_SIZE];
    uint32_t env_addr;
    struct sector_meta_data sector;

    /* prepare to delete the current ENV */
    if (env->status == ENV_WRITE) {
        del_env(NULL, env, false);//预删除
    }

    if ((env_addr = alloc_env(&sector, env->len)) != FAILED_ADDR) {
        if (in_recovery_check) {
            struct env_meta_data env_bak;
            char name[EF_ENV_NAME_MAX + 1] = { 0 };
            strncpy(name, env->name, env->name_len);
            /* check the ENV in flash is already create success */
            if (find_env_no_cache(name, &env_bak)) {
                /* already create success, don't need to duplicate */
                result = EF_NO_ERR;
                goto __exit;
            }
        }
    } else {
        return EF_ENV_FULL;
    }
    /* start move the ENV */
    {
        uint8_t buf[32];
        size_t len, size, env_len = env->len;

        /* update the new ENV sector status first */
        update_sec_status(&sector, env->len, NULL);

        write_status(env_addr, status_table, ENV_STATUS_NUM, ENV_PRE_WRITE);
        env_len -= ENV_MAGIC_OFFSET;
        for (len = 0, size = 0; len < env_len; len += size) {
            if (len + sizeof(buf) < env_len) {
                size = sizeof(buf);
            } else {
                size = env_len - len;
            }
            ef_port_read(env->addr.start + ENV_MAGIC_OFFSET + len, (uint32_t *) buf, EF_WG_ALIGN(size));
            result = ef_port_write(env_addr + ENV_MAGIC_OFFSET + len, (uint32_t *) buf, size);
        }
        write_status(env_addr, status_table, ENV_STATUS_NUM, ENV_WRITE);

#ifdef EF_ENV_USING_CACHE
        update_sector_cache(EF_ALIGN_DOWN(env_addr, SECTOR_SIZE),
                env_addr + ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env->name_len) + EF_WG_ALIGN(env->value_len));
        update_env_cache(env->name, env->name_len, env_addr);
#endif /* EF_ENV_USING_CACHE */
    }

    EF_DEBUG("Moved the ENV (%.*s) from 0x%08X to 0x%08X.\n", env->name_len, env->name, env->addr.start, env_addr);

__exit:
    del_env(NULL, env, true);

    return result;
}


/***
 * @return empty_env
 */
static uint32_t new_env(sector_meta_data_t sector, size_t env_size)
{//env_hdr.len = ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env_hdr.name_len) + EF_WG_ALIGN(env_hdr.value_len);
    bool already_gc = false;
    uint32_t empty_env = FAILED_ADDR;

__retry:
    //申明环境变量
    if ((empty_env = alloc_env(sector, env_size)) == FAILED_ADDR && gc_request && !already_gc) {
        EF_DEBUG("Warning: Alloc an ENV (size %d) failed when new ENV. Now will GC then retry.\n", env_size);
        gc_collect();
        already_gc = true;
        goto __retry;
    }

    return empty_env;
}

static uint32_t new_env_by_kv(sector_meta_data_t sector, size_t key_len, size_t buf_len)
{
    size_t env_len = ENV_HDR_DATA_SIZE + EF_WG_ALIGN(key_len) + EF_WG_ALIGN(buf_len);

    return new_env(sector, env_len);
}

static bool gc_check_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    size_t *empty_sec = arg1;

    if (sector->check_ok) {
        *empty_sec = *empty_sec + 1;
    }

    return false;

}
/**
 * 执行清理callback
 * @param sector 需要清理的sector
 */
static bool do_gc(sector_meta_data_t sector, void *arg1, void *arg2)
{
    struct env_meta_data env;

    if (sector->check_ok && (sector->status.dirty == SECTOR_DIRTY_TRUE || sector->status.dirty == SECTOR_DIRTY_GC)) {
        uint8_t status_table[DIRTY_STATUS_TABLE_SIZE];
        /* change the sector status to GC */
        write_status(sector->addr + SECTOR_DIRTY_OFFSET, status_table, SECTOR_DIRTY_STATUS_NUM, SECTOR_DIRTY_GC);
        /* search all ENV */
        env.addr.start = FAILED_ADDR;
        while ((env.addr.start = get_next_env_addr(sector, &env)) != FAILED_ADDR) {
            read_env(&env);
            if (env.crc_is_ok && (env.status == ENV_WRITE || env.status == ENV_PRE_DELETE)) {
                /* move the ENV to new space */
                if (move_env(&env) != EF_NO_ERR) {
                    EF_DEBUG("Error: Moved the ENV (%.*s) for GC failed.\n", env.name_len, env.name);
                }
            }
        }
        format_sector(sector->addr, SECTOR_NOT_COMBINED);
        EF_DEBUG("Collect a sector @0x%08X\n", sector->addr);
    }

    return false;
}

/**
 * 辣鸡清理程序
 * The GC will be triggered on the following scene:
 * 1. alloc an ENV when the flash not has enough space
 * 2. write an ENV then the flash not has enough space
 */
static void gc_collect(void)
{
    struct sector_meta_data sector;
    size_t empty_sec = 0;

    /* GC check the empty sector number */
    //寻找空的扇区(考虑到combined的话修改方案)
    sector_iterator(&sector, SECTOR_STORE_EMPTY, &empty_sec, NULL, gc_check_cb, false);

    /* do GC collect */
    EF_DEBUG("The remain empty sector is %d, GC threshold is %d.\n", empty_sec, EF_GC_EMPTY_SEC_THRESHOLD);//empty_sec 空的扇区个数
    if (empty_sec <= EF_GC_EMPTY_SEC_THRESHOLD) {
        sector_iterator(&sector, SECTOR_STORE_UNUSED, NULL, NULL, do_gc, false);//不遍历内部内容,只执行回调函数
    }

    gc_request = false;
}

static EfErrCode align_write(uint32_t addr, const uint32_t *buf, size_t size)
{
    EfErrCode result = EF_NO_ERR;
    size_t align_remain;

#if (EF_WRITE_GRAN / 8 > 0)
    uint8_t align_data[EF_WRITE_GRAN / 8];
    size_t align_data_size = sizeof(align_data);
#else
    /* For compatibility with C89 */
    uint8_t align_data_u8, *align_data = &align_data_u8;
    size_t align_data_size = 1;
#endif

    memset(align_data, 0xFF, align_data_size);
    result = ef_port_write(addr, buf, EF_WG_ALIGN_DOWN(size));

    align_remain = size - EF_WG_ALIGN_DOWN(size);
    if (result == EF_NO_ERR && align_remain) {
        memcpy(align_data, (uint8_t *)buf + EF_WG_ALIGN_DOWN(size), align_remain);
        result = ef_port_write(addr + EF_WG_ALIGN_DOWN(size), (uint32_t *) align_data, align_data_size);
    }

    return result;
}
/***
 * @name 创建BLOB变量
 * @param sector 输入的扇区信息
 * @return EF错误代码
 */
static EfErrCode create_env_blob(sector_meta_data_t sector, const char *key, const void *value, size_t len)
{
    EfErrCode result = EF_NO_ERR;
    struct env_hdr_data env_hdr;
    bool is_full = false;
    uint32_t env_addr = sector->empty_env;

    if (strlen(key) > EF_ENV_NAME_MAX) {
        EF_INFO("Error: The ENV name length is more than %d\n", EF_ENV_NAME_MAX);
        return EF_ENV_NAME_ERR;
    }
    EF_INFO("Create_env_blob %s\n",key);//edit:新增
    memset(&env_hdr, 0xFF, sizeof(struct env_hdr_data));
    env_hdr.magic = ENV_MAGIC_WORD;
    env_hdr.name_len = strlen(key);//名字长度
    env_hdr.value_len = len;//blob值，的长度
    //env_hdr 一个变量的header
    env_hdr.len = ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env_hdr.name_len) + EF_WG_ALIGN(env_hdr.value_len);
    //这里限制了不允许连续写入 如果改写连续写入模式,需要修改
    //这里是说.需要写入的ENV > 扇区

    // if (env_hdr.len > SECTOR_SIZE - SECTOR_HDR_DATA_SIZE) {
    //     EF_INFO("Error: The ENV size is too big\n");
    //     return EF_ENV_FULL;
    // }

    if (env_addr != FAILED_ADDR || (env_addr = new_env(sector, env_hdr.len)) != FAILED_ADDR) {//如果 new_env成功,sector就已经和env_addr配对
        size_t align_remain;
        ef_print("BLOB WRITE addr 0x%x\n", env_addr);
        /* update the sector status */
        if (result == EF_NO_ERR) {
            result = update_sec_status(sector, env_hdr.len, &is_full);
        }
        if (result == EF_NO_ERR) {
            uint8_t ff = 0xFF;
            /* start calculate CRC32 */
            env_hdr.crc32 = ef_calc_crc32(0, &env_hdr.name_len, ENV_HDR_DATA_SIZE - ENV_NAME_LEN_OFFSET);
            env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, key, env_hdr.name_len);
            align_remain = EF_WG_ALIGN(env_hdr.name_len) - env_hdr.name_len;
            while (align_remain--) {
                env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, &ff, 1);
            }
            env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, value, env_hdr.value_len);
            align_remain = EF_WG_ALIGN(env_hdr.value_len) - env_hdr.value_len;
            while (align_remain--) {
                env_hdr.crc32 = ef_calc_crc32(env_hdr.crc32, &ff, 1);
            }
            /* write ENV header data */
            result = write_env_hdr(env_addr, &env_hdr);//把CRC32校验值计算出来后,写入HDR中..函数中有把ENV区块状态变成准备写入.

        }
        /* write key name */
        if (result == EF_NO_ERR) {
            result = align_write(env_addr + ENV_HDR_DATA_SIZE, (uint32_t *) key, env_hdr.name_len);//实际写入key-name

#ifdef EF_ENV_USING_CACHE
            if (!is_full) {
                update_sector_cache(sector->addr,
                        env_addr + ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env_hdr.name_len) + EF_WG_ALIGN(env_hdr.value_len));
            }
            update_env_cache(key, env_hdr.name_len, env_addr);
#endif /* EF_ENV_USING_CACHE */
        }
        /* write value */
        if (result == EF_NO_ERR) {
            result = align_write(env_addr + ENV_HDR_DATA_SIZE + EF_WG_ALIGN(env_hdr.name_len), value,
                    env_hdr.value_len);
        }
        /* change the ENV status to ENV_WRITE */
        if (result == EF_NO_ERR) {
            result = write_status(env_addr, env_hdr.status_table, ENV_STATUS_NUM, ENV_WRITE);
        }
        /* trigger GC collect when current sector is full */
        if (result == EF_NO_ERR && is_full) {
            EF_DEBUG("Trigger a GC check after created ENV.\n");
            gc_request = true;
        }
    } else {
        result = EF_ENV_FULL;
    }

    return result;
}

/**
 * Delete an ENV.
 *
 * @param key ENV name
 *
 * @return result
 */
EfErrCode ef_del_env(const char *key)
{
    EfErrCode result = EF_NO_ERR;

    if (!init_ok) {
        EF_INFO("Error: ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    result = del_env(key, NULL, true);

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

/**
 * The same to ef_del_env on this mode
 * It's compatibility with older versions (less then V4.0).
 *
 * @note this function is DEPRECATED
 *
 * @param key ENV name
 *
 * @return result
 */
EfErrCode ef_del_and_save_env(const char *key)
{
    return ef_del_env(key);
}

static EfErrCode set_env(const char *key, const void *value_buf, size_t buf_len)
{
    EfErrCode result = EF_NO_ERR;
    static struct env_meta_data env;
    static struct sector_meta_data sector;
    bool env_is_found = false;

    if (value_buf == NULL) {
        result = del_env(key, NULL, true);
    } else {
        /* make sure the flash has enough space */
        if (new_env_by_kv(&sector, strlen(key), buf_len) == FAILED_ADDR) {//sector有关键性作用
            EF_INFO("EF_ENV_FULL\n");
            return EF_ENV_FULL;
        }
        env_is_found = find_env(key, &env);
        /* prepare to delete the old ENV */
        if (env_is_found) {
            result = del_env(key, &env, false);//预删除
        }
        /* create the new ENV */
        if (result == EF_NO_ERR) {
            result = create_env_blob(&sector, key, value_buf, buf_len);
        }
        /* delete the old ENV */
        if (env_is_found && result == EF_NO_ERR) {
            result = del_env(key, &env, true);//完全删除
        }
        /* process the GC after set ENV */
        if (gc_request) {
            gc_collect();
        }
    }

    return result;
}

/**
 * Set a blob ENV. If it value is NULL, delete it.
 * If not find it in flash, then create it.
 *
 * @param key ENV name
 * @param value ENV value
 * @param len ENV value length
 *
 * @return result
 */
EfErrCode ef_set_env_blob(const char *key, const void *value_buf, size_t buf_len)
{
    EfErrCode result = EF_NO_ERR;


    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return EF_ENV_INIT_FAILED;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    result = set_env(key, value_buf, buf_len);

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

//新增 
void ef_bin_update_sector_combined(int sectorID)
{
    struct sector_meta_data sector;
    sector.addr = FAILED_ADDR;
    uint32_t sec_addr;
    int cnt = 0;
    //EfErrCode result = EF_NO_ERR;
    //uint8_t status_table[STORE_STATUS_TABLE_SIZE];

    //sector.combined
    ef_port_env_lock();

    ef_print("Hello sector Combined!\n");


    while ((sec_addr = get_next_sector_addr(&sector)) != FAILED_ADDR) {
        ef_print("Looking SECTOR %d. %d\n", sectorID, cnt);
        if (read_sector_meta_data(sec_addr, &sector, false) != EF_NO_ERR) {
            continue;
        }
        if(cnt++ != sectorID)
            continue;
        format_sector(sector.addr, 2);
        erase_sector(sector.addr + SECTOR_SIZE);
        // //判断逻辑下一个扇区无ENV..否则改成combined失败
        // if (result == EF_NO_ERR) {
        //     result = write_status(sector.addr, status_table, SECTOR_STORE_STATUS_NUM, SECTOR_STORE_USING);
        // }
        //format_sector(sector->addr, SECTOR_NOT_COMBINED);
        ef_print("\nCOMBINED SECTOR %d\n",sectorID);
        break;
    }



    ef_port_env_unlock();
    //return EF_NO_ERR;
}
/**
static bool print_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    bool value_is_str = true, print_value = false;
    size_t *using_size = arg1;

    if (env->crc_is_ok) {
        *using_size += env->len;
        if (env->status == ENV_WRITE) {
            ef_print("[0x%x]%.*s=", env->addr.value, env->name_len, env->name);//edit:添加地址

            if (env->value_len < EF_STR_ENV_VALUE_MAX_SIZE ) {
                uint8_t buf[32];
                size_t len, size;
__reload:
                for (len = 0, size = 0; len < env->value_len; len += size) {
                    if (len + sizeof(buf) < env->value_len) {
                        size = sizeof(buf);
                    } else {
                        size = env->value_len - len;
                    }
                    ef_port_read(env->addr.value + len, (uint32_t *) buf, EF_WG_ALIGN(size));
                    if (print_value) {
                        ef_print("%.*s", size, buf);
                    } else if (!ef_is_str(buf, size)) {
                        value_is_str = false;
                        break;
                    }
                }
                ef_print(" [0x%x]", env->addr.value + len);//edit:新增

            } else {
                value_is_str = false;
            }
            if (value_is_str && !print_value) {
                print_value = true;
                goto __reload;
            } else if (!value_is_str) {
                ef_print("blob @0x%08X %dbytes", env->addr.value, env->value_len);
            }
            ef_print("\n");
        }
    }

    return false;
}**/

static bool print_sector_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    //if()
    return false;
}


//新增 打印sector
void ef_bin_print_sector(void)
{
    struct sector_meta_data sector;
    size_t empty_sector = 0, using_sector = 0;
    size_t empty_size = 0,using_size = 0;
    uint32_t sec_addr;
    size_t i = 0;
    sector.addr = FAILED_ADDR;
    ef_port_env_lock();

    while ((sec_addr = get_next_sector_addr(&sector)) != FAILED_ADDR) {
        ef_print("\n========================================\n");
        read_sector_meta_data(sec_addr, &sector, true);//读扇区meta信息
        if(sector.check_ok == false)
            continue;
        ef_print("box%d FSec%d 0x%x\t|", i++, sector.addr / SECTOR_SIZE, sector.addr);//如果输出0可能是sector的状态为Full (有阈值可以来调整)
        ef_print("%d %d\n", sector.remain, sector.combined == SECTOR_NOT_COMBINED ? 1 : sector.combined);//如果输出0可能是sector的状态为Full (有阈值可以来调整)


        //遍历sector里面的env
        
        struct env_meta_data env;

        env.addr.start = FAILED_ADDR;
        /* search all ENV */
        while ((env.addr.start = get_next_env_addr(&sector, &env)) != FAILED_ADDR) {
            read_env(&env);
            if (env.crc_is_ok) {
                ef_print("[%.*s] -> %d", env.name_len, env.name, env.value_len);
                if(env.status == ENV_PRE_DELETE)
                    ef_print(" <PD>");
                else if(env.status == ENV_DELETED)
                    ef_print(" <DEL>");
                ef_print("\n");
            }
        }
    }
    ef_print("========================================\n");


    ef_port_env_unlock();
/**    return;
    sector_iterator(&sector, SECTOR_STORE_UNUSED, &empty_sector, &using_sector, sector_statistics_cb, false);

    return;

    struct env_meta_data env;
    size_t using_size = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return;
    }

    ef_port_env_lock();

    env_iterator(&env, &using_size, NULL, print_env_cb);

    ef_print("\nmode: next generation\n");
    ef_print("size: %lu/%lu bytes.\n", using_size + (SECTOR_NUM - EF_GC_EMPTY_SEC_THRESHOLD) * SECTOR_HDR_DATA_SIZE,
            ENV_AREA_SIZE - SECTOR_SIZE * EF_GC_EMPTY_SEC_THRESHOLD);

    ef_port_env_unlock();**/
}


/**
 * Set a string ENV. If it value is NULL, delete it.
 * If not find it in flash, then create it.
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
EfErrCode ef_set_env(const char *key, const char *value)
{
    return ef_set_env_blob(key, value, strlen(value));
}

/**
 * The same to ef_set_env on this mode.
 * It's compatibility with older versions (less then V4.0).
 *
 * @note this function is DEPRECATED
 *
 * @param key ENV name
 * @param value ENV value
 *
 * @return result
 */
EfErrCode ef_set_and_save_env(const char *key, const char *value)
{
    return ef_set_env_blob(key, value, strlen(value));
}

/**
 * Save ENV to flash.
 *
 * @note this function is DEPRECATED
 */
EfErrCode ef_save_env(void)
{
    /* do nothing not cur mode */
    return EF_NO_ERR;
}

/**
 * ENV set default.
 *
 * @return result
 */
EfErrCode ef_env_set_default(void)
{
    EfErrCode result = EF_NO_ERR;
    uint32_t addr, i, value_len;
    struct sector_meta_data sector;

    EF_ASSERT(default_env_set);
    EF_ASSERT(default_env_set_size);

    /* lock the ENV cache */
    ef_port_env_lock();
    /* format all sectors */
    for (addr = env_start_addr; addr < env_start_addr + ENV_AREA_SIZE; addr += SECTOR_SIZE) {//初始化所有的sector
        result = format_sector(addr, SECTOR_NOT_COMBINED);
        if (result != EF_NO_ERR) {
            goto __exit;
        }
    }
    /* create default ENV */
    for (i = 0; i < default_env_set_size; i++) {
        /* It seems to be a string when value length is 0.
         * This mechanism is for compatibility with older versions (less then V4.0). */
        if (default_env_set[i].value_len == 0) {
            value_len = strlen(default_env_set[i].value);
        } else {
            value_len = default_env_set[i].value_len;
        }
        sector.empty_env = FAILED_ADDR;
        create_env_blob(&sector, default_env_set[i].key, default_env_set[i].value, value_len);
        if (result != EF_NO_ERR) {
            goto __exit;
        }
    }

__exit:
    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

static bool print_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    bool value_is_str = true, print_value = false;
    size_t *using_size = arg1;

    if (env->crc_is_ok) {
        /* calculate the total using flash size */
        *using_size += env->len;
        /* check ENV */
        if (env->status == ENV_WRITE) {
            ef_print("[0x%x]%.*s=", env->addr.value, env->name_len, env->name);//edit:添加地址

            if (env->value_len < EF_STR_ENV_VALUE_MAX_SIZE ) {
                uint8_t buf[32];
                size_t len, size;
__reload:
                /* check the value is string */
                for (len = 0, size = 0; len < env->value_len; len += size) {
                    if (len + sizeof(buf) < env->value_len) {
                        size = sizeof(buf);
                    } else {
                        size = env->value_len - len;
                    }
                    ef_port_read(env->addr.value + len, (uint32_t *) buf, EF_WG_ALIGN(size));
                    if (print_value) {
                        ef_print("%.*s", size, buf);
                    } else if (!ef_is_str(buf, size)) {
                        value_is_str = false;
                        break;
                    }
                }
                ef_print(" [0x%x]", env->addr.value + len);//edit:新增

            } else {
                value_is_str = false;
            }
            if (value_is_str && !print_value) {
                print_value = true;
                goto __reload;
            } else if (!value_is_str) {
                ef_print("blob @0x%08X %dbytes", env->addr.value, env->value_len);
            }
            ef_print("\n");
        }
    }

    return false;
}


/**
 * Print ENV.
 */
void ef_print_env(void)
{
    struct env_meta_data env;
    size_t using_size = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    env_iterator(&env, &using_size, NULL, print_env_cb);

    ef_print("\nmode: next generation\n");
    ef_print("size: %lu/%lu bytes.\n", using_size + (SECTOR_NUM - EF_GC_EMPTY_SEC_THRESHOLD) * SECTOR_HDR_DATA_SIZE,
            ENV_AREA_SIZE - SECTOR_SIZE * EF_GC_EMPTY_SEC_THRESHOLD);

    /* unlock the ENV cache */
    ef_port_env_unlock();
}

#ifdef EF_ENV_AUTO_UPDATE
/*
 * Auto update ENV to latest default when current EF_ENV_VER_NUM is changed.
 */
static void env_auto_update(void)
{
    size_t saved_ver_num, setting_ver_num = EF_ENV_VER_NUM;

    if (get_env(VER_NUM_ENV_NAME, &saved_ver_num, sizeof(size_t), NULL) > 0) {
        /* check version number */
        if (saved_ver_num != setting_ver_num) {
            struct env_meta_data env;
            size_t i, value_len;
            struct sector_meta_data sector;
            EF_DEBUG("Update the ENV from version %d to %d.\n", saved_ver_num, setting_ver_num);
            for (i = 0; i < default_env_set_size; i++) {
                /* add a new ENV when it's not found */
                if (!find_env(default_env_set[i].key, &env)) {
                    /* It seems to be a string when value length is 0.
                     * This mechanism is for compatibility with older versions (less then V4.0). */
                    if (default_env_set[i].value_len == 0) {
                        value_len = strlen(default_env_set[i].value);
                    } else {
                        value_len = default_env_set[i].value_len;
                    }
                    sector.empty_env = FAILED_ADDR;
                    create_env_blob(&sector, default_env_set[i].key, default_env_set[i].value, value_len);
                }
            }
        } else {
            /* version number not changed now return */
            return;
        }
    }

    set_env(VER_NUM_ENV_NAME, &setting_ver_num, sizeof(size_t));
}
#endif /* EF_ENV_AUTO_UPDATE */

static bool check_sec_hdr_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    if (!sector->check_ok) {
        size_t *failed_count = arg1;

        EF_INFO("Warning: Sector header check failed. Format this sector (0x%08x).\n", sector->addr);
        (*failed_count) ++;
        format_sector(sector->addr, SECTOR_NOT_COMBINED);
    }

    return false;
}

static bool check_and_recovery_gc_cb(sector_meta_data_t sector, void *arg1, void *arg2)
{
    if (sector->check_ok && sector->status.dirty == SECTOR_DIRTY_GC) {
        /* make sure the GC request flag to true */
        gc_request = true;
        /* resume the GC operate */
        gc_collect();
    }

    return false;
}

static bool check_and_recovery_env_cb(env_meta_data_t env, void *arg1, void *arg2)
{
    /* recovery the prepare deleted ENV */
    if (env->crc_is_ok && env->status == ENV_PRE_DELETE) {
        EF_INFO("Found an ENV (%.*s) which has changed value failed. Now will recovery it.\n", env->name_len, env->name);
        /* recovery the old ENV */
        if (move_env(env) == EF_NO_ERR) {
            EF_DEBUG("Recovery the ENV successful.\n");
        } else {
            EF_DEBUG("Warning: Moved an ENV (size %d) failed when recovery. Now will GC then retry.\n", env->len);
            return true;
        }
    } else if (env->status == ENV_PRE_WRITE) {
        uint8_t status_table[ENV_STATUS_TABLE_SIZE];
        /* the ENV has not write finish, change the status to error */
        //TODO 绘制异常处理的状态装换图
        write_status(env->addr.start, status_table, ENV_STATUS_NUM, ENV_ERR_HDR);
        return true;
    }

    return false;
}

/**
 * Check and load the flash ENV meta data.
 *
 * @return result
 */
EfErrCode ef_load_env(void)
{
    EfErrCode result = EF_NO_ERR;
    struct env_meta_data env;
    struct sector_meta_data sector;
    size_t check_failed_count = 0;

    /* lock the ENV cache */
    ef_port_env_lock();

    in_recovery_check = true;
    /* check all sector header */
    sector_iterator(&sector, SECTOR_STORE_UNUSED, &check_failed_count, NULL, check_sec_hdr_cb, false);
    /* all sector header check failed */
    if (check_failed_count == SECTOR_NUM) {
        EF_INFO("Warning: All sector header check failed. Set it to default.\n");
        ef_env_set_default();
    }
    /* check all sector header for recovery GC */
    sector_iterator(&sector, SECTOR_STORE_UNUSED, NULL, NULL, check_and_recovery_gc_cb, false);

__retry:
    /* check all ENV for recovery */
    env_iterator(&env, NULL, NULL, check_and_recovery_env_cb);
    if (gc_request) {
        gc_collect();
        goto __retry;
    }

    in_recovery_check = false;

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return result;
}

/**
 * Flash ENV initialize.
 *
 * @param default_env default ENV set for user
 * @param default_env_size default ENV set size
 *
 * @return result
 */
EfErrCode ef_env_init(ef_env const *default_env, size_t default_env_size) {
    EfErrCode result = EF_NO_ERR;

#ifdef EF_ENV_USING_CACHE
    size_t i;
#endif

    EF_ASSERT(default_env);
    EF_ASSERT(ENV_AREA_SIZE);
    /* must be aligned with erase_min_size */
    EF_ASSERT(ENV_AREA_SIZE % EF_ERASE_MIN_SIZE == 0);
    /* sector number must be greater than or equal to 2 */
    EF_ASSERT(SECTOR_NUM >= 2);
    /* must be aligned with write granularity */
    EF_ASSERT((EF_STR_ENV_VALUE_MAX_SIZE * 8) % EF_WRITE_GRAN == 0);

    if (init_ok) {
        return EF_NO_ERR;
    }

#ifdef EF_ENV_USING_CACHE
    for (i = 0; i < EF_SECTOR_CACHE_TABLE_SIZE; i++) {
        sector_cache_table[i].addr = FAILED_ADDR;
    }
    for (i = 0; i < EF_ENV_CACHE_TABLE_SIZE; i++) {
        env_cache_table[i].addr = FAILED_ADDR;
    }
#endif /* EF_ENV_USING_CACHE */

    env_start_addr = EF_START_ADDR;
    default_env_set = default_env;
    default_env_set_size = default_env_size;

    EF_DEBUG("ENV start address is 0x%08X, size is %d bytes.\n", EF_START_ADDR, ENV_AREA_SIZE);

    result = ef_load_env();

#ifdef EF_ENV_AUTO_UPDATE
    if (result == EF_NO_ERR) {
        env_auto_update();
    }
#endif

    if (result == EF_NO_ERR) {
        init_ok = true;
    }

    return result;
}


/** add some new function by bin **/
//size_t ef_get_env_blob(const char *key, void *value_buf, size_t buf_len, size_t *saved_value_len)
/*
size_t ef_get_env_blob(const char *key, void *value_buf, size_t buf_len, size_t *saved_value_len)
{
    size_t read_len = 0;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return 0;
    }

    lock the ENV cache
    ef_port_env_lock();

    read_len = get_env(key, value_buf, buf_len, saved_value_len);

    unlock the ENV cache
    ef_port_env_unlock();

    return read_len;
}

static size_t get_env(const char *key, void *value_buf, size_t buf_len, size_t *value_len)
{
    struct env_meta_data env;
    size_t read_len = 0;

    if (find_env(key, &env)) {
        if (value_len) {
            *value_len = env.value_len;
        }
        if (buf_len > env.value_len) {
            read_len = env.value_len;
        } else {
            read_len = buf_len;
        }
        ef_port_read(env.addr.value, (uint32_t *) value_buf, read_len);
    }

    return read_len;
}



static size_t get_env_stream(const char *key, size_t offset, void *value_buf, size_t buf_len, size_t *value_len)
{
    struct env_meta_data env;
    size_t read_len = 0;

    if (find_env(key, &env)) {
        //SAVED_VALUE_LENGTH pointer
        if (value_len) {
            *value_len = env.value_len;
        }
        // buf_len buf Array Length 取最小值
        if (buf_len + offset > env.value_len) {
            read_len = env.value_len - offset;
        } else {
            read_len = buf_len;
        }
        ef_port_read(env.addr.value, (uint32_t *) value_buf + offset, read_len);
    }

    return read_len;
}
*/
size_t ef_get_env_stream(const char *key, size_t offset, void *value_buf, size_t buf_len, size_t *saved_value_len)
{
    size_t read_len = 0;
    struct env_meta_data env;

    if (!init_ok) {
        EF_INFO("ENV isn't initialize OK.\n");
        return 0;
    }

    /* lock the ENV cache */
    ef_port_env_lock();

    if (find_env(key, &env)) {
        if (saved_value_len) {
            *saved_value_len = env.value_len;
        }
        if (buf_len > env.value_len) {
            read_len = env.value_len - offset;
        } else {
            read_len = buf_len;
        }
        //offset推动
        ef_port_read(env.addr.value + offset, (uint32_t *) value_buf, read_len);
    }

    //read_len = get_env(key, (void *)((uint32_t *)value_buf + offset), buf_len - offset, saved_value_len);

    //*saved_value_len -= offset;

    /* unlock the ENV cache */
    ef_port_env_unlock();

    return read_len;
}

/**
 * 结合XIPFS的一些函数,暂时放在这里,以后以easyflash的拓展出现
 * 逻辑需要大改,这里的作用就是寻找下一个可用的env
 * @name getdents
 */
bool env_get_fs_getdents(env_meta_data_t env, uint32_t* sec_addr_p)
{
    //static uint32_t get_next_env_addr(sector_meta_data_t sector, env_meta_data_t pre_env)
    ef_port_env_lock();

    static struct sector_meta_data sector;
    uint32_t sec_addr = *sec_addr_p;
    //pre_sector
    sector.addr = *sec_addr_p;


    //在传入的sector下寻找,找不到换到下一个sector?

    
    do{
        //寻找下一个env地址,若为首地址,那么需要start=FAILADDR
        //若返回的start为FAIL_ADDR.那么证明这次循环到头了

        //ef_print("Sector %x,pre_env %x\n", sector.addr, env->addr.start);

        if(sec_addr != FAILED_ADDR && (env->addr.start = get_next_env_addr(&sector, env)) != FAILED_ADDR)
        {
            read_env(env);
            //ENV正常,当前即可
            if(env->crc_is_ok && env->status == ENV_WRITE)
                break;
        }else{
            //FAILED_ADDR
            //需要切换到下一个扇区 或者是 到头了?那么判断一下 还有没有下一个扇区就可以了
            while ((sec_addr = get_next_sector_addr(&sector)) != FAILED_ADDR) {
                if (read_sector_meta_data(sec_addr, &sector, false) != EF_NO_ERR) {
                    continue;
                }
                if (sector.status.store == SECTOR_STORE_EMPTY)
                    continue;
                //SECTOR里面应该有ENV的情况,那么就可以去FIND一下了
                env->addr.start = FAILED_ADDR;
                break;
            }
        }
    }while(sec_addr != FAILED_ADDR);


    *sec_addr_p = sec_addr;
    
    ef_port_env_unlock();
    if(sec_addr == FAILED_ADDR)
        return false;
    return true;
}

bool env_remainSize(sector_meta_data_t sector, void *arg1,void* arg2)
{
    size_t remain_size = *(size_t *)arg1;
    if(sector->check_ok)
    {
        remain_size += sector->remain;
    }

    return false;
}

void ef_get_remainSive(ef_env_dev_t dev,size_t *remain_size)
{
    struct sector_meta_data sector;

    sector_iterator(&sector, SECTOR_STORE_EMPTY, remain_size, NULL, env_remainSize, true);
    sector_iterator(&sector, SECTOR_STORE_USING, remain_size, NULL, env_remainSize, true);
}


#endif /* defined(EF_USING_ENV) && !defined(EF_ENV_USING_LEGACY_MODE) */
