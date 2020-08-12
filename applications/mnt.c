#include <rtthread.h>

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#include "dfs_ramfs.h"
#include "dfs_romfs.h"

#include "dfs_fs.h"
#include "dfs_posix.h"
/* �ļ�ϵͳ�Զ����ر� */
const struct dfs_mount_tbl mount_table[]=
{
  //{"sd0","/","elm",0,0},
  //{"W25Q128","/dev0","elm",0,0},
  {0}
};


#include "fal.h"





#include <rtdbg.h>
#define FS_PARTITION_NAME  "fs"
#define XIP_PARTITION_NAME  "ef"

int flash_init()
{
    /* ��ʼ�� fal ���� */
    fal_init();
	
    /* �� spi flash ����Ϊ "exchip0" �ķ����ϴ���һ�����豸 */
    // struct rt_device *flash_dev = fal_blk_device_create(FS_PARTITION_NAME);
    // if (flash_dev == NULL)
    // {
    //     LOG_E("Can't create a block device on '%s' partition.", FS_PARTITION_NAME);
    // }
    // else
    // {
    //     LOG_D("Create a block device on the %s partition of flash successful.", FS_PARTITION_NAME);
    // }

    /* ���� spi flash ����Ϊ "dev0" �ķ����ϵ��ļ�ϵͳ */
    // if (dfs_mount(flash_dev->parent.name, "/dev0", "elm", 0, 0) == 0)
    // {
    //     LOG_I("Filesystem initialized!");
    // }
    // else
    // {
    //     LOG_E("Failed to initialize filesystem!");
    //     LOG_D("You should create a filesystem on the block device first!");
    // }
    extern void easyflash_init();
    easyflash_init();

    return 0;
}

//INIT_ENV_EXPORT(flash_init);

int bin_mnt_init(void)
{

    flash_init();

    /* �����ϴ������豸 */
    struct rt_device *flash_dev = fal_blk_device_create(FS_PARTITION_NAME);
    if (flash_dev == NULL)
    {
        LOG_E("Can't create a block device on '%s' partition.", FS_PARTITION_NAME);
    }
    else
    {
        LOG_D("Create a block device on the %s partition of flash successful.", FS_PARTITION_NAME);
    }

    /* ���豸�Ϲ����ļ�ϵͳ */
    if (dfs_mount(flash_dev->parent.name, "/", "elm", 0, 0) == 0)
    {
        LOG_I("Filesystem initialized!");
    }
    else
    {
        if(dfs_mkfs("elm", FS_PARTITION_NAME) == 0)
        {
            if (dfs_mount(FS_PARTITION_NAME, "/", "elm", 0, 0) == 0)
            {
                rt_kprintf("file system initialization done!\n");
            }
            else
            {
                rt_kprintf("file system initialization failed!\n");
            }
        }
    }

    if(!fal_char_device_create(XIP_PARTITION_NAME))
    {
        rt_kprintf("flash:%s create failed!\n", XIP_PARTITION_NAME);
    }else{
        if(access("/xip", 0) != RT_EOK)
        {
            if(mkdir("/xip", 0) != RT_EOK)
            {
                LOG_E("xip filesystem error!\n");
                goto _xipfsend;
            }
        }
        if (dfs_mount(XIP_PARTITION_NAME, "/xip", "xipfs", 0, 0) == 0)
        {
            rt_kprintf("XIP file system initializated!\n");
        }
        else
        {
            rt_kprintf("XIP file system initializate failed!\n");
        }
    }

_xipfsend:

/*
    if (dfs_mount(RT_NULL, "/ram", "ram", 0, dfs_ramfs_create(rt_malloc(1024),1024)) == 0)
    {
        rt_kprintf("RAM file system initializated!\n");
    }
    else
    {
        rt_kprintf("RAM file system initializate failed!\n");
    }

    if(!fal_blk_device_create("exchip0"))
    {
        rt_kprintf("flash:%s create failed!\n","exchip0");
    }else{
        if (dfs_mount("exchip0", "/dev0", "elm", 0, 0) == 0)
        {
            LOG_I("Filesystem initialized!");
        }
        else
        {
            LOG_E("Failed to initialize filesystem!");
            LOG_D("You should create a filesystem on the block device first!");
        }
    }

    if( !fal_char_device_create("exchip1"))
    {
        rt_kprintf("flash:%s create failed!\n","exchip1");
    }else{
        //���ַ��豸�Ϲ���xipfs?
        if (dfs_mount("exchip1", "/xip", "xipfs", 0, 0) == 0)
        {
            rt_kprintf("XIP file system initializated!\n");
        }
        else
        {
            rt_kprintf("XIP file system initializate failed!\n");
        }
    }



*/


		
		
		
		


    return 0;
}
INIT_ENV_EXPORT(bin_mnt_init);




#endif
