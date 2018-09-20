#define __SYSCALL_LL_E(x) \
((union { long long ll; long l[2]; }){ .ll = x }).l[0], \
((union { long long ll; long l[2]; }){ .ll = x }).l[1]
#define __SYSCALL_LL_O(x) 0, __SYSCALL_LL_E((x))

#define __SC_socket      1
#define __SC_bind        2
#define __SC_connect     3
#define __SC_listen      4
#define __SC_accept      5
#define __SC_getsockname 6
#define __SC_getpeername 7
#define __SC_socketpair  8
#define __SC_send        9
#define __SC_recv        10
#define __SC_sendto      11
#define __SC_recvfrom    12
#define __SC_shutdown    13
#define __SC_setsockopt  14
#define __SC_getsockopt  15
#define __SC_sendmsg     16
#define __SC_recvmsg     17
#define __SC_accept4     18
#define __SC_recvmmsg    19
#define __SC_sendmmsg    20

long __invalid_syscall(long n, long a, long b, long c, long d, long e, long f);

#define __WORKAROUND_NO_ARGUMENTS 0
#define __WORKAROUND_NO_PARAMETERS long

long __syscall_fork(__WORKAROUND_NO_PARAMETERS);
long __syscall_execve(long,long,long);
long __syscall_kill(long,long);
long __syscall_rename(long,long);
long __syscall_umask(long);
long __syscall_setrlimit(long,long);
long __syscall_getrlimit(long,long);
long __syscall_socketcall(long,long);
long __syscall_setitimer(long,long,long);
long __syscall_sysinfo(long);
long __syscall_ugetrlimit(long,long);
long __syscall_stat64(long,long);
long __syscall_lstat64(long,long);
long __syscall_fstat64(long,long);
long __syscall_sched_getaffinity(long,long,long);
long __syscall_prlimit64(long,long,long,long);

long __syscall_unlink(long);
long __syscall_chdir(long);
long __syscall_getpid(__WORKAROUND_NO_PARAMETERS);
long __syscall_access(long,long);
long __syscall_dup(long);
long __syscall_pipe(long);
long __syscall_dup2(long,long);
long __syscall_getppid(__WORKAROUND_NO_PARAMETERS);
long __syscall_getrusage(long,long);
long __syscall_gettimeofday(long,long);
long __syscall_readlink(long,long,long);
long __syscall_wait4(long,long,long,long);
long __syscall_uname(long);
long __syscall_rt_sigaction(long,long,long);
long __syscall_geteuid32(__WORKAROUND_NO_PARAMETERS);
long __syscall_getegid32(__WORKAROUND_NO_PARAMETERS);
long __syscall_setreuid32(long,long);
long __syscall_setregid32(long,long);
long __syscall_getgroups32(long,long);
long __syscall_chown32(long,long,long);
long __syscall_getdents64(long,long,long);
long __syscall_clock_gettime(long,long);
long __syscall_tgkill(long,long,long);
long __syscall_faccessat(long,long,long,long);

long __syscall_membarrier(__WORKAROUND_NO_PARAMETERS);
long __syscall_exit(long);
long __syscall_read(long,long,long);
long __syscall_write(long,long,long);
long __syscall_open(long,long,long);
long __syscall_openat(long,long,long,long);
long __syscall_creat(long,long);
long __syscall_readv(long,long,long);
long __syscall_writev(long,long,long);
long __syscall_fsync(long);
long __syscall_fdatasync(long);
long __syscall_futex(long,long,long,long,long,long);
long __syscall_mmap(long,long,long,long,long,long);
long __syscall_munmap(long,long);
long __syscall_mremap(long,long,long,long,long);
long __syscall_madvise(long,long,long);
long __syscall_brk(long);
long __syscall_poll(long,long,long);
long __syscall_exit_group(long);
long __syscall_gettid(__WORKAROUND_NO_PARAMETERS);
long __syscall_close(long);
long __syscall_ioctl(long,long,long,long,long,long);
long __syscall_llseek(long,long,long,long,long);
long __syscall_fcntl64(long,long,long);
long __syscall_getuid32(__WORKAROUND_NO_PARAMETERS);
long __syscall_getgid32(__WORKAROUND_NO_PARAMETERS);

long __syscall_pselect6(long,long,long,long,long,long);
long __syscall__newselect(long,long,long,long,long);

long __syscall_rt_sigprocmask(long a, long b, long c);
long __syscall_tkill(long a, long b);

