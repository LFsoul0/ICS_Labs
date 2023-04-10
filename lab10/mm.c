/*
 * mm.c - A simple malloc package based on segregated free list.
 * 
 * Name: Wang Tingyu
 * ID: 519021910475
 *
 * Discription:
 * --Block Structure-- 
 *   Each block has a header and a footer, which occupates 4 bytes each
 * and packs block size and allocated bit. 
 *   For free blocks, a predecessor and a successor pointer is between
 * header and footer, which occupates 4 bytes each and point to its 
 * neighbors in the free list.
 *   A smallest block occupates 16 bytes.
 *
 * --Free List--
 *   Using segregated free list, which has linear collection for every 
 * small size and has collections for each power of 2 for larger sizes.
 *   The free list is managed with LIFO-ordering, first-fit and immediate 
 * coalesce strategy.
 *
 * --Heap Structure--
 *   Leading with the free lists, since definition of compound global
 * data structures is forbidden. Then comes the prologue block, common
 * blocks and epilogue block. All blocks are align to 8.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/* for debug */
//#define TRACE
//#define CHECK
//#define STATE

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* word size and double words size */
#define WSIZE 4
#define DSIZE 8

/* heap extend size */
#define INITCHUNKSIZE (1 << 7)
#define CHUNKSIZE (1 << 12)
#define REALLOCCHUNKSIZE (1 << 6)

/* max and min */
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))

/* pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* read and write a word */
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* read the size and allocated fields */
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/* transform between address and offset */
#define GET_OFFSET(bp) ((char *)bp - (char *)heap_listp)
#define GET_ADDR(offset) (heap_listp + offset)

/* compute address of header, footer, predecessor and successor */
#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define PRED(bp) (bp)
#define SUCC(bp) ((bp) + WSIZE)

/* compute address of previous and next blocks according to address */
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))

/* compute address of previous and next free block in the list */
#define PREV_FREEP(bp) ((GET(PRED(bp)))? (GET_ADDR(GET(PRED(bp)))) : NULL)
#define NEXT_FREEP(bp) ((GET(SUCC(bp)))? (GET_ADDR(GET(SUCC(bp)))) : NULL)

/* length of segregated free list */
#define LISTMAX 16
#define LINEARBIT 5
#define LINEARMAX ((1 << (LINEARBIT - 3)) - 2)

/* the critical value of "big" size */
#define BIGSIZE (96)


/* global variables */
static void *heap_listp;
static void **free_listps;

/* helper functions */
static void *extend_heap(size_t size);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void *place(void *bp, size_t asize);
static void *insert_freeblk(void *bp);
static void *remove_freeblk(void *bp);
static void **get_free_listpp(size_t size);

