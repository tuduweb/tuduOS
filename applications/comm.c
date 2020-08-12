#include <rtthread.h>
#include <board.h>

#include <dfs_posix.h>
#include <stdlib.h>

#include "ymodem.h"
#include "finsh.h"

struct custom_ctx
{
    struct rym_ctx parent;
    int fd;
    int flen;
    char fpath[256];
};

static enum rym_code _rym_bg(
    struct rym_ctx *ctx,
    rt_uint8_t *pbuf,
    rt_size_t len)
{
    char *buf = (char*)pbuf;
    struct custom_ctx *cctx = (struct custom_ctx *)ctx;
    cctx->fpath[0] = '\0';

    /* use current working directory 保存到当前的目录*/
    getcwd(cctx->fpath, sizeof(cctx->fpath));
    strcat(cctx->fpath, "/");
    strcat(cctx->fpath, (char*)buf);

    cctx->fd = open(cctx->fpath, O_CREAT | O_WRONLY | O_TRUNC, 0);
    if (cctx->fd < 0)
    {
        rt_err_t err = rt_get_errno();
        rt_kprintf("error creating file: %d\n", err);
        rt_kprintf("abort transmission\n");
        return RYM_CODE_CAN;
    }

    cctx->flen = atoi(buf + strlen(buf) + 1);//把字符串转成整数 atoi //fileLength?
    if (cctx->flen == 0)
        cctx->flen = -1;
    return RYM_CODE_ACK;
}

static enum rym_code _rym_tof(
    struct rym_ctx *ctx,
    rt_uint8_t *buf,
    rt_size_t len)
{
    struct custom_ctx *cctx = (struct custom_ctx *)ctx;
    RT_ASSERT(cctx->fd >= 0);
    //写入数据
    if (cctx->flen == -1)
    {
        write(cctx->fd, buf, len);
    }
    else
    {
        int wlen = len > cctx->flen ? cctx->flen : len;
        write(cctx->fd, buf, wlen);
        cctx->flen -= wlen;
    }
    return RYM_CODE_ACK;
}

static enum rym_code _rym_end(
    struct rym_ctx *ctx,
    rt_uint8_t *buf,
    rt_size_t len)
{
    struct custom_ctx *cctx = (struct custom_ctx *)ctx;

    RT_ASSERT(cctx->fd >= 0);
    //关闭句柄
    close(cctx->fd);
    cctx->fd = -1;

    return RYM_CODE_ACK;
}

rt_err_t rym_write_to_file(rt_device_t idev)
{
    rt_err_t res;
    struct custom_ctx *ctx = rt_malloc(sizeof(*ctx));
    //开临时空间 如果要做隔离的话 这里应该在单独的“堆栈”中

    RT_ASSERT(idev);//input device

    rt_kprintf("entering RYM mode\n");

    res = rym_recv_on_device(&ctx->parent, idev,  RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                             _rym_bg, _rym_tof, _rym_end, 1000);//begin trans ends

    /* there is no Ymodem traffic on the line so print out info. */
    rt_kprintf("leaving RYM mode with code %d\n", res);
    rt_kprintf("file %s has been created.\n", ctx->fpath);

    rt_free(ctx);

    return res;
}

int comm(int argc, char **argv)
{
    rt_err_t res;

    if (argc != 2)
    {
        rt_kprintf("Usage: ymodem device\n");
        return 0;
    }

    rt_device_t dev = rt_device_find(argv[1]);
    if (!dev)
    {
        rt_kprintf("could not find device:%s\n", argv[1]);
        return -RT_ERROR;
    }

    res = rym_write_to_file(dev);

    return res;
}

MSH_CMD_EXPORT(comm, comm sample: comm <device>);
