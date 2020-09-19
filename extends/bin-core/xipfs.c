/**
 * XIP文件系统
 * 主要是存放一些可以执行xip操作的文件
 * 参考FrostOS
**/
#include <xipfs.h>
#include <rtthread.h>



#include <easyflash.h>

//---->以下为日志单元的配置项
#define LOG_TAG     "xipfs"     // 该模块对应的标签。不定义时，默认：NO_TAG
//#define LOG_LVL     LOG_LVL_DBG   // 该模块对应的日志输出级别。不定义时，默认：调试级别
#include <ulog.h>                 // 必须在 LOG_TAG 与 LOG_LVL 下面
//日志单元配置项结束<----




/*
enum env_status {
    ENV_UNUSED = 0x0,
    ENV_PRE_WRITE = 0x1,
    ENV_WRITE = 0x2,
    ENV_PRE_DELETE = 0x3,
    ENV_DELETED = 0x4,
    ENV_ERR_HDR = 0x5,
    ENV_STATUS_NUM = 0x6,
};
typedef enum env_status env_status_t;
*/

struct root_dirent{
    struct ef_env_dev env_dev;
    struct env_meta_data env;
    uint32_t sec_addr;
};

typedef struct root_dirent *root_dirent_t;


struct root_dirent* xip_mount_table[2] = {RT_NULL};

/**
 * 一些变量 可能还没有初始化 需要注意
**/
rt_mutex_t ef_write_lock;//写入操作mutex锁
rt_event_t ef_write_event;//写入操作事件


struct root_dirent* get_env_by_dev(rt_device_t dev_id)
{
    //void* deviceType = dev_id->parent.type;//type of kernel object

    for(int table_id = 0; table_id < 2; ++table_id)
    {
        if(xip_mount_table[table_id] && xip_mount_table[table_id]->env_dev.flash == dev_id)
            return xip_mount_table[table_id];
    }


    //rt_kprintf("DEV %s\n",dev_id->parent.name);

    //fal_partition_find()

    return NULL;
}

/**
 * 文件系统 挂载
 * 把fs->path下挂载文件系统,其中含有私有数据data
 * fs   : path,ops,dev_id
 * data : 文件系统的私有数据 在dfs_mount调用中自行传入
**/
#include "fal.h"
int mount_index = 0;

int dfs_xipfs_mount(struct dfs_filesystem *fs,
                    unsigned long          rwflag,
                    const void            *data)
{
    extern const struct fal_partition *fal_partition_find(const char *name);
    if(fs->dev_id->type != RT_Device_Class_Char)
    {
        rt_kprintf("The flash device type must be Char!\n");
        /* Not a character device */
        return -ENOTTY;
    }

    rt_kprintf("XipFS Mounted!\n");

    //已经挂载过了 在ef里面存在这些信息!?
    if( get_env_by_dev(fs->dev_id) != NULL)
        return RT_EOK;

    
    if(mount_index >= 2u)
        return -RT_ERROR;

    /* 如果没有找到挂载表 那么在这里新建挂载表 */



    //申请地址空间
    root_dirent_t root = rt_malloc(sizeof(struct root_dirent));
    //没申请到
    if(root == RT_NULL)
        return RT_ERROR;

    //初始化基本的参数(可能需要封装成函数)
    root->env_dev.part = fal_partition_find(fs->dev_id->parent.name);//所在分区,信息
    root->env_dev.env_start_addr = (fal_flash_device_find(root->env_dev.part->flash_name ))->addr;//初始化地址位置
    root->env_dev.sector_size = (fal_flash_device_find(root->env_dev.part->flash_name ))->blk_size;//扇区大小
    root->env_dev.flash = fs->dev_id;
    root->env_dev.init_ok = false;
    root->env_dev.in_recovery_check = false;
    //root->env_dev.part->flash_name
    root->sec_addr = 0xFFFFFFFF;

    xip_mount_table[mount_index] = root;
    
    //挂载数量++
    mount_index++;

    
    /*
    for(int table_id = 0; table_id < 2; ++table_id)
    {

        
        //类型 void *flash;
        ////ef_env_dev->flash = fs->dev_id->parent.type;//得到flash?
        //sector_size保存在哪里?在挂载的物理硬件的信息里
        ////ef_env_dev->sector_size = fs->dev_id;//得到sector_size

        //ef_env_init_by_flash(ef_env_dev);

    }*/

    //如果挂载表新建成功 那么需要执行清理等步骤?
    //清理:就是把app分区里面的碎片给整理了,
    //换句话说 可以把app一个个的搬运到新分区中,这样可以形成新表
    return RT_EOK;

}

