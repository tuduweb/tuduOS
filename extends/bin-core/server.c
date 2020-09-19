#include <rthw.h>
#include <rtthread.h>
#include "lwt_shm.h"

#define DBG_TAG    "SRV"
#define DBG_LVL    DBG_WARNING
#include <rtdbg.h>

#include "server.h"
/**
 * 后台应用
 */

rt_err_t server_app_install()
{
    return -RT_ERROR;
}

void msh_app_server(int argc,char* argv[])
{
}
FINSH_FUNCTION_EXPORT_ALIAS(msh_app_server, __cmd_list_appserver, application server.);


/* ymodem_app */
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

static enum rym_code _ymodem_on_begin(
    struct rym_ctx *ctx,
    rt_uint8_t *pbuf,
    rt_size_t len)
{
    char *buf = (char*)pbuf;
    struct custom_ctx *cctx = (struct custom_ctx *)ctx;
    cctx->fpath[0] = '\0';

    /* use current working directory 保存到当前的目录 */
    //getcwd(cctx->fpath, sizeof(cctx->fpath));
    strcpy(cctx->fpath, "/download");//保存到/download目录下
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

static enum rym_code _ymodem_on_data(
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
        int wlen = len > cctx->flen ? cctx->flen : len;//最大大小
        write(cctx->fd, buf, wlen);
        cctx->flen -= wlen;
    }
    return RYM_CODE_ACK;
}

static enum rym_code _ymodem_on_end(
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

/**
 * 通过Ymodem获取App程序
 */
rt_err_t ymodem_get_app(rt_device_t idev)
{
    rt_err_t res;
    struct custom_ctx *ctx = rt_malloc(sizeof(*ctx));
    //开临时空间 如果要做隔离的话 这里应该在单独的“堆栈”中

    RT_ASSERT(idev);//input device

    rt_kprintf("entering RYM mode\n");

    res = rym_recv_on_device(&ctx->parent, idev,  RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX,
                             _ymodem_on_begin, _ymodem_on_data, _ymodem_on_end, 1000);//begin trans ends

    /* there is no Ymodem traffic on the line so print out info. */
    rt_kprintf("leaving RYM mode with code %d\n", res);
    rt_kprintf("file %s has been created.\n", ctx->fpath);

    /* if complate trans , we should install the app . And the app path is 'ctx->fpath' */
    /* we use xipfs to save App, so we need write this whole file to xipfs */

    rt_free(ctx);

    //int fd = open(ctx->fpath, 0, O_RDONLY);
    struct stat buf;
    if(stat(ctx->fpath, &buf)  == RT_EOK)
    {
        rt_kprintf("[SRV] app size:%d\n", buf.st_size);
        /* malloc temp space */
        if(buf.st_size > 0 && buf.st_size < 4096*4)
        {
            void *appBuf = malloc(buf.st_size);
            if(appBuf != RT_NULL)
            {
                int fd = open(ctx->fpath, 0, O_RDONLY);
                if(fd > 0)
                {
                    if(read(fd, appBuf, buf.st_size))
                    {
                        rt_kprintf("[SRV] app read OK!\n");
                    }
                }
                rt_free(appBuf);
            }
        }
    }

    return res;
}

int ymodem_app(int argc,char* argv[])
{
    rt_err_t res;

    /* get current console device */
    rt_device_t dev = rt_console_get_device();
    res = ymodem_get_app(dev);
    
    return res;
}
MSH_CMD_EXPORT(ymodem_app, ymodem_app isntall app.);
