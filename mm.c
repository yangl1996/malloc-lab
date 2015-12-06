/*
 * mm.c
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mm.h"
#include "memlib.h"

/* If you want debugging output, use the following macro.  When you hand
 * in, remove the #define DEBUG line. */
/* TODO: remove them before submission */
//#define DEBUG
//#define CHECK
#ifdef DEBUG
# define dbg_printf(...) printf(__VA_ARGS__)
#else
# define dbg_printf(...)
#endif


/* do not change the following! */
#ifdef DRIVER
/* create aliases for driver tests */
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#endif /* def DRIVER */

/* word sizes of x64 */
#define WSIZE 8  /* x64 pointer */
#define HSIZE 4  /* x64 int */
#define DSIZE 16 /* x64 double word */
/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
/* 2^0 to 2^(SAGCOUNT-1) sized blocks will have its own list */
/* we will have SAGCOUNT + 1 classes in total */
#define SAGCOUNT 12  /* choose an even number */
/* class n: non-zero MSB at n
   class 0: 1
   class 1: 2-3
   class 2: 4-7
   ...
*/

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(p) (((size_t)(p) + (ALIGNMENT-1)) & ~0x7)

/* calculate actual pointer using heap begin and offset */
#define CPTR(o) ((unsigned int)(o) + heap_begin)

/* calculate offset from pointer and heap begin */
#define COFF(p) ((char*)(p) - heap_begin)

/* write to heap */
#define PUT(p, data) (*(unsigned int*)((CPTR(p))) = (data))

/* read from heap */
#define GET(p) (*(unsigned int*)(CPTR(p)))

/* pack header block */
#define PACK(size, allocated) ((size) | (allocated))

/* get class head offset */
#define CLASS(class) ((unsigned int)((class) * HSIZE))

/* get allocated bit */
#define ALLOCED(head) ((unsigned int)((head) & 0x01))

/* get size */
#define SIZE(head) ((unsigned int)((head) & 0xFFFFFFFE))

/* global variables */
static char *heap_begin; /* to the first byte of a heap */
static char *heap_end;   /* to the last byte of a heap */

/* get class index */
static inline unsigned int find_class(unsigned int bsize)
{
    dbg_printf("find_class(): finding class for size %u\n", bsize);
    int class = -1;
    while (bsize != 0)
    {
        bsize >>= 1;
        class++;
    }
    if (class >= SAGCOUNT)
    {
        return SAGCOUNT;
    }
    dbg_printf("find_class(): class %u found\n", class);
    return class;
}

static inline void remove_from_list(unsigned int offset)
{
    unsigned int original_next = GET(offset + 4);
    unsigned int original_prev = GET(offset + 8);
    /* link next to prev */
    if (original_prev > CLASS(SAGCOUNT))
    {
        PUT(original_prev + 4, original_next);
    }
    else
    {
        PUT(original_prev, original_next);
    }
    if (original_next != 0)
    {
        PUT(original_next + 8, original_prev);
    }
    dbg_printf("remove_from_list(): %x removed from list\n", offset);
    return;
}

static inline void insert_into_list(unsigned int offset)
{
  unsigned int size = SIZE(GET(offset));
  unsigned int class = find_class(size);
  unsigned int original_next = GET(CLASS(class));
  PUT(offset + 4, original_next);
  PUT(offset + 8, CLASS(class));
  if (original_next != 0)
  {
      PUT(original_next + 8, offset);
  }
  PUT(CLASS(class), offset);
  dbg_printf("insert_into_list(): %x inserted into list\n", offset);
}

