# 轻量级线程(LWT)
## 印象
用于用户态App的相关服务。

### LWT与thread的关系
#### lwt_execve
属于同级关系或者上下级关系
##### 在内核级内创建(在shell中)
```
/**
* Thread Group
* <thread> is new object, so thread->sibling is itself
* <lwt> is also new object, so lwt->t_grp is empty(or say point itself)
* Result: lwt->t_grp <=> thread->sibing <=> t_grp
**/
```

##### 在用户态程序中创建

#### lwt: sys_create_thread
属于同级关系
```
/**
* Thread Group
* if we create a new thread by rt_thread_create in a lwt in user APP, the new thread is parallel to old thread.
* t_grp <=> old_thread->sibling ... <=> new_thread_sibling <=> t_grp
* 
* now <thread> point to itself
**/
```
