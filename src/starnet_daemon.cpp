#include "starnet_daemon.h"

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/file.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

//守护进程化（对齐 skynet_daemon.c）：
//  check_pid 防重复运行 → daemon(1,1)（fork+setsid 后台化）→ flock 独占写 pidfile → stdio 重定向 /dev/null

//检查 pidfile 中是否有存活进程；有则返回其 pid，无返回 0
static int check_pid(const char* pidfile) {
    int pid = 0;
    FILE* f = fopen(pidfile, "r");
    if(f == NULL) {
        return 0;
    }
    int n = fscanf(f, "%d", &pid);
    fclose(f);

    if(n != 1 || pid == 0 || pid == getpid()) {
        return 0;
    }

    if(kill(pid, 0) && errno == ESRCH) {
        return 0;
    }

    return pid;
}

//flock 独占锁 + 写当前 pid；成功返回 pid，失败返回 0
static int write_pid(const char* pidfile) {
    FILE* f;
    int pid = 0;
    int fd = open(pidfile, O_RDWR | O_CREAT, 0644);
    if(fd == -1) {
        fprintf(stderr, "Can't create pidfile [%s].\n", pidfile);
        return 0;
    }
    f = fdopen(fd, "w+");
    if(f == NULL) {
        fprintf(stderr, "Can't open pidfile [%s].\n", pidfile);
        return 0;
    }

    if(flock(fd, LOCK_EX | LOCK_NB) == -1) {
        int n = fscanf(f, "%d", &pid);
        fclose(f);
        if(n != 1) {
            fprintf(stderr, "Can't lock and read pidfile.\n");
        }
        else {
            fprintf(stderr, "Can't lock pidfile, lock is held by pid %d.\n", pid);
        }
        return 0;
    }

    pid = getpid();
    if(!fprintf(f, "%d\n", pid)) {
        fprintf(stderr, "Can't write pid.\n");
        close(fd);
        return 0;
    }
    fflush(f);

    return pid;
}

//stdin/stdout/stderr 重定向到 /dev/null（daemon 后台无终端）
static int redirect_fds() {
    int nfd = open("/dev/null", O_RDWR);
    if(nfd == -1) {
        perror("Unable to open /dev/null: ");
        return -1;
    }
    if(dup2(nfd, 0) < 0) {
        perror("Unable to dup2 stdin(0): ");
        return -1;
    }
    if(dup2(nfd, 1) < 0) {
        perror("Unable to dup2 stdout(1): ");
        return -1;
    }
    if(dup2(nfd, 2) < 0) {
        perror("Unable to dup2 stderr(2): ");
        return -1;
    }

    close(nfd);

    return 0;
}

//守护进程化（pidfile 非空时后台运行）。成功返回 0；失败返回 1
//注意：daemon(1,1) 即 fork + setsid，且不 chdir/不关 fd（stdio 随后由 redirect_fds 处理）
int starnet_daemon_init(const char* pidfile) {
    int pid = check_pid(pidfile);

    if(pid) {
        fprintf(stderr, "Starnet is already running, pid = %d.\n", pid);
        return 1;
    }

    if(daemon(1, 1)) {
        fprintf(stderr, "Can't daemonize.\n");
        return 1;
    }

    pid = write_pid(pidfile);
    if(pid == 0) {
        return 1;
    }

    if(redirect_fds()) {
        return 1;
    }

    return 0;
}

//退出时删除 pidfile
int starnet_daemon_exit(const char* pidfile) {
    return unlink(pidfile);
}