/**
 * 取消挂载
 * 在这里需要停止所有xip操作吗?
**/
int dfs_xipfs_unmount(struct dfs_filesystem *fs)
{
    root_dirent_t root_dirent = get_env_by_dev(fs->dev_id);
    
    if(root_dirent)
        rt_free(root_dirent);
    
    mount_index--;//这里是有BUG的

    return RT_EOK;
}

/**
 * 在某个dev_id上初始化xipfs文件系统
 * 如果配置表中没有这个devid的信息 那么直接返回
 * 如果配置表中有这个devid信息 那么把信息配置到ef系统
**/
int dfs_xipfs_mkfs(rt_device_t dev_id)
{
    struct root_dirent* mount_table;
    mount_table = get_env_by_dev(dev_id);
    
    //以下函数需要改造
    if(mount_table)
        ef_env_set_default();//ef_env_set_default(mount_table->env_dev);

    return RT_EOK;
}

#include <fal.h>
/**
 * 获取文件磁盘信息 放在buf中
**/
int dfs_xipfs_statfs(struct dfs_filesystem *fs, struct statfs *buf)
{
    size_t block_size, total_size,free_size;
    struct root_dirent* root_dirent;

    //参数键入env中的name

    root_dirent = get_env_by_dev(fs->dev_id);
    //env_table->env_dev.flash从这里面搞出分区名字?
    //需要找到正确的变量 ..... block_size & f_blocks
    block_size = fal_flash_device_find( root_dirent->env.name )->blk_size;
    total_size = 512;//在env中获取已占用大小
    buf->f_bsize  = block_size;
    buf->f_blocks = root_dirent->env.len / block_size;//ramfs->memheap.pool_size / 512;
    buf->f_bfree  = 512;//ramfs->memheap.available_size / 512;

    return RT_EOK;
}


/**
 * 文件系统 改名
**/
int dfs_xipfs_rename(struct dfs_filesystem *fs,
                     const char            *oldpath,
                     const char            *newpath)
{
    //当前知道是可以在“物理”上，对app进行更名的
    //用二进制查看工具 如 HxD,可以看到在特定位置有APP的名称

    //但是更名操作势必要进行很多步骤 所以谨慎编写使用
    //!注意 如果直接替换存储器上的内容,那么需要重新擦除存储器内容,禁止。

    return -RT_ERROR;
}


/**
 * XIP文件系统 删除操作
**/
int dfs_xipfs_unlink(struct dfs_filesystem *fs, const char *path)
{
    root_dirent_t root_dirent;
    
    root_dirent = get_env_by_dev(fs->dev_id);

    if(root_dirent)
    {
        //在easyflash中删除这个变量,没有了这个的信息,代表删除操作
        //这种方法称为软删除 所以会造成“硬件”上的碎片化
        // ef_dev_env env_dev filename[fs->path + 1]
    }

    return -RT_ERROR;
}

extern bool find_env(const char *key, env_meta_data_t env);
/**
 * XIP文件系统 获得文件状态
**/
int dfs_xipfs_stat(struct dfs_filesystem *fs,
                   const char            *path,
                   struct stat           *st)
{
    //需要判断path是目录,还是文件:目录和文件返回的数据是不一样的
    //返回的数据是在st这个实参中的
    bool find_ok = false;
    struct root_dirent* root_dirent = get_env_by_dev(fs->dev_id);
    //struct env_meta_data env;
    //envmetadata

