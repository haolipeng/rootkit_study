#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>    /* printk() */
#include <linux/sched.h>
#include <linux/fs.h>        /* 文件操作相关 */
#include <linux/file.h>
#include <linux/uaccess.h>   /* copy_to_user */

MODULE_LICENSE("GPL");

#define FILE_PATH "/tmp/touchX.txt"
#define CONTENT "hello world,my name is haolipeng.\n"

/* 写入文件的函数 */
static int write_file(const char *path, const char *content)
{
    struct file *fp;
    loff_t pos = 0;
    int ret;

    /* 打开文件，如果不存在则创建 */
    fp = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(fp)) {
        printk(KERN_ERR "Failed to open file %s, error %ld\n", 
               path, PTR_ERR(fp));
        return PTR_ERR(fp);
    }

    /* 写入内容 */
    ret = kernel_write(fp, content, strlen(content), &pos);
    if (ret < 0) {
        printk(KERN_ERR "Failed to write file %s, error %d\n", 
               path, ret);
    } else {
        printk(KERN_INFO "Successfully wrote %d bytes to %s\n", 
               ret, path);
    }

    /* 关闭文件 */
    filp_close(fp, NULL);
    return ret;
}

static __init int test_driver_init(void)
{
    int result = 0;
    char cmd_path[] = "/usr/bin/touch";
    char* cmd_argv[] = {cmd_path, FILE_PATH, NULL};
    char* cmd_envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL};

    /* 创建文件 */
    result = call_usermodehelper(cmd_path, cmd_argv, cmd_envp, UMH_WAIT_PROC);
    printk(KERN_DEBUG "test driver init exec! there result of call_usermodehelper is %d\n", result);
    printk(KERN_DEBUG "test driver init exec! the process is \"%s\", pid is %d.\n",
           current->comm, current->pid);

    /* 写入内容 */
    if (result == 0) {
        result = write_file(FILE_PATH, CONTENT);
        if (result < 0) {
            printk(KERN_ERR "Failed to write content to file\n");
        }
    }

    return result;
}

static __exit void test_driver_exit(void)
{
    int result = 0;
    char cmd_path[] = "/bin/rm";
    char* cmd_argv[] = {cmd_path, FILE_PATH, NULL};
    char* cmd_envp[] = {"HOME=/", "PATH=/sbin:/bin:/usr/bin", NULL};

    result = call_usermodehelper(cmd_path, cmd_argv, cmd_envp, UMH_WAIT_PROC);
    printk(KERN_DEBUG "test driver exit exec! the result of call_usermodehelper is %d\n", result);
    printk(KERN_DEBUG "test driver exit exec! the process is \"%s\", pid is %d \n", 
           current->comm, current->pid);
}

module_init(test_driver_init);
module_exit(test_driver_exit);