int mm_check(void);
void mm_state(void);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    #ifdef TRACE
	fprintf(stdout, "INIT\n");
    #endif

    /* create initial empty heap and segregated free list */
    heap_listp = mem_sbrk(ALIGN(LISTMAX * sizeof(void *) - WSIZE) + 4 * WSIZE);
    if (heap_listp == (void *)-1)
	return -1;

    free_listps = (void **)heap_listp; 
    for (int i = 0; i < LISTMAX; ++i) {
	free_listps[i] = NULL;
    }
    
    heap_listp += ALIGN(LISTMAX * sizeof(void *) - WSIZE) + 2 * WSIZE;
    PUT(HDRP(heap_listp), PACK(DSIZE, 1));
    PUT(FTRP(heap_listp), PACK(DSIZE, 1));
    PUT(HDRP(NEXT_BLKP(heap_listp)), PACK(0, 1));

    /* extend the empty heap with a free block */
    if (extend_heap(INITCHUNKSIZE) == NULL)
	return -1;

    return 0;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    #ifdef TRACE
	fprintf(stdout, "MALLOC %ld\n", size);
    #endif
    #ifdef CHECK
	mm_check();
    #endif
    #ifdef STATE
	mm_state();
    #endif

    if (!size)
	return NULL;

    /* find a block of asize */
    size_t asize = ALIGN(size) + DSIZE; 
    void *bp = find_fit(asize);

    /* if not found, extend */
    if (bp == NULL) {
	size_t exsize = MAX(asize, CHUNKSIZE);
	bp = extend_heap(exsize);
	if (bp == NULL)
	    return NULL;
    }

    /* place asize in the block found */
    bp = place(bp, asize);

    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    #ifdef TRACE
	fprintf(stdout, "FREE %ld\n", GET_OFFSET(ptr));
    #endif
    #ifdef CHECK
	mm_check();
    #endif
    #ifdef STATE
	mm_state();
    #endif

    if (!ptr)
	return;

    /* set the allocated bit to 0 */
    size_t size = GET_SIZE(HDRP(ptr));
    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));

    /* coalesce and insert to free list */
    coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    #ifdef TRACE
	fprintf(stdout, "REALLOC %ld %ld\n", GET_OFFSET(ptr), size);
    #endif
    #ifdef CHECK
	mm_check();
    #endif
    #ifdef STATE
	mm_state();
    #endif

    /* if ptr is NULL, just malloc it */
    if(!ptr)
        return mm_malloc(size);
    /* if size is 0, just free it */
    if(!size){
        mm_free(ptr);
        return NULL;
    }

    size_t newsize = ALIGN(size) + DSIZE;	/* new actual size */
    size_t oldsize = GET_SIZE(HDRP(ptr));	/* old block size */
    size_t remainsize = GET_ALLOC(HDRP(NEXT_BLKP(ptr)))? 0 : GET_SIZE(HDRP(NEXT_BLKP(ptr)));	/* remain size after the block */

    /* case: size not change, directly return */
    if (newsize == oldsize)
	return ptr;

    /* case: size change smaller */
    if (newsize < oldsize) {
	/* if remain size < smallest block size, directly return */
	if (oldsize - newsize + remainsize < 16)
	    return ptr;

	/* else place at left side */
	PUT(HDRP(ptr), PACK(newsize, 1));
        PUT(FTRP(ptr), PACK(newsize, 1));
        PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldsize - newsize, 0));
        PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldsize - newsize, 0));
        coalesce(NEXT_BLKP(ptr));
	return ptr;
    }

    /* case: size change larger */
    int can_extend = (remainsize && !GET_SIZE(HDRP(NEXT_BLKP(NEXT_BLKP(ptr))))) || !GET_SIZE(HDRP(NEXT_BLKP(ptr)));

    /* if no enough remain size but can extend, extend and get new remain size */
    if (remainsize < newsize - oldsize && can_extend) {
	extend_heap(MAX(newsize - oldsize - remainsize, REALLOCCHUNKSIZE));
	remainsize = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    }

    /* if remain size enough */
    if (remainsize >= newsize - oldsize) {
	/* if new remain size < smallest block size, simply coalesce */
	if (oldsize + remainsize - newsize < 16) {
	    remove_freeblk(NEXT_BLKP(ptr));
	    PUT(HDRP(ptr), PACK(oldsize + remainsize, 1));
	    PUT(FTRP(ptr), PACK(oldsize + remainsize, 1));
	    return ptr;
	}
	
	/* else place at left side */
	remove_freeblk(NEXT_BLKP(ptr));
	PUT(HDRP(ptr), PACK(newsize, 1));
	PUT(FTRP(ptr), PACK(newsize, 1));
	PUT(HDRP(NEXT_BLKP(ptr)), PACK(oldsize + remainsize - newsize, 0));
	PUT(FTRP(NEXT_BLKP(ptr)), PACK(oldsize + remainsize - newsize, 0));
	insert_freeblk(NEXT_BLKP(ptr));
	return ptr;
    }

    /* else malloc a new block */
    void* newptr = mm_malloc(size);
    if (!newptr) return NULL;
    memcpy(newptr, ptr, oldsize - DSIZE);
    mm_free(ptr);

    return newptr;
}

/* 
 * extend_heap - extend heap to contain larger size
 */
static void *extend_heap(size_t size)
{
    /* allocate an even number of words to maintain alignment */
    size = ALIGN(size);
    char* bp = mem_sbrk(size);
    if((long)bp == -1)
        return NULL;
    
    /* initialize free bock header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0)); /* Free block header */
    PUT(FTRP(bp), PACK(size, 0)); /* Free block footer */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */

    /* coalesce if the previous block was free */
    return coalesce(bp);
}

