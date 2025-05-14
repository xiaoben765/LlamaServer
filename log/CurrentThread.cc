// filepath: /home/shl203/kama-webserver/log/CurrentThread.cc
#include "CurrentThread.h"
#include <sys/types.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef SYS_gettid
#define SYS_gettid 186 // 如果未定义 SYS_gettid，则定义为 186
#endif

namespace CurrentThread {
    thread_local int t_cachedTid = 0;

    void cacheTid() {
        if (t_cachedTid == 0) {
            t_cachedTid = static_cast<pid_t>(::syscall(SYS_gettid));
        }
    }
}