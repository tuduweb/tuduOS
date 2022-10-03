#include <rthw.h>
#include <rtthread.h>
#include "lwt_shm.h"

#define DBG_TAG    "SRV"
#define DBG_LVL    DBG_WARNING
#include <rtdbg.h>

#include "server.h"

#include "string.h"


/**
 * 后台应用
 */

rt_err_t server_app_install(char* app)
{
    return -RT_ERROR;
}

char txt[] = "hello IPC";


struct system_cmd{
    int type;
    char* path;
};

struct system_cmd cmd;

static void system_server_send_entry(void *parameter)
{
    int fd;
    struct bin_channel_msg msg;


    cmd.type = 1;


    fd = bin_channel_open("server", 0);

    msg.u.b.buf = &cmd;
    cmd.type = 1;
    cmd.path = "/download/main.c";
    bin_channel_send(fd, &msg, 1);



    bin_channel_close(fd);
}

void msh_app_server(int argc,char* argv[])
{
    rt_thread_t tid1 = RT_NULL;

    tid1 = rt_thread_create("srv_S", system_server_send_entry, NULL, 512, 10, 20);
    if(tid1 != RT_NULL)
        rt_thread_startup(tid1);

}

FINSH_FUNCTION_EXPORT_ALIAS(msh_app_server, __cmd_server_test, application server.);

/* ymodem_app */
#include <dfs_posix.h>
#include <stdlib.h>
#include "ymodem.h"
#include "finsh.h"
extern const char *_get_path_lastname(const char *path);
static void system_server_entry(void *parameter)
{
    int fd;
    struct bin_channel_msg msg;
    struct system_cmd cmd;
    struct stat buf;
    char* filename = "\0";

    fd = bin_channel_open("server", 0);

    /* 挂起线程,等待命令的到来 */
    while(bin_channel_recv(fd, -1, &msg) == RT_EOK)
    {
        rt_kprintf("[SRV](from:%s) %x\n", (msg.sender)->name, msg.u.d );//, msg->u.b.buf

        memcpy(&cmd, msg.u.b.buf, sizeof(cmd));


        switch (cmd.type)
        {
        case 1:
            rt_kprintf("app install begin %s\n", cmd.path);

            if(cmd.path == RT_NULL)
                break;

            if(stat(cmd.path, &buf)  == RT_EOK)
            {
                rt_kprintf("[SRV] app size:%d\n", buf.st_size);
                // /* malloc temp space */
                // if(buf.st_size > 0 && buf.st_size < 4096*4)
                // {
                //     void *appBuf = rt_malloc(buf.st_size);
                //     if(appBuf != RT_NULL)
                //     {
                //         int file_fd = open(cmd.path, 0, O_RDONLY);
                //         if(file_fd > 0)
                //         {
                //             if(read(file_fd, appBuf, buf.st_size))
                //             {
                //                 //读取之后..就是写
                //                 int xip_fd = open()
                //                 rt_kprintf("[SRV] app read OK!\n");
                //             }
                //         }
                //         //release space
                //         rt_free(appBuf);
                //     }
                // }
                if(buf.st_size > 0 && buf.st_size < 4096*4)
                {
                    filename = (char *)rt_malloc(100);
                    strcpy(filename, "/xip/");
                    strcat(filename, _get_path_lastname(cmd.path));

                    extern void copy(const char *src, const char *dst);
                    rt_kprintf("filedir:%s\n", filename);//输出最后一个斜杠后的,即文件名
                    //这里把download下的文件复制到xip下..
                    copy(cmd.path, filename);

                    rt_free(filename);
                }
            
            }

            break;
        
        case 2:
            rt_kprintf("app uninstall %s\n", cmd.path);
            //卸载用到的是写入空
            if(cmd.path == RT_NULL)
                break;
            if(buf.st_size == 0)
                break;

            filename = (char *)rt_malloc(100);
            strcpy(filename, "/xip/");
            strcat(filename, _get_path_lastname(cmd.path));

            if(stat(filename, &buf)  == RT_EOK)
            {


                if(dfs_file_unlink(filename) < 0)
                {
                    rt_kprintf("Delete %s failed\n", filename);
                }else{
                    rt_kprintf("Delete %s ok\n", filename);
                }
            }
            
            rt_free(filename);
            break;
        
        default:
            break;
        }


        bin_channel_reply(fd, &msg);

        //rt_kprintf("hello\n");
    }

}

int system_server_init(void)
{
    rt_thread_t tid = RT_NULL;
    tid = rt_thread_create("srv", system_server_entry, NULL, 4096, 10, 20);
    if(tid != RT_NULL)
        rt_thread_startup(tid);
    else
    {
        LOG_E("system server init error!");
        while(1);
    }
    return 0;
}
INIT_ENV_EXPORT(system_server_init);


/**
 * 这个文件下的程序与comm下的不一样
 */
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
    //download目录下
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

    int fd;
    fd = bin_channel_open("server", 0);

    struct bin_channel_msg msg;

    cmd.type = 1;
    cmd.path = ctx->fpath;
    msg.u.b.buf = &cmd;
    bin_channel_send(fd, &msg, 1);
    //need reply 阻塞

    

    bin_channel_close(fd);

    //int fd = open(ctx->fpath, 0, O_RDONLY);
    // struct stat buf;
    // if(stat(ctx->fpath, &buf)  == RT_EOK)
    // {
    //     rt_kprintf("[SRV] app size:%d\n", buf.st_size);
    //     /* malloc temp space */
    //     if(buf.st_size > 0 && buf.st_size < 4096*4)
    //     {
    //         void *appBuf = malloc(buf.st_size);
    //         if(appBuf != RT_NULL)
    //         {
    //             int fd = open(ctx->fpath, 0, O_RDONLY);
    //             if(fd > 0)
    //             {
    //                 if(read(fd, appBuf, buf.st_size))
    //                 {
    //                     rt_kprintf("[SRV] app read OK!\n");
    //                 }
    //             }
    //             rt_free(appBuf);
    //         }
    //     }
    // }

    rt_free(ctx);

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



int server_app(int argc, char* argv[])
{
    if(argc == 1)
    {
        //empty args
    
    }else{

        // int fd;
        // fd = bin_channel_open("server", 0);

        // struct bin_channel_msg msg;

        // cmd.type = 2;
        // cmd.path = "test";
        // msg.u.b.buf = &cmd;

        // bin_channel_send(fd, &msg, 1);
        // //need reply 阻塞
        // bin_channel_close(fd);

        int fd = open("/download/123.txt", 0 ,O_RDWR);
        for(int i = 0; i < 3; ++i)
            write(fd, "1234", 5);
        
        close(fd);


    }
    return 0;
}

FINSH_FUNCTION_EXPORT_ALIAS(server_app, __cmd_app, APP helper~);
