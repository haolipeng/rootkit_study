#include <linux/module.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/dcache.h>
#include "samples-trace.h"

//定义 struct tracepoint _tracepoint_subsys_event 结构体
//定义跟踪点：subsys_event
DEFINE_TRACE(subsys_event);

struct proc_dir_entry *pentry_sample;

/*
 * Here the caller only guarantees locking for struct file and struct inode.
 * Locking must therefore be done in the probe to use the dentry.
 */
//跟踪点回调函数 probe1
static void probe_subsys_event_1(void *ignore, struct inode *inode, struct file *file)
{
	path_get(&file->f_path);
	dget(file->f_path.dentry);
	printk(KERN_INFO "Event is encountered with filename: %s\n", file->f_path.dentry->d_name.name);
	dput(file->f_path.dentry);
	path_put(&file->f_path);
}

/*
 * Here the caller only guarantees locking for struct file and struct inode.
 * Locking must therefore be done in the probe to use the dentry.
 */
//跟踪点回调函数 probe2
static void probe_subsys_event_2(void *ignore, struct inode *inode, struct file *file)
{
	printk(KERN_INFO "Event is encountered with inode number: %lu\n", inode->i_ino);
}

// /proc/tracepoint-sample 文件的 open操作
static int sample_open(struct inode *inode, struct file *file)
{

    printk(KERN_INFO "open /proc/tracepoint-sample file\n");

    //调用跟踪点：subsys_event
	trace_subsys_event(inode, file);

	return -EPERM;
}

static int sample_release(struct inode *inode, struct file *file)
{

    printk(KERN_INFO "release /proc/tracepoint-sample file\n");

    //调用跟踪点：subsys_event
	trace_subsys_event(inode, file);

	return -EPERM;
}

//初始化 /proc/tracepoint-sample 文件的 open操作
static const struct file_operations mark_ops = {
    .owner = THIS_MODULE,
    .open = sample_open,
    .release = sample_release,
};

static int __init sample_init(void)
{
    int ret;

	printk(KERN_ALERT "sample init\n");

    //调用 proc_create 结构在 procfs 文件系统下创建 /proc/tracepoint-sample 文件
	pentry_sample = proc_create("tracepoint-sample", 0644, NULL, &mark_ops);
	if (!pentry_sample)
		return -EPERM;

    //给跟踪点：subsys_event 注册回调函数 probe1:probe_subsys_event_1
	ret = register_trace_subsys_event(probe_subsys_event_1, NULL);
	WARN_ON(ret);

    //给跟踪点：subsys_event 注册回调函数 probe2:probe_subsys_event_2
    ret = register_trace_subsys_event(probe_subsys_event_2, NULL);
	WARN_ON(ret);

	return 0;
}

static void __exit sample_exit(void)
{
	printk(KERN_ALERT "sample exit\n");

    unregister_trace_subsys_event(probe_subsys_event_1, NULL);

    unregister_trace_subsys_event(probe_subsys_event_2, NULL);
    
	remove_proc_entry("tracepoint-sample", NULL);
}

module_init(sample_init)
module_exit(sample_exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tracepoint sample");
