#include "devices/block.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include <bitmap.h>

struct bitmap* swap_table;
struct block* block_device;
struct lock swap_lock;

void initialize_swap_table();
block_sector_t load_to_swap(struct page_entry* page);
void load_from_swap(struct page_entry* page);
