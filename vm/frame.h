#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include <list.h>

// Dinesh driving, use an implicit list of frames to construct the frame table
static struct list frame_table;

// use a lock to prevent multiple processes from allocating simutaneously
static struct lock frame_lock;

typedef struct frame_entry {
  // a frame allocated for this page entry received from the user pool
  uint8_t* user_frame;

  // the process that owns this frame because its page is mapped to it
  struct thread* process;

  // the page that is associated with this frame
  struct page_entry* page;

  // a list element to reference into the frame table
  struct list_elem frame_entry_elem;
} frame_entry;

void initialize_frame_table();
frame_entry* allocate_frame(uint8_t* user_frame);
void free_frame(uint8_t* user_frame);
bool evict_page();