    //rt_kprintf("XIP STAT: dev:%d path:%s\n", fs->dev_id, path);


    //return -RT_ERROR;
    //如果这个device_id下有配置的xip环境 也就是 是xip环境
    if(root_dirent || true)
    {
        st->st_dev = 0;
        st->st_size = 0;
        st->st_mtime = 0;

        /* 如果是获取的文件夹信息"根目录",也就是顶级目录. */
        if((*path == '/' && !*(path+1)) || (find_ok = find_env( path+1, &root_dirent->env)) && root_dirent->env.value_len == 1 )
        {
            //文件夹
            st->st_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH |
                        S_IWUSR | S_IWGRP | S_IWOTH;
            st->st_mode &= ~S_IFREG;
            st->st_mode |= S_IFDIR | S_IXUSR | S_IXGRP | S_IXOTH;
            return RT_EOK;
        }
        
        /* 其他目录 or 文件 */
        /* 暂时单目录 */

        //如果在env中找到了这个"path"也就是文件,那么返回文件. 如果没找到 那么返回错误"文件不存在"
        if(find_ok)
        {
            st->st_size = root_dirent->env.value_len;
            return RT_EOK;

        }

        /* 拓展项 */

        //if( find_env("test",))

        //获取的文件信息 也就是app的信息

        //find_env root_dirent->env_dev path+1 &env

        //更改文件size
        //更改文件属性 mode 只读等等



    }
    return -RT_ERROR;

}


/**
 * XIPFS 文件系统下 文件的操作 打开
*/
int dfs_xipfs_open(struct dfs_fd *fd){
    root_dirent_t root_dirent;
    struct dfs_filesystem *fs = (struct dfs_filesystem *)fd->data; 

    // root_dirent = get_env_by_dev(fs->dev_id);


    // //暂时存在着溢出
    // //root_dirent = rt_malloc(sizeof(root_dirent));

    // rt_kprintf("XIPFS OPEN\n");

    // //取或操作
    // //如果打开的是根目录,那么->false
    // //如果打开的操作为目录,那么false
    // //上述 不能打开目录

    // //伪造 测试
    // root_dirent->env.addr.start = -1;//初始化
    // root_dirent->sec_addr = -1;//初始化
    // fd->pos = 0;
    // fd->data = root_dirent;//(私有数据)
    // fd->size = 0;
    // return RT_EOK;

    //打开操作flag = 0
    //下面的条件还是有问题的..
    //if( !fd->path[1] ||  fd->flags & O_DIRECTORY || fd->flags == 0x0)//获得一个能表示文件在文件系统中位置的文件描述符
    if( !fd->path[1] ||  !(fd->flags & O_PATH))//获得一个能表示文件在文件系统中位置的文件描述符
    {
        root_dirent = get_env_by_dev(fs->dev_id);

        if(root_dirent)
        {
            //通过flags区分文件和文件夹
            //但是
            if(!fd->path[1] || (find_env(fd->path + 1, &root_dirent->env) && (fd->flags == 0x0 || root_dirent->env.value_len == 1)))
            {
                //find_env root_dirent->env_dev file->path+1 &root_dirent->dev
                root_dirent->env.addr.start = -1;
                root_dirent->sec_addr = -1;
                fd->pos = 0;
                fd->data = root_dirent;
                fd->size = 0;
                return RT_EOK;
            }
        }else{
            LOG_E("can't find dev_id!");
        }
    }


    if(fd->flags & O_DIRECTORY)
    {
        //文件夹形式
        if(fd->flags & O_CREAT)
        {
            //新建文件夹.O_CREAT 若此文件夹不存在则创建它
            //mkdir
            return RT_EOK;
        }
        //打开文件夹的操作?
        if((fd->path[0] == '/' && !fd->path[1])\
            || (find_env(fd->path + 1, &root_dirent->env) && root_dirent->env.value_len == 1))
            return RT_EOK;
        else
            return -ENOENT;//无
    }else{
        //文件形式
        bool find_ok = find_env(fd->path + 1, &root_dirent->env);
        if(find_ok && root_dirent->env.value_len == 1)
        {
            //这是一个文件夹
            return -ENOENT;
        }

        if(find_ok == false)
        {
            //没有找到这样的文件

            if(fd->flags & O_CREAT || fd->flags & O_WRONLY)
            {
                //新建模式,判断是否有内存之类的
            }else{
                return -ENOENT;
            }
        }

        if(fd->flags & O_TRUNC)
        {
            //删除or重写
        }
    }

    fd->data = root_dirent;
    fd->size = 0;
    if(fd->flags & O_APPEND)
    {
        //
    }else{
        //
    }
    
    fd->pos = 0;
    return RT_EOK;
}

