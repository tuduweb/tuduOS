#ifndef __BIN_LIGHTWEIGHTTASK__
#define __BIN_LIGHTWEIGHTTASK__

#define LWT_MAGIC           'LWT'

#define LWP_TYPE_FIX_ADDR   0x01
#define LWP_TYPE_DYN_ADDR   0x02

#define LWP_ARG_MAX         8

#include <dfs.h>

typedef int pid_t;
//注意字节对齐 4byte对齐格式
struct rt_lwt
{
    //mpu保护信息

    uint8_t lwt_type;
    uint8_t heap_cnt;
    uint8_t reserv[2];

    struct rt_lwt *parent;
    struct rt_lwt *first_child;
    struct rt_lwt *sibling;
    rt_list_t wait_list;

    int32_t finish;
    int lwp_ret;

    rt_list_t hlist;                                    /**< headp list */

    //程序代码段位置
    uint8_t *text_entry;
    uint32_t text_size;
    //静态数据段
    uint8_t *data_entry;
    uint32_t data_size;

    uint32_t *kernel_sp;                                /**< kernel stack point */
    struct dfs_fdtable fdt;
    void *args;
    int ref;

    pid_t pid;
    rt_list_t t_grp;

    char    cmd[8];

    //signal相关
    rt_uint32_t signal;


    //其他
    rt_list_t object_list;
};

struct lwt_header
{
    uint8_t magic;
    uint8_t compress_encrypt_algo;
    uint16_t reserved;

    uint32_t crc32;
};

struct lwt_chunk
{
    uint32_t total_len;

    char name[4];
    uint32_t data_len;
    uint32_t data_len_space;
};

#define LWP_PIDMAP_SIZE 30
struct lwt_pidmap
{
    struct rt_lwt *pidmap[LWP_PIDMAP_SIZE];
    pid_t lastpid;
};

/**
 * 轻量级进程 执行文件
 * 在父进程中fork一个子进程
**/
int lwt_execve(char *filename, int argc, char **argv, char **envp);




#endif