static __attribute__((always_inline)) long __syscall_dispatch(long n, long a, long b, long c, long d, long e, long f)
{
	switch(n)
	{
	case __NR_fork: return __syscall_fork(__WORKAROUND_NO_ARGUMENTS);
	case __NR_execve: return __syscall_execve(a, b, c);
	case __NR_kill: return __syscall_kill(a, b);
	case __NR_rename: return __syscall_rename(a, b);
	case __NR_umask: return __syscall_umask(a);
	case __NR_setrlimit: return __syscall_setrlimit(a, b);
	case __NR_getrlimit: return __syscall_getrlimit(a, b);
	case __NR_socketcall: return __syscall_socketcall(a, b);
	case __NR_setitimer: return __syscall_setitimer(a, b, c);
	case __NR_sysinfo: return __syscall_sysinfo(a);
	case __NR_ugetrlimit: return __syscall_ugetrlimit(a, b);
	case __NR_stat64: return __syscall_stat64(a, b);
	case __NR_lstat64: return __syscall_lstat64(a, b);
	case __NR_fstat64: return __syscall_fstat64(a, b);
	case __NR_sched_getaffinity: return __syscall_sched_getaffinity(a, b, c);
	case __NR_prlimit64: return __syscall_prlimit64(a, b, c, d);
	case __NR_unlink: return __syscall_unlink(a);
	case __NR_chdir: return __syscall_chdir(a);
	case __NR_getpid: return __syscall_getpid(__WORKAROUND_NO_ARGUMENTS);
	case __NR_access: return __syscall_access(a, b);
	case __NR_dup: return __syscall_dup(a);
	case __NR_pipe: return __syscall_pipe(a);
	case __NR_dup2: return __syscall_dup2(a, b);
	case __NR_getppid: return __syscall_getppid(__WORKAROUND_NO_ARGUMENTS);
	case __NR_getrusage: return __syscall_getrusage(a, b);
	case __NR_gettimeofday: return __syscall_gettimeofday(a, b);
	case __NR_readlink: return __syscall_readlink(a, b, c);
	case __NR_wait4: return __syscall_wait4(a, b, c, d);
	case __NR_uname: return __syscall_uname(a);
	case __NR_rt_sigaction: return __syscall_rt_sigaction(a, b, c);
	case __NR_geteuid32: return __syscall_geteuid32(__WORKAROUND_NO_ARGUMENTS);
	case __NR_getegid32: return __syscall_getegid32(__WORKAROUND_NO_ARGUMENTS);
	case __NR_setreuid32: return __syscall_setreuid32(a, b);
	case __NR_setregid32: return __syscall_setregid32(a, b);
	case __NR_getgroups32: return __syscall_getgroups32(a, b);
	case __NR_chown32: return __syscall_chown32(a, b, c);
	case __NR_getdents64: return __syscall_getdents64(a, b, c);
	case __NR_clock_gettime: return __syscall_clock_gettime(a, b);
	case __NR_tgkill: return __syscall_tgkill(a, b, c);
	case __NR_faccessat: return __syscall_faccessat(a, b, c, d);
	case __NR_exit: return __syscall_exit(a);
	case __NR_read: return __syscall_read(a, b, c);
	case __NR_write: return __syscall_write(a, b, c);
	case __NR_open: return __syscall_open(a, b, c);
	case __NR_openat: return __syscall_openat(a, b, c, d);
	case __NR_creat: return __syscall_creat(a, b);
	case __NR_readv: return __syscall_readv(a, b, c);
	case __NR_writev: return __syscall_writev(a, b, c);
	case __NR_fsync: return __syscall_fsync(a);
	case __NR_fdatasync: return __syscall_fdatasync(a);
	case __NR_brk: return __syscall_brk(a);
	case __NR_futex: return __syscall_futex(a, b, c, d, e, f);
	case __NR_membarrier: return __syscall_membarrier(__WORKAROUND_NO_ARGUMENTS);
	case __NR_mmap2: return __syscall_mmap(a, b, c, d, e, f);
	case __NR_munmap: return __syscall_munmap(a, b);
	case __NR_mremap: return __syscall_mremap(a, b, c, d, e);
	case __NR_madvise: return __syscall_madvise(a, b, c);
	case __NR_poll: return __syscall_poll(a, b, c);
	case __NR_exit_group: return __syscall_exit_group(a);
	case __NR_tkill: return __syscall_tkill(a, b);
	case __NR_rt_sigprocmask: return __syscall_rt_sigprocmask(a, b, c);
	case __NR_gettid: return __syscall_gettid(__WORKAROUND_NO_ARGUMENTS);
	case __NR_close: return __syscall_close(a);
	case __NR_ioctl: return __syscall_ioctl(a, b, c, d, e, f);
	case __NR__llseek: return __syscall_llseek(a, b, c, d, e);
	case __NR_fcntl64: return __syscall_fcntl64(a, b, c);
	case __NR_getuid32: return __syscall_getuid32(__WORKAROUND_NO_ARGUMENTS);
	case __NR_getgid32: return __syscall_getgid32(__WORKAROUND_NO_ARGUMENTS);
	case __NR_pselect6: return __syscall_pselect6(a, b, c, d, e, f);
	case __NR__newselect: return __syscall__newselect(a, b, c, d, e);
	default: return __invalid_syscall(n, a, b, c, d, e, f);
	};
}

static __attribute__((always_inline)) long __syscall0(long n)                                                 { return __syscall_dispatch(n, 0, 0, 0, 0, 0, 0); }
static __attribute__((always_inline)) long __syscall1(long n, long a)                                         { return __syscall_dispatch(n, a, 0, 0, 0, 0, 0); }
static __attribute__((always_inline)) long __syscall2(long n, long a, long b)                                 { return __syscall_dispatch(n, a, b, 0, 0, 0, 0); }
static __attribute__((always_inline)) long __syscall3(long n, long a, long b, long c)                         { return __syscall_dispatch(n, a, b, c, 0, 0, 0); }
static __attribute__((always_inline)) long __syscall4(long n, long a, long b, long c, long d)                 { return __syscall_dispatch(n, a, b, c, d, 0, 0); }
static __attribute__((always_inline)) long __syscall5(long n, long a, long b, long c, long d, long e)         { return __syscall_dispatch(n, a, b, c, d, e, 0); }
static __attribute__((always_inline)) long __syscall6(long n, long a, long b, long c, long d, long e, long f) { return __syscall_dispatch(n, a, b, c, d, e, f); }

#define SYSCALL_USE_SOCKETCALL