int dfs_xipfs_close(struct dfs_fd *file)
{
    //主要是销毁文件资源 free
    //要看看在这里有没有文件资源呢

    rt_mutex_take(ef_write_lock, 0);
    
    if(ef_write_event)
    {
        //ef_buf_count = 0;
        rt_event_send(ef_write_event, 1u << 1);
    }

    rt_mutex_release(ef_write_lock);
    rt_thread_delay(1u);

    return RT_EOK;
}

extern int syscall(int number, ...);
void lwp_test_main(uint8_t argc, char **argv)
{
    rt_kprintf("syscall APP test begin\n");
    char txt[] = "syscall\n";
    //syscall(0xff, txt, sizeof(txt));//无法正常返回
    //syscall(1,1);//似乎是正常使用
    rt_kprintf("syscall APP delay 1000ms\n");
    rt_thread_mdelay(1000);
    rt_kprintf("syscall APP test end\n");


}

extern void ef_get_remainSive(ef_env_dev_t dev,size_t *remain_size);
/**
 * XIPFS IO操作
 * 根据CMD命令字进行系统级的一些操作[获取剩余存储大小 / ]
 * 返回的代码需要规范化
**/
int dfs_xipfs_ioctl(struct dfs_fd *fd, int cmd, void *args)
{
    ef_env_dev_t env_dev;
    //根据命令字cmd
    switch (cmd)
    {
    case 0x0000:
        env_dev = (ef_env_dev_t)fd->data;
        if(env_dev)
        {
            //size_t remain_size = 0;
            //获取remain_size 剩余大小
            ef_get_remainSive(env_dev, (size_t *)args);
            return RT_EOK;
        }
        break;

    //查找入口
    case 0x0001:
        env_dev = (ef_env_dev_t)fd->data;
        if(env_dev)
        {
            //那么find_env
            //find_env(env_dev, file->path+1,)
            struct env_meta_data env;
            if(find_env(fd->path + 1, &env) == false)
            {
                return -ENOENT;
            }
            //查找start_addr
            //把start_addr 放入 arg 返回
            //*(uint32_t *)args = env.addr.value;
            uint32_t* entry = args;
            *entry = (uint32_t)lwp_test_main;
            
            return RT_EOK;
            
        }
        break;
    
    //查找入口变量
    case 0x0002:
        env_dev = (ef_env_dev_t)fd->data;
        if(env_dev)
        {
            //那么find_env
            //find_env(env_dev, file->path+1,)
            env_meta_data_t env = args;
            
            if(find_env(fd->path + 1, env) == false)
            {
                return -ENOENT;
            }
            //查找start_addr
            //把start_addr 放入 arg 返回
            //*(uint32_t *)args = env.addr.value;
            rt_kprintf("find ENV %s\n",env->name);
            
            return RT_EOK;
            
        }
        break;
    
    //查找文件大小
    case 0x0003:
        env_dev = (ef_env_dev_t)fd->data;
        if(env_dev)
        {
            //那么find_env
            //find_env(env_dev, file->path+1,)
            env_meta_data_t env = args;
            
            if(find_env(fd->path + 1, env) == false)
            {
                return -ENOENT;
            }
            *(int *)args = env->len;
            return RT_EOK;
        }
        break;
    default:
        //命令字错误
        return -ENOSYS;
        break;
    }

    LOG_E("can't find data!");
    return -ENOENT;
}

