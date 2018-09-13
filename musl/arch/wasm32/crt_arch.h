#ifndef SHARED

#include <alloca.h>

void _start_c(int argc,char** argv,char** envp);

extern size_t __wavix_get_num_args();
extern size_t __wavix_get_arg_length(size_t argIndex);
extern void __wavix_get_arg(size_t argIndex,char* buffer,size_t numCharsInBuffer);

void _start(void) {
	// Query the number of arguments and allocate the argument vector.
	const long numArgs = __wavix_get_num_args();
	char** args = (char**)alloca(sizeof(char*) * (numArgs + 1));
  args[numArgs] = 0;

  for(size_t argIndex = 0;argIndex < numArgs;++argIndex) {
  	// Query the length of the argument string, and allocate memory for it.
  	const size_t numChars = __wavix_get_arg_length(argIndex);
  	args[argIndex] = (char*)alloca(numChars);

  	// Write the argument string to the memory allocated for it.
  	__wavix_get_arg(argIndex,args[argIndex],numChars);
  }

  // Pass the arguments to the _start_c function.
  char* envp[1] = { 0 };
  _start_c((int)numArgs,args,envp);
}
#endif