/* 
 * coalesce - insert current block to free list and coalesce adjecent free blocks
 */
static void *coalesce(void *bp)
{
    void *prev = PREV_BLKP(bp);
    void *next = NEXT_BLKP(bp);
    size_t prev_alloc = GET_ALLOC(FTRP(prev));
    size_t next_alloc = GET_ALLOC(HDRP(next));
    size_t size = GET_SIZE(HDRP(bp));

    /* case: no coalesce */
    if (prev_alloc && next_alloc) {
	insert_freeblk(bp);
    }
    /* case: coalesce with next block */
    else if (prev_alloc && !next_alloc) {
        remove_freeblk(next);
        size += GET_SIZE(HDRP(next));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert_freeblk(bp);
    }
    /* case: coalesce with previous block */
    else if (!prev_alloc && next_alloc) {
        remove_freeblk(prev);
        size += GET_SIZE(HDRP(prev));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(prev), PACK(size, 0));
        insert_freeblk(prev);
        bp = prev;
    }
    /* case: coalesce with both next block and previous block*/
    else {
        remove_freeblk(next);
        remove_freeblk(prev);
        size += GET_SIZE(HDRP(next)) + GET_SIZE(HDRP(prev));
        PUT(HDRP(prev), PACK(size, 0));
        PUT(FTRP(next), PACK(size, 0));
        insert_freeblk(prev);
        bp = prev;
    }

    return bp;
}

/* 
 * find_fit - find befitting free blocks for malloc
 */
void *find_fit(size_t asize)
{
    if (!asize)
	return NULL;

    /* get the first idx to search */
    int idx = get_free_listpp(asize) - free_listps;

    /* search each free list after */
    for (int i = idx; i < LISTMAX; ++i) {
	void *bp = free_listps[i];
	while (bp) {
	    if (GET_SIZE(HDRP(bp)) >= asize)
		return bp;

	    bp = NEXT_FREEP(bp);
	}
    }

    return NULL;
}

/* 
 * place - place the given size to given free block
 */
void *place(void *bp, size_t asize)
{
    size_t size = GET_SIZE(HDRP(bp));
    size_t remain = size - asize;

    /* remove given free block */
    remove_freeblk(bp);

    /* if remain size < smallest size, no division */
    if (remain < 16) {
        PUT(HDRP(bp), PACK(size, 1));
        PUT(FTRP(bp), PACK(size, 1));
        return bp;
    }
    
    /* if big size, place at right side */
    else if (asize > BIGSIZE) {
	PUT(HDRP(bp), PACK(remain, 0));
	PUT(FTRP(bp), PACK(remain, 0));
	void *next = NEXT_BLKP(bp);
	PUT(HDRP(next), PACK(asize, 1));
	PUT(FTRP(next), PACK(asize, 1));
	insert_freeblk(bp);
 	return next;
    }

    /* if small size, place at left size */
    else {
	PUT(HDRP(bp), PACK(asize, 1));
	PUT(FTRP(bp), PACK(asize, 1));
	void *next = NEXT_BLKP(bp);
	PUT(HDRP(next), PACK(remain, 0));
	PUT(FTRP(next), PACK(remain, 0));
	insert_freeblk(next);
 	return bp;
    }
    
}

/* 
 * insert_freeblk - insert given block to suitable free block list
 */
static void *insert_freeblk(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));

    /* get the free list to insert */
    void **free_listpp = get_free_listpp(size);

    /* whether the first block */
    if (*free_listpp) {
	PUT(PRED(*free_listpp), GET_OFFSET(bp));
	PUT(SUCC(bp), GET_OFFSET(*free_listpp));
       	PUT(PRED(bp), 0);
    }
    else {
	PUT(SUCC(bp), 0);
       	PUT(PRED(bp), 0);
    }

    /* set the list pointer */
    *free_listpp = bp;

    return bp;
}

/* 
 * remove_freeblk - remove given block from suitable free block list
 */
