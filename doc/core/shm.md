# 共享内存(shared memory)
## 印象

轻量级应用(Apps)有自己的堆栈空间，是独立的。

要实现应用间的数据通信，需要使用<b>共享内存</b>技术

## 依赖

适用的内存管理算法

## 结构

应用

    shm_app
        node            关联mem_node
        pid             lwt的addr
        ref_tab         内存地址引用次数动态表
        ref_tab_size    引用次数动态表大小
        use_num         该app使用共享内存的数量

内存

    shm_mem
        node            内存关联的app_node
        addr            内存位置
        size            内存大小

关系表

    shm_relation
        app             指向shm_app
        app_node        节点，属于shm_app.node
        mem             指向shm_mem
        mem_node        节点

## 流程

### 初始化共享内存块

在初始化阶段，向系统申请一段连续且符合MPU要求的内存。

### 相互关系初始化(链表初始化)


## 函数

### 申请共享内存 shm_alloc

在App中通过系统调用进而申请共享内存，需要记录的是当前App的信息，和申请的内存信息。

<b>申请内存过程通过内存管理算法取得具体的地址。</b>

### 申明使用内存 shm_retain

在使用共享内存的App，需要申明使用内存。申明过程，主要是在这段内存上加上此App的信息，以及在此App下加上使用了该App的信息。

### 释放共享内存 shm_free

释放共享内存只是删除此App和此段内存的联系，如果此段内存跟任何App没有关联了，那么释放此段内存及实体。