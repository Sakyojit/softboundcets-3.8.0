

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#if defined(__linux__)
#include <malloc.h>
#endif
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/mman.h>
#if !defined(__FreeBSD__)
#include <execinfo.h>
#endif
#include "softboundcets.h"

__softboundcets_trie_entry_t** __softboundcets_trie_primary_table;

size_t* __softboundcets_free_map_table = NULL;

size_t* __softboundcets_shadow_stack_ptr = NULL;

size_t* __softboundcets_lock_next_location = NULL;
size_t* __softboundcets_lock_new_location = NULL;
size_t __softboundcets_key_id_counter = 2;

/* key 0 means not used, 1 is for  globals*/
size_t __softboundcets_deref_check_count = 0;
size_t* __softboundcets_global_lock = 0;

size_t* __softboundcets_temporal_space_begin = 0;
size_t* __softboundcets_stack_temporal_space_begin = NULL;

void* malloc_address = NULL;

__SOFTBOUNDCETS_NORETURN void __softboundcets_abort()
{
  fprintf(stderr, "Softboundcets: Memory safety violation detected\n");
  abort();
}


static int softboundcets_initialized = 0;

__NO_INLINE void __softboundcets_stub(void) { return; }

void __softboundcets_init(void) 
{
  if (softboundcets_initialized != 0) {
    return;  // already initialized, do nothing
  }
  
  softboundcets_initialized = 1;

  if (__SOFTBOUNDCETS_DEBUG) {
    __softboundcets_printf("Initializing softboundcets metadata space\n");
  }

  
  assert(sizeof(__softboundcets_trie_entry_t) >= 16);

  /* Allocating the temporal shadow space */

  size_t temporal_table_length = (__SOFTBOUNDCETS_N_TEMPORAL_ENTRIES)* sizeof(void*);

  __softboundcets_lock_new_location = mmap(0, temporal_table_length, 
                                           PROT_READ| PROT_WRITE,
                                           SOFTBOUNDCETS_MMAP_FLAGS, -1, 0);
  
  assert(__softboundcets_lock_new_location != (void*) -1);
  __softboundcets_temporal_space_begin = (size_t *)__softboundcets_lock_new_location;


  size_t stack_temporal_table_length = (__SOFTBOUNDCETS_N_STACK_TEMPORAL_ENTRIES) * sizeof(void*);
  __softboundcets_stack_temporal_space_begin = mmap(0, stack_temporal_table_length, 
                                                    PROT_READ| PROT_WRITE, 
                                                    SOFTBOUNDCETS_MMAP_FLAGS, -1, 0);
  assert(__softboundcets_stack_temporal_space_begin != (void*) -1);


  size_t global_lock_size = (__SOFTBOUNDCETS_N_GLOBAL_LOCK_SIZE) * sizeof(void*);
  __softboundcets_global_lock = mmap(0, global_lock_size, 
                                     PROT_READ|PROT_WRITE, 
                                     SOFTBOUNDCETS_MMAP_FLAGS, -1, 0);
  assert(__softboundcets_global_lock != (void*) -1);
  //  __softboundcets_global_lock =  __softboundcets_lock_new_location++;
  *((size_t*)__softboundcets_global_lock) = 1;



  size_t shadow_stack_size = __SOFTBOUNDCETS_SHADOW_STACK_ENTRIES * sizeof(size_t);
  __softboundcets_shadow_stack_ptr = mmap(0, shadow_stack_size, 
                                          PROT_READ|PROT_WRITE, 
                                          SOFTBOUNDCETS_MMAP_FLAGS, -1, 0);
  assert(__softboundcets_shadow_stack_ptr != (void*)-1);

  *((size_t*)__softboundcets_shadow_stack_ptr) = 0; /* prev stack size */
  size_t * current_size_shadow_stack_ptr =  __softboundcets_shadow_stack_ptr +1 ;
  *(current_size_shadow_stack_ptr) = 0;


  if(__SOFTBOUNDCETS_FREE_MAP) {
    size_t length_free_map = (__SOFTBOUNDCETS_N_FREE_MAP_ENTRIES) * sizeof(size_t);
    __softboundcets_free_map_table = mmap(0, length_free_map, 
                                          PROT_READ| PROT_WRITE, 
                                          SOFTBOUNDCETS_MMAP_FLAGS, -1, 0);
    assert(__softboundcets_free_map_table != (void*) -1);
  }


  size_t length_trie = (__SOFTBOUNDCETS_TRIE_PRIMARY_TABLE_ENTRIES) * sizeof(__softboundcets_trie_entry_t*);
  
  __softboundcets_trie_primary_table = mmap(0, length_trie, 
					    PROT_READ| PROT_WRITE, 
					    SOFTBOUNDCETS_MMAP_FLAGS, -1, 0);
  assert(__softboundcets_trie_primary_table != (void *)-1);  
  
  int* temp = malloc(1);
  __softboundcets_allocation_secondary_trie_allocate_range(0, (size_t)temp);

}

