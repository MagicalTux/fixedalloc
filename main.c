#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <string.h>
#include "fixedalloc.h"

int main(int argc, char *argv[]) {
	printf("in main, my pid is %d\n", getpid());

	// random alloc/free (randomized with a fair lottery)
	void *t1, *t2, *t3, *t4;
	t1 = malloc_str127();
	t2 = malloc_str127();
	t3 = malloc_str127();
	t4 = malloc_str127();
	free_str127(t2);
	free_str127(t4);
	free_str127(t1);
	free_str127(t3);

	char *str;
	for(int i = 0; i < 600; i++) {
		// store a string in memory
		str = malloc_str127();
		if (str == NULL) break;
		strcpy(str, "This is a string stored in memory!");
		if (i == 0)
			printf("String stored at %p: %s\n", str, str);

		if (i <= 300)
			free_str127(str);

		if (i == 300)
			printf("not freeing memory after that one\n");
	}
	//sleep(300);
	return 0;
}