extern size_t ef_get_env_stream(const char *key, size_t offset, void *value_buf, size_t buf_len, size_t *saved_value_len);
int dfs_xipfs_read(struct dfs_fd *fd, void *buf, size_t len)
{
    size_t save_size;
    rt_size_t length;
    //int result;
    //result = ef_get_env_blob()

    //从file->pos开始读 len位,如果len超过文件size,那么需要处理

    length = ef_get_env_stream(fd->path + 1, fd->pos, buf, len, &save_size);
    //实现主要是 根据env_dev->flash 读取 n字节到buf中!?

    //READ功能主要是在读取一些字节信息的时候用到

    //pos指针移动length长度
    fd->pos += length;

    return length;
}


/**
 * XIPFS 线程写入函数实体<删除操作>
 **/
void xipfs_write_entry(struct dfs_fd* fd)
{
    if(ef_set_env_blob(fd->path + 1, NULL, fd->size))
    {
        //出现错误 发送事件
        rt_event_send(ef_write_event, 1u << 1);
    }else{
        //
        rt_event_delete(ef_write_event);
        ef_write_event = 0;
    }
}

int dfs_xipfs_write(struct dfs_fd *fd, const void *buf, size_t len)
{
    ef_env_dev_t env_dev;
    rt_thread_t tid;
    int result = RT_EOK;

    //fd->path;

    //把类型强制转换
    env_dev = (ef_env_dev_t)fd->data;

    //取得锁的控制权
    if( !rt_mutex_take(ef_write_lock, 0) )
    {
        //无错误 那么取得了控制权

        if(fd->size > 0)
        {
            if(!ef_write_event)
            {
                ef_write_event = rt_event_create("ef_e", 0);

                if(!ef_write_event || !(tid = rt_thread_create("write_t", xipfs_write_entry, fd, 0x1000u, 5, 12)))
                {
                    //创建不了 那么是有错误的 内存不足
                    result = -RT_ENOMEM;
                    goto write_end;
                }
                //开辟一个内核线程用来写入
                rt_thread_startup(tid);

            }
            //存在ef_write_event事件
            rt_event_send(ef_write_event,1u << 1);
            
            rt_uint32_t recv;
            //等待接收
            if(rt_event_recv(ef_write_event,1u << 1,RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,0,&recv))
            {
                //报错了
            }
            if(recv & 1u << 2)
            {
                //事件2
            }
            if(recv & 1u << 1)
            {
                //事件1
                rt_event_delete(ef_write_event);
                ef_write_event = RT_NULL;
            }

        }else{
            //size == 0 文件夹形式 那么写入环境变量!?
            int errCode = ef_set_env_blob(fd->path + 1, buf, len);
            if(!errCode)
            {
                //无错误,返回写入的字节数
                result = len;
                goto write_end;
            }else if(errCode == EF_ENV_FULL)//EF_ENV_FULL
            {
                result = -RT_EFULL;
                goto write_end;
            }else{
                result = -RT_ERROR;
                goto write_end;
            }
        }

    }

write_end:
    //释放控制权
    rt_mutex_release(ef_write_lock);

    return result;
}

