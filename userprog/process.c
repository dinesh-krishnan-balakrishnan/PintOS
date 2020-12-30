#include "userprog/process.h"
#include "devices/block.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "vm/frame.h"
#include "vm/page.h"
#include <hash.h>

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

// lock to control access to the filesystem
struct lock filesys_lock;

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  tid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid != TID_ERROR) {
    // Abhi driving here, receive the child thread based on its tid
    struct thread* search_thread = tid_to_thread(tid);
    if(!search_thread) {
      return TID_ERROR;
    }

    // wait for the new process to finish loading
    int tid = search_thread->tid;
    sema_down(&search_thread->loading);
    if(!search_thread->loaded || search_thread->exited) {
      return TID_ERROR;
    }

    // add the child process into the parent process list of children
    struct list children = thread_current()->children;
    list_push_front(&children, &search_thread->children_elem);
  } else {
    palloc_free_page (fn_copy);
  }
  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  /* Abhi drivng here, this process has finished
    loading, so unblock its parent process */
  struct thread* current_thread = thread_current();
  current_thread->loaded = success;
  sema_up(&current_thread->loading);

  palloc_free_page (file_name);
  if (!success)
    thread_exit ();

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (tid_t child_tid)
{
  /* Pravat driving here, if there are no children,
    then we have nothing to wait for! */
  struct thread* current_thread = thread_current();
  if(!list_empty(&current_thread->children)) {
    // find the child (wait) thread in the list of this thread's children
    struct thread* index_thread;
    struct thread* wait_thread = NULL;
    struct list_elem* child_iterator = list_front(&current_thread->children);
    while(child_iterator && !wait_thread) {
      // receive this index's thread
      index_thread = list_entry(child_iterator, struct thread, children_elem);

      // check if the tid matches, then we found the wait thread
      if(index_thread->tid == child_tid) {
        wait_thread = index_thread;
      } else {
        child_iterator = child_iterator->next;
      }
    }
    if(wait_thread) {
      // reference the parent thread so it can set the exit status later
      wait_thread->waiting_parent = current_thread;

      // since we're waiting for the child to finish, remove it as a child
      list_remove(&wait_thread->children_elem);

      // allow for the child to now exit
      sema_up(&wait_thread->exit_allowed);

      // sleep the parent thread and wait for its child to exit
      sema_down(&wait_thread->waiting);
      return current_thread->exit;
    }
  }
  return -1;
}


// Dinesh and Abhi driving here, close all of the files within the files Array
void close_files(struct file** files){
    const int MIN_USER_FD = 3;
    const int MAX_FD = 128;

    // iterate through every file possible in this Array
    int fd;
    for(fd = MIN_USER_FD; fd < MAX_FD; fd++) {
      if(files[fd]) {
        // close this file and null out its index
        close(fd);
        files[fd] = NULL;
      }
    }
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  /* Dinesh and Abhi driving here, close all
    of the files opened by this thread */
  close_files(cur->files);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char** arguments, int arg_size);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  // initialize the page table using the page hashing functions
  t->page_table = malloc(sizeof(struct hash));
  if(!t->page_table) {
    goto done;
  }
  hash_init(t->page_table, hash_page_func, hash_page_comparator, NULL);
  lock_init(&t->page_table_lock);

  // Abhi and Pravat driving here, parse the command and its arguments
  char* arguments[128];
  char* leftover = file_name;
  char* token = strtok_r(leftover, " ", &leftover);
  arguments[0] = token;
  int arg_size = 1;

  // initialize the arguments with the program name and args
  while((token = strtok_r(leftover, " ", &leftover))) {
    arguments[arg_size] = token;
    arg_size++;
  }
  lock_acquire(&filesys_lock);
  /* Open executable file. */
  file = filesys_open (file_name);
  lock_release(&filesys_lock);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", file_name);
      goto done;
    }

  // prevent modifications to the executable open by a running process
  lock_acquire(&filesys_lock);
  file_deny_write(file);
  lock_release(&filesys_lock);
  t->executing = file;
  /* Read and verify executable header. */
  lock_acquire(&filesys_lock);
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }
    lock_release(&filesys_lock);

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      lock_acquire(&filesys_lock);
      file_seek (file, file_ofs);
      lock_release(&filesys_lock);


      lock_acquire(&filesys_lock);
      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      lock_release(&filesys_lock);
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }
  /* Set up stack. */
  if (!setup_stack (esp, arguments, arg_size))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  return success;
}

