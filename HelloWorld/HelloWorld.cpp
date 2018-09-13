#include <stdio.h>
#include <cstdint>
#include <inttypes.h>
#include <atomic>

int main(int argc,char** argv)
{
	printf("args(%u): <%xh>\n",argc,reinterpret_cast<int>(argv));
	for(int argIndex = 0;argIndex <= argc;++argIndex)
	{
		printf(" [%i] <%xh> %s\n",argIndex,reinterpret_cast<int>(argv[argIndex]),argv[argIndex]);
	}

	printf("Hello world!\n");

	printf("Please type a character: ");
	fflush(stdout);
	char c = getchar();
	printf("\nThank you!\n");

	FILE* testFile = fopen("testFile.txt","w");
	if(!testFile)
	{
		printf("Couldn't open testFile.txt!\n");
	}
	else
	{
		fprintf(testFile,"The character pressed was: %c\n",c);
		fclose(testFile);
	}
}