/* join a block */
static inline unsigned int join(unsigned int offset)
{
    dbg_printf("join(): joining %x\n", offset);
    /* look backwords */
    unsigned int my_size;
    if (!ALLOCED(GET(offset - HSIZE)))
    {
        dbg_printf("join(): joining with previous block\n");
        my_size = SIZE(GET(offset));
        unsigned int before_size = SIZE(GET(offset - HSIZE));
        unsigned int new_size = my_size + before_size;
        remove_from_list(offset);
        remove_from_list(offset - before_size);
        /* update head and foot stamp */
        PUT(offset + my_size - HSIZE, PACK(new_size, 0));
        PUT(offset - before_size, PACK(new_size, 0));
        offset = offset - before_size;
        /* update linked list */
        insert_into_list(offset);
    }
    my_size = SIZE(GET(offset));
    if (!ALLOCED(GET(offset + my_size)))
    {
        dbg_printf("join(): joining with next block\n");
        unsigned int next_size = SIZE(GET(offset + my_size));
        unsigned int new_size = my_size + next_size;
        remove_from_list(offset);
        remove_from_list(offset + my_size);
        PUT(offset, PACK(new_size, 0));
        PUT(offset + new_size - HSIZE, PACK(new_size, 0));
        insert_into_list(offset);
        my_size = new_size;
    }
    return offset;
}

/* extend a new free block */
static inline unsigned int extend(int word)
{
    unsigned int size_new = (word >= 2) ? (word * WSIZE) : (2 * WSIZE);
    /* increase heap */
    unsigned int new_block = COFF(mem_sbrk(size_new)) - HSIZE;
    /* set header */
    PUT(new_block, PACK(size_new, 0));
    /* set footer */
    PUT(new_block + size_new - 4, PACK(size_new, 0));
    PUT(new_block + size_new, PACK(0, 1));
    /* link the list */
    insert_into_list(new_block);
    dbg_printf("extend(): %u bytes extended from %x\n", size_new, new_block);
    return join(new_block);
}

/* Heap Layout
First SAGCOUNT + 1 Ints: list heads at address 0 to SAGCOUNT
Prologue
Prologue
....
(int HEAD ... int FOOT)
Epilogue
*/

/* Free Block Layout
int Head
int prev
int next
int Foot
*/

/*
 * Initialize: return -1 on error, 0 on success.
 */
int mm_init(void) {
    int init_allocate = HSIZE * (SAGCOUNT + 4); /* keep it an even number for allignment */
    mem_sbrk(init_allocate);
    heap_begin = (char*)mem_heap_lo();
    heap_end = (char*)mem_heap_hi();
    for (int i = 0; i < SAGCOUNT + 1; i++)
    {
        PUT(CLASS(i), 0);
    }
    PUT((SAGCOUNT + 1) * HSIZE, PACK(WSIZE, 1));
    PUT((SAGCOUNT + 2) * HSIZE, PACK(WSIZE, 1));
    PUT((SAGCOUNT + 3) * HSIZE, PACK(0, 1));
    dbg_printf("mm_init(): heap initialized\n");
    return 0;
}

/*
 * malloc
 */
void *malloc (size_t size) {
    unsigned int bytes = (unsigned int)ALIGN((size + 8));
    if (bytes < 16)
    {
        bytes = 16;
    }
    dbg_printf("malloc(): allocating %u bytes\n", bytes);
    unsigned int class = find_class(bytes);
    unsigned int find_ptr = GET(CLASS(class));
    unsigned int min_space = 999999999;;
    unsigned int min_ptr = 0;
    /* find in the same class */
    while (find_ptr != 0)
    {
        unsigned int current_size = SIZE(GET(find_ptr));
        if ((current_size >= bytes) && ((current_size - bytes) < min_space))
        {
            min_space = current_size - bytes;
            min_ptr = find_ptr;
            if (min_space == 0)
            {
                break;
            }
        }
        find_ptr = GET(find_ptr + 4);
    }
    /* if cannot find in current class, search higher class */
    /* TODO: search for best in higher class */
    class++;
    if (!min_ptr)
    {
        while (class <= SAGCOUNT)
        {
            if (GET(CLASS(class)) != 0)
            {
                find_ptr = GET(CLASS(class));
                while (find_ptr != 0)
                {
                    unsigned int current_size = SIZE(GET(find_ptr));
                    if ((current_size - bytes) < min_space)
                    {
                        min_space = current_size - bytes;
                        min_ptr = find_ptr;
                    }
                    find_ptr = GET(find_ptr + 4);
                }
                break;
            }
            else
            {
                class++;
            }
        }
    }
    if (min_ptr)
    {
        unsigned int current_size = SIZE(GET(min_ptr));
        remove_from_list(min_ptr);
        if ((current_size - bytes) >= 16)
        {
            PUT(min_ptr, PACK(bytes, 1));
            PUT(min_ptr + bytes - 4, PACK(bytes, 1));
            PUT(min_ptr + bytes, PACK(current_size - bytes, 0));
            PUT(min_ptr + current_size - 4, PACK(current_size - bytes, 0));
            insert_into_list(min_ptr + bytes);
        }
        else
        {
            PUT(min_ptr, PACK(current_size, 1));
            PUT(min_ptr + current_size - 4, PACK(current_size, 1));
        }
        dbg_printf("malloc(): %u bytes allocated at %x\n", bytes, min_ptr);
        #ifdef CHECK
        mm_checkheap(0);
        #endif
        return (void*)CPTR(min_ptr + 4);
    }
    else
    {
        unsigned int new_block = extend(bytes/8);
        unsigned int current_size = SIZE(GET(new_block));
        PUT(new_block, PACK(current_size, 1));
        PUT(new_block + current_size - 4, PACK(current_size, 1));
        remove_from_list(new_block);
        dbg_printf("malloc(): %u bytes allocated at %x\n", bytes, new_block);
        #ifdef CHECK
        mm_checkheap(0);
        #endif
        return (void*)CPTR(new_block + 4);
    }
}

