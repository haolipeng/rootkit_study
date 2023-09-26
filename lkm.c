/*lkm.c*/
 
#include <linux/module.h>    
#include <linux/kernel.h>   
#include <linux/init.h>        

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TheXcellerator");
MODULE_DESCRIPTION("Basic Kernel Module");
MODULE_VERSION("0.01");

static int lkm_init(void)
{
    printk("Arciryas:module loaded\n");
    return 0;    
}
 
static void lkm_exit(void)
{
    printk("Arciryas:module removed\n");
}
 
module_init(lkm_init);
module_exit(lkm_exit);