/* load() helpers. */

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  lock_acquire(&filesys_lock);
  file_seek (file, ofs);
  lock_release(&filesys_lock);

  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct thread* current_thread = thread_current();
      lock_acquire(&current_thread->page_table_lock);

      // malloc a new supplemental page table entry if cannot find old entry
      struct page_entry* page = get_page_entry(upage);
      if(!page) {
        page = malloc(sizeof(struct page_entry));
        if(!page) {
          return false;
        }
      }
      
      // set the properties for this new page entry
      page->user_page = upage;
      lock_init(&page->pinning_lock);
      page->file_ptr = file;
      page->file_offset = ofs;
      page->read_bytes = page_read_bytes;
      page->zero_bytes = page_zero_bytes;
      page->writable = writable;
      page->location = FILE_SYSTEM;

      // add the page table entry into this process's supplemental page table
      hash_replace(current_thread->page_table, &page->page_entry_elem);
      lock_release(&current_thread->page_table_lock);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char** arguments, int arg_size)
{
  /*
    Abhi driving, use a page from the stack
    to use as a "zeroed page" at the top
  */
  uint8_t* stack_top_page = ((uint8_t*) PHYS_BASE) - PGSIZE;

  // push a zeroed page at the top of user virtual memory
  bool grew_stack = grow_stack(stack_top_page);
  if(grew_stack) {
    *esp = PHYS_BASE;
  } else {
    palloc_free_page(stack_top_page);
    return false;
  }

  // Dinesh driving here, store the location of each argument
  char* locations[arg_size];

  // add the argument values into the stack, in reverse order
  const int ARGS_LIMIT = PHYS_BASE - PGSIZE;
  const int WORD_SIZE = sizeof(int);
  char* esp_val = (char*) (*esp);
  int arg_index;
  int sub_index;
  for(arg_index = arg_size - 1; arg_index >= 0; arg_index--) {
    esp_val--;
    *esp_val = NULL;
    int curLen = strlen(arguments[arg_index] + 1);

    // add each character on the stack, in reverse order
    for(sub_index = curLen; sub_index >= 0; sub_index--) {
      esp_val--;
      *esp_val = arguments[arg_index][sub_index];
    }
    locations[arg_index] = esp_val;

    /*
      check the limit when pushing arguments to the stack
      https://piazza.com/class/k5iivwicu0kvg?cid=639
    */
    if(*esp < ARGS_LIMIT) {
      palloc_free_page(stack_top_page);
      return false;
    }
  }

  // Abhi driving here, add padding into the stack to be divisible by a word
  int total_alignment = WORD_SIZE + ((int) esp_val % WORD_SIZE);
  int alignment;
  for(alignment = 0; alignment < total_alignment; alignment++) {
    esp_val--;
    *esp_val = NULL;
  }
  if(*esp < ARGS_LIMIT) {
    palloc_free_page(stack_top_page);
    return false;
  }

  // add the argument pointers into the stack
  esp_val -= WORD_SIZE;
  *esp_val = NULL;
  for(arg_index = arg_size - 1; arg_index >= 0; arg_index--){
     esp_val -= sizeof(char *);
     *((char **) esp_val) = locations[arg_index];
  }
  if(*esp < ARGS_LIMIT) {
    palloc_free_page(stack_top_page);
    return false;
  }

  /* Pravat driving here, add the arguments pointer (argv)
    and the arg_size (argc) integer into the stack */
  char *argv_pointer = esp_val;
  esp_val -= WORD_SIZE;
  *(char**) esp_val = argv_pointer;
  esp_val -= WORD_SIZE;
  *esp_val = arg_size;
  if(*esp < ARGS_LIMIT) {
    palloc_free_page(stack_top_page);
    return false;
  }

  // finally, add an arbitrary return address (void*) into the stack
  void *return_address;
  esp_val -= WORD_SIZE;
  *esp_val = return_address;
  *esp = esp_val;
  if(*esp < ARGS_LIMIT) {
    palloc_free_page(stack_top_page);
    return false;
  }

  return true;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}