/*
 * free
 */
void free (void *ptr) {
    if(!ptr) return;
    unsigned int to_remove = COFF(ptr) - 4;
    unsigned int current_size = SIZE(GET(to_remove));
    dbg_printf("free(): freeing %u bytes at %x\n", current_size, to_remove);
    PUT(to_remove, PACK(current_size, 0));
    /* TODO: should put end tag here, but if we do, needle.rep will run out memory */
    PUT(to_remove + current_size - 4, PACK(current_size, 0));
    insert_into_list(to_remove);
    join(to_remove);
    #ifdef check
    mm_checkheap(0);
    #endif
    return;
}

/*
 * realloc - you may want to look at mm-naive.c
 */
void *realloc(void *oldptr, size_t size) {
    size_t oldsize;
    void *newptr;

    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0) {
      free(oldptr);
      return 0;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if(oldptr == NULL) {
      return malloc(size);
    }

    newptr = malloc(size);

    /* If realloc() fails the original block is left untouched  */
    if(!newptr) {
      return 0;
    }

    /* Copy the old data. */
    oldsize = (size_t)(SIZE(GET(COFF(oldptr))) - 8);
    if(size < oldsize) oldsize = size;
    memcpy(newptr, oldptr, oldsize);

    /* Free the old block. */
    free(oldptr);

    return newptr;
}

/*
 * calloc - you may want to look at mm-naive.c
 * This function is not tested by mdriver, but it is
 * needed to run the traces.
 */
void *calloc (size_t nmemb, size_t size) {
    size_t bytes = nmemb * size;
    void *newptr;

    newptr = malloc(bytes);
    memset(newptr, 0, bytes);

    return newptr;
}


/*
 * Return whether the pointer is in the heap.
 * May be useful for debugging.
 */
static int in_heap(const void *p) {
    return p <= mem_heap_hi() && p >= mem_heap_lo();
}

/*
 * Return whether the pointer is aligned.
 * May be useful for debugging.
 */
static int aligned(const void *p) {
    return (size_t)ALIGN(p) == (size_t)p;
}

/*
 * mm_checkheap
 */
void mm_checkheap(int verbose) {
    for (int i = 0; i <= SAGCOUNT; i++)
    {
        unsigned int current_ptr = GET(CLASS(i));
        while (current_ptr != 0)
        {
            current_ptr = GET(current_ptr + 4);
            if (current_ptr == 0)
            {
                break;
            }
            unsigned current_size = SIZE(GET(current_ptr));
            unsigned foot_size = SIZE(GET(current_ptr + current_size - 4));
            if (current_size == foot_size)
            {
                /* do nothing */
            }
            else
            {
                printf("deep dark fantasy\n");
            }
            if (GET(GET(current_ptr + 8) + 4) == current_ptr)
            {
                /* do nothing */
            }
            else
            {
                printf("what the hell you are doing now!\n");
            }
        }
    }
}
