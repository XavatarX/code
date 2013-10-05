#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define TOTAL_BYTES	(unsigned int)(2*1000*1000*1000)
#define SEARCH_PERCENT	(1)
#define SEARCH_BYTES	((TOTAL_BYTES/100)*SEARCH_PERCENT)
#define SEARCH_ITERATION (1000)

#define NUMBER_OF_SERCHES (SEARCH_BYTES)

int search(char *  bytes, int * iteration)
{
	int i = 0;
	int rand = 0;
	// reset the random generator
	srandom(random()%random ());
	for (i = 0; i< SEARCH_ITERATION; i++)
	{
		rand = random()%TOTAL_BYTES;
		if (bytes [rand] == 0) {
			*iteration = i;
			return rand;
		}
	}
	return -1;
}

int main ()
{
	unsigned int rand = 0, i= 0;
	int iteration;
	unsigned char *bytes = malloc(TOTAL_BYTES);
	
	if (bytes == NULL) {
		printf ("Unable to allocate memory\n");
		return -1;
	}
	memset(bytes, 1, TOTAL_BYTES);

	srandom(time(NULL)%100000);
	// Set Search percent blocks to 0
	for (i=0;i<SEARCH_BYTES;i++)
	{
		rand = random()%TOTAL_BYTES;
		if (bytes[rand] == 0) {
			i--;
		} else {
			bytes[rand] = 0;
		}
		
	}
	printf ("%d %d \n", TOTAL_BYTES, SEARCH_BYTES);
	for (i=0 ; i<NUMBER_OF_SERCHES; i++) {
		 int  found  = search(bytes, &iteration);
		if (found != -1)
			printf("%d %d %d\n", i, iteration, found);
		else {
			printf("%d -1 -1 -1\n");
		}
	}
	return 0;
}