static void softboundcets_init_ctype(){  
#if defined(__linux__)

  char* ptr;
  char* base_ptr;

  ptr = (void*) __ctype_b_loc();
  base_ptr = (void*) (*(__ctype_b_loc()));
  __softboundcets_allocation_secondary_trie_allocate(base_ptr);

#ifdef __SOFTBOUNDCETS_SPATIAL
  __softboundcets_metadata_store(ptr, ((char*) base_ptr - 129), 
                                 ((char*) base_ptr + 256));

#elif __SOFTBOUNDCETS_TEMPORAL
  __softboundcets_metadata_store(ptr, 1, __softboundcets_global_lock);

#elif __SOFTBOUNDCETS_SPATIAL_TEMPORAL
  __softboundcets_metadata_store(ptr, ((char*) base_ptr - 129), 
                                 ((char*) base_ptr + 256), 1, 
                                 __softboundcets_global_lock);

#else  
  __softboundcets_metadata_store(ptr, ((char*) base_ptr - 129), 
                                 ((char*) base_ptr + 256), 1, 
                                 __softboundcets_global_lock);
  
#endif

#endif // __linux ends 
}


void __softboundcets_printf(const char* str, ...)
{
  va_list args;
  
  va_start(args, str);
  vfprintf(stderr, str, args);
  va_end(args);
}

extern int softboundcets_pseudo_main(int argc, char **argv);

int main(int argc, char **argv){

#if __WORDSIZE == 32
  exit(1);
#endif
  
  char** new_argv = argv;
  int i;
  char* temp_ptr;
  int return_value;
  size_t argv_key;
  void* argv_loc;

  int* temp = malloc(1);
  malloc_address = temp;
  __softboundcets_allocation_secondary_trie_allocate_range(0, (size_t)temp);

  __softboundcets_stack_memory_allocation(&argv_loc, &argv_key);

#if defined(__linux__)
  mallopt(M_MMAP_MAX, 0);
#endif

  for(i = 0; i < argc; i++) { 

#ifdef __SOFTBOUNDCETS_SPATIAL

    __softboundcets_metadata_store(&new_argv[i], 
                                   new_argv[i], 
                                   new_argv[i] + strlen(new_argv[i]) + 1);
    
#elif __SOFTBOUNDCETS_TEMPORAL
    //    printf("performing metadata store\n");
    __softboundcets_metadata_store(&new_argv[i],  
                                   argv_key, argv_loc);
    
#elif __SOFTBOUNDCETS_SPATIAL_TEMPORAL

    __softboundcets_metadata_store(&new_argv[i], 
                                   new_argv[i], 
                                   new_argv[i] + strlen(new_argv[i]) + 1, 
                                   argv_key, argv_loc);

#else

    __softboundcets_metadata_store(&new_argv[i], 
                                   new_argv[i], 
                                   new_argv[i] + strlen(new_argv[i]) + 1, 
                                   argv_key, argv_loc);

#endif


  }


  softboundcets_init_ctype();


  temp_ptr = ((char*) &new_argv[argc]) + 8;



  __softboundcets_allocate_shadow_stack_space(2);

#ifdef __SOFTBOUNDCETS_SPATIAL

  __softboundcets_store_base_shadow_stack(&new_argv[0], 1);
  __softboundcets_store_bound_shadow_stack(temp_ptr, 1);

#elif __SOFTBOUNDCETS_TEMPORAL

 
  __softboundcets_store_key_shadow_stack(argv_key, 1);
  __softboundcets_store_lock_shadow_stack(argv_loc, 1);

#elif __SOFTBOUNDCETS_SPATIAL_TEMPORAL

  __softboundcets_store_base_shadow_stack(&new_argv[0], 1);
  __softboundcets_store_bound_shadow_stack(temp_ptr, 1);
  __softboundcets_store_key_shadow_stack(argv_key, 1);
  __softboundcets_store_lock_shadow_stack(argv_loc, 1);

#else

  __softboundcets_store_base_shadow_stack(&new_argv[0], 1);
  __softboundcets_store_bound_shadow_stack(temp_ptr, 1);
  __softboundcets_store_key_shadow_stack(argv_key, 1);
  __softboundcets_store_lock_shadow_stack(argv_loc, 1);

#endif
  

  return_value = softboundcets_pseudo_main(argc, new_argv);
  __softboundcets_deallocate_shadow_stack_space();

  __softboundcets_stack_memory_deallocation(argv_key);

  return return_value;
}

void * __softboundcets_safe_mmap(void* addr, 
                                 size_t length, int prot, 
                                 int flags, int fd, 
                                 off_t offset){
  return mmap(addr, length, prot, flags, fd, offset);
}

void* __softboundcets_safe_calloc(size_t nmemb, size_t size){

  return calloc(nmemb, size);
}

void* __softboundcets_safe_malloc(size_t size){

  return malloc(size);
}
void __softboundcets_safe_free(void* ptr){

  free(ptr);
}