static void *remove_freeblk(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    void **free_listpp = get_free_listpp(size);
    void *prev = PREV_FREEP(bp);
    void *next = NEXT_FREEP(bp);

    /* 4 cases accroding to the existence of previous and next block */
    if (prev && next) {
        PUT(SUCC(prev), GET(SUCC(bp)));
        PUT(PRED(next), GET(PRED(bp)));
    } 
    else if (prev && !next) {
        PUT(SUCC(prev), 0);
    } 
    else if (!prev && next) {
        PUT(PRED(next), 0);
        *free_listpp = next;
    } 
    else {
        *free_listpp = NULL;
    }

    return bp;
}

/*
 * get_free_listp - get free list by size
 */
static void **get_free_listpp(size_t size)
{
    /* linear part */
    size_t linear_group = size / DSIZE - 2;
    if (linear_group < LINEARMAX) {
	return &free_listps[linear_group];
    }

    /* power of 2 part */
    size_t search = (size - 1) >> LINEARBIT;
    for (int i = LINEARMAX; i < LISTMAX - 1; ++i) {
	if (!search) 
	    return &free_listps[i];

	search >>= 1;
    }
 
    /* largest ones */
    return &free_listps[LISTMAX - 1];
}

/* 
 * mm_check - check the correctness of code
 */
int mm_check(void)
{
    /* check 1: every block in the free list marked as free */
    for (int i = 0; i < LISTMAX; ++i) {
	void *bp = free_listps[i];
	while (bp) {
	    if (GET_ALLOC(HDRP(bp))) {
		fprintf(stderr, "Allocated blocks in free list!\n");
	        return 0;
	    }
	    bp = NEXT_FREEP(bp);
	}
    }

    /* check 2: all contiguous free blocks are finished coalescing */
    for(void *bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if(!GET_ALLOC(HDRP(bp)) && !GET_ALLOC(HDRP(NEXT_BLKP(bp)))){
            fprintf(stderr, "Contagious free blocks!\n");
            return 0;
        }
    }

    /* check 3: every free block actually in the free list */
    int count = 0;
    for(void *bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if(!GET_ALLOC(HDRP(bp))) {
            ++count;
	}
    }
    for (int i = 0; i < LISTMAX; ++i) {
	void *bp = free_listps[i];
	while (bp) {
	    --count;
	    bp = NEXT_FREEP(bp);
	}
    }
    if(count){
        fprintf(stderr, "Not all free blocks in free list!\n");
        return 0;
    }

    /* check 4: all the pointers in the free list point to valid free blocks */
    for (int i = 0; i < LISTMAX; ++i) {
	if (!free_listps[i]) continue;

	if (GET(PRED(free_listps[i]))) {
	    fprintf(stderr, "FreeList header pred not NULL!\n");
	    return 0;
	}

	void *bp = free_listps[i];
	while (bp) {
	    if (GET(SUCC(bp))) {
		if (GET(PRED(NEXT_FREEP(bp))) != GET_OFFSET(bp) || GET(SUCC(bp)) != GET_OFFSET(NEXT_FREEP(bp))) {
		    fprintf(stderr, "Adjacent free blocks unmatched!\n");
	            return 0;
		}
	    }
	    bp = NEXT_FREEP(bp);
	}
    }

    return 1;
}

/* 
 * mm_state - show blocks status of heap
 */
void mm_state(void){
    void *bp = NULL;
    int count = 1;
    
    fprintf(stdout,"##### NO\tOFF \tSIZE \tALLOC \tPRED \tSUCC\n");
    for(bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        if(!GET_ALLOC(HDRP(bp)))
            fprintf(stdout,"BLOCK %d \t%d \t%d \t%d \t%d \t%d\n", count, (unsigned int)GET_OFFSET(bp), GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)), GET(PRED(bp)), GET(SUCC(bp)));
        else
            fprintf(stdout,"BLOCK %d \t%d \t%d \t%d \t-- \t--\n", count, (unsigned int)GET_OFFSET(bp), GET_SIZE(HDRP(bp)), GET_ALLOC(HDRP(bp)));
        count++;
    }
    return;
}