extern bool env_get_fs_getdents(env_meta_data_t env, uint32_t* sec_addr_p);
/**
 * XIPFS get directory entry
 * 似乎是获取目录入口
 * https://linux.die.net/man/2/getdents
 * The system call getdents() reads several linux_dirent structures from
 * the directory referred to by the open file descriptor fd into the
 * buffer pointed to by dirp.  The argument count specifies the size of
 * that buffer.
 * @param count count为dirp中返回的数据量!也就是造了几个这玩意
 * @return 剩余长度 当为0说明没有剩余内容 On success, the number of bytes read is returned.
 * @bug 在子文件夹中没有判断逻辑，导致ls出错
**/
int dfs_xipfs_getdents(struct dfs_fd *fd, struct dirent *dirp, uint32_t count)
{
    root_dirent_t dir;
    rt_uint32_t index;
    struct dirent *d;
    //uint32_t sec_addr = 0xFFFFFFFF;

    //getdents may convert from the native format to fill the linux_dirent.

    /* make integer count */
    count = (count / sizeof(struct dirent)) * sizeof(struct dirent);
    if (count == 0)
        return -EINVAL;
    //标志位,可以换成for循环里面的
    index = 0;
    //取出fd中的私有数据,转换成我们要的root_dirent
    dir = (root_dirent_t) fd->data;

    while(1)
    {
        //递推当前的d<递推式>
        d = dirp + index;

        //遍历 转换出需要的大小

        //从env_dev中获取目录信息 input: dir
        //env_get_fs_getdents
        if( env_get_fs_getdents(&dir->env, &dir->sec_addr) == false)
            return 0;//新增一个跳出条件,用来测试
        //rt_kprintf("sector_addr 0x%x\n", sec_addr);

        //返回的类型 d 是当前的 dirP<item>
        d->d_type = DT_REG;//文件
        d->d_namlen = dir->env.name_len;//名字长度
        d->d_reclen = (rt_uint16_t)sizeof(struct dirent);//长度 16表示子目录或文件,24表示非子目录
        rt_strncpy(d->d_name, dir->env.name, dir->env.name_len);//拷贝名字
        d->d_name[d->d_namlen] = 0;//添加结束符

        index ++;
        //跟传入getdents函数中的count有关,逻辑就是返回n个struct dirent
        if(index * sizeof(struct dirent) >= count)
            break;
    }

    //index = 0的情况是在执行循环体前出错 这时候没有数据,输出错误
    if (index == 0)
        return -RT_ERROR;


    //pos递增 这里需要吗?
    //file->pos += index * sizeof(struct dirent);

    //On success, the number of bytes read is returned.
    //On end of directory, 0 is returned.
    //On error, -1 is returned, and errno is set appropriately.
    return index * sizeof(struct dirent);
}

/**
 * XIP下 文件操作方法
**/
static const struct dfs_file_ops _dfs_xip_fops =
{
    dfs_xipfs_open,
    dfs_xipfs_close,
    dfs_xipfs_ioctl,
    dfs_xipfs_read,
    dfs_xipfs_write,
    RT_NULL,//dfs_elm_flush,//把缓存刷新到存储介质中
    RT_NULL,//dfs_elm_lseek,//更改pos的偏移量
    dfs_xipfs_getdents,
    RT_NULL, /* poll interface */
};

static const struct dfs_filesystem_ops _dfs_xipfs =
{
    "xipfs",
    DFS_FS_FLAG_DEFAULT,
    &_dfs_xip_fops,

    dfs_xipfs_mount,// 挂载操作, 实际上是把带有文件系统(已mkfs)的设备device附加到dir上, 然后我们就可以通过访问dir来访问这个设备.
    dfs_xipfs_unmount,
    dfs_xipfs_mkfs,//用于全片擦除后创建文件系统,只是存储器介质上有这个文件系统? 其参数为 dev_id 是存储器的id
    dfs_xipfs_statfs,

    dfs_xipfs_unlink,
    dfs_xipfs_stat,
    dfs_xipfs_rename,
};


int dfs_xipfs_init(void)
{
    /* register ram file system */
    dfs_register(&_dfs_xipfs);

    rt_kprintf("DFS_XipFS initializated!\n");


    ef_write_lock = rt_mutex_create("xipfs_lock", 1);
    ef_write_event = rt_event_create("xipfs_event", 1);

    return 0;
}

INIT_COMPONENT_EXPORT(dfs_xipfs_init);
