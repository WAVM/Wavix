#include <features.h>

#define START "_start"

#include "crt_arch.h"

int main(int,char *argv[]);
void _init(void) __attribute__((weak));
void _fini(void) __attribute__((weak));
_Noreturn int __libc_start_main(int (*)(int,char **), int, char **, char **,
	void (*)(), void(*)());

void _start_c(int argc,char *argv[],char *envp[])
{
	__libc_start_main(main, argc, argv, envp, _init, _fini);
}
