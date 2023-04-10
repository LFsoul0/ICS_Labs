/*
 * csim.c - a cache simulator 
 *
 * Name: Wang Tingyu
 * ID: 519021910475
 */

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "cachelab.h"

#define MAXLINE 1024

/* args */
int verbose = 0;
int setbit = 1;
long setnum = 2;
long linenum = 1;
int blockbit = 1;
char tracefile[MAXLINE];

/* result */
int hit_count = 0;
int miss_count = 0;
int eviction_count = 0;

/* cache struct */
struct cacheline {
    int valid;
    long tag;
    unsigned long stamp;
};
struct cacheline **cache = NULL;

/*
 * usage - print help message
 */
void usage() 
{
    printf("Usage: shell [-hv] -s <s> -E <E> -b <b> -t <tracefile>\n");
    printf("   -h   print this message\n");
    printf("   -v   display trace info\n");
    printf("   -s   number of set index bits\n");
    printf("   -E   number of lines per set\n");
    printf("   -b   number of block bits\n");
    printf("   -t   name of the valgrind trace to replay\n");
    exit(1);
}

/*
 * init_cache - initialize the cache
 */
void init_cache()
{
    /* malloc sets */
    cache = malloc(sizeof(struct cacheline*) * setnum);
    
    /* malloc lines */
    for (long i = 0; i < setnum; ++i) {
	cache[i] = malloc(sizeof(struct cacheline) * linenum);

	/* init lines */
	for (long j = 0; j < linenum; ++j) {
	    cache[i][j].valid = 0;
	}
    } 
}

/* 
 * free_cache - free the cache
 */
void free_cache()
{
    /* free lines */
    for (int i = 0; i < setnum; ++i) {
	free(cache[i]);
    }

    /* free sets */
    free(cache);
}

/*
 * touch - simulate address visit
 */
void touch(unsigned long address)
{
    /* simulate time stamp */
    static unsigned long curr_stamp = 0;
    ++curr_stamp;

    /* split address */
    long set = (address >> blockbit) & ((0x1ul << setbit) - 0x1);
    long tag = address >> (setbit + blockbit);

    /* find tag in corresponding set */
    for (long i = 0; i < linenum; ++i) {
	/* if hit, update stamp */
	if (cache[set][i].valid && cache[set][i].tag == tag) {
	    cache[set][i].stamp = curr_stamp;
	    ++hit_count;
	    return;
	}
    }
    ++miss_count; /* else miss */ 

    /* find empty line */
    for (long i = 0; i < linenum; ++i) {
	/* if empty */
        if (!cache[set][i].valid) {
	    cache[set][i].valid = 1;
	    cache[set][i].tag = tag;
	    cache[set][i].stamp = curr_stamp;
	    return;
	}
    }
    ++eviction_count;/* else eviction */

    /* find least recently used line */
    unsigned long min_stamp = curr_stamp; 
    long evic_line = 0;
    for (long i = 0; i < linenum; ++i) {
	if (cache[set][i].stamp < min_stamp) {
	    min_stamp = cache[set][i].stamp;
	    evic_line = i;
	}
    }

    /* replace the evicted line */
    cache[set][evic_line].tag = tag;
    cache[set][evic_line].stamp = curr_stamp;
}

/*
 * parse_trace - parse trace and get counts 
 */
void parse_trace()
{
    /* open file */
    FILE *file = fopen(tracefile, "r");
    if (!file) {
	printf("file open failed");
	exit(-1);
    }

    /* trace line args */
    char op;
    unsigned long address;
    int size;

    /* read each line */
    while (fscanf(file, " %c %lx,%d\n", &op, &address, &size) == 3) {
	switch (op) {
	case 'M':		/* if modify, touch twice */
	    touch(address);
	case 'L':
	case 'S':		/* if load or store, touch once */
	    touch(address);
	    break;
	default:		/* else(op == 'I'), ignore */
	    continue;
	}
    }

    /* close file */
    fclose(file);
}

/*
 * main - main routine 
 */
int main(int argc, char **argv)
{
    /* arg flags */
    int f_set = 0, f_line = 0, f_block = 0, f_trace = 0;

    /* parse the command line */
    char c;
    while ((c = getopt(argc, argv, "hvs:E:b:t:")) != EOF) {
        switch (c) {
        case 'h':		/* print help message */  
	    usage();
	    break;
        case 'v':		/* display trace info */  
	    verbose = 1;
	    break;
        case 's':            		/* set index bits */
	    setbit = atoi(optarg);
            setnum = 0x1 << setbit;
	    f_set = 1;
	    break;
	case 'E':			/* lines per set */
	    linenum = atoi(optarg);
	    f_line = 1;
	    break;
	case 'b':			/* block bits */
	    blockbit = atoi(optarg);
	    f_block = 1;
	    break;
	case 't':			/* trace filename */
	    strcpy(tracefile, optarg);
	    f_trace = 1;
	    break;
	default:
            usage();
	}
    }

    /* if arg missing */
    if (!f_set || !f_line || !f_block || !f_trace) {
	usage();
    }

    /* init cache */
    init_cache();

    /* parse trace */
    parse_trace();

    /* free cache */
    free_cache();

    /* print result */
    printSummary(hit_count, miss_count, eviction_count);

    return 0;
}
