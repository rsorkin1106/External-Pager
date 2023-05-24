# External-Pager

## 1. Overview
This project is a multi-threaded external pager, which manages virtual memory for
application processes


## 2. System structure
The following portion of vm_pager.h describes the arena, PTE, page table, and PTBR.
```
/*
* ***********************
* * Definition of arena *
* ***********************
*/
/* pagesize for the machine */
#define VM_PAGESIZE 4096
/* virtual address at which application's arena starts */
#define VM_ARENA_BASEADDR ((void *) 0x60000000)
/* virtual page number at which application's arena starts */
#define VM_ARENA_BASEPAGE ((uintptr_t) VM_ARENA_BASEADDR / VM_PAGESIZE)
/* size (in bytes) of arena */
#define VM_ARENA_SIZE 0x20000000
/*
* **************************************
* * Definition of page table structure *
* **************************************
*/
/*
* Format of page table entry.
*
* read_enable=0 ==> loads to this virtual page will fault
* write_enable=0 ==> stores to this virtual page will fault
* ppage refers to the physical page for this virtual page (unused if
* both read_enable and write_enable are 0)
*/
typedef struct {
unsigned int ppage : 20; /* bit 0-19 */
unsigned int read_enable : 1; /* bit 20 */
unsigned int write_enable : 1; /* bit 21 */
} page_table_entry_t;
/*
* Format of page table. Entries start at virtual page VM_ARENA_BASEPAGE,
* i.e., ptes[0] is the page table entry for virtual page VM_ARENA_BASEPAGE.
*/
typedef struct {
page_table_entry_t ptes[VM_ARENA_SIZE/VM_PAGESIZE];
} page_table_t;
/*
* MMU's page table base register. This variable is defined by the
* infrastructure, but it is controlled completely by the student's pager code.
*/
extern page_table_t *page_table_base_register;
```
## 3. How to use
Here is an example application program that uses the external pager.
```
#include <iostream>
#include <cstring>
#include <unistd.h>
#include "vm_app.h"

using std::cout;

int main()
{
    /* Allocate swap-backed page from the arena */
    char *filename = (char *) vm_map(nullptr, 0);

    /* Write the name of the file that will be mapped */
    strcpy(filename, "lampson83.txt");

    /* Map a page from the specified file */
    char *p = (char *) vm_map (filename, 0);

    /* Print the first part of the paper */
    for (unsigned int i=0; i<1937; i++) {
	cout << p[i];
    }
}
```
## 4 Features
The pager's main functions which I implemented are the functions in vm_pager.h: vm_init, vm_create, vm_switch, vm_fault, vm_destroy, and vm_map

### vm_init
The infrastructure calls vm_init when the pager starts. Its parameters are the number of physical pages provided
in physical memory and the number of disk blocks available on the disk.

### vm_create
The infrastructure calls vm_create when a parent process starts a new child process via the fork system call.
Note that the child process is not running at the time vm_create is called. The child process will run when it is
switched to via vm_switch.
After a new child process is created, the infrastructure will subsequently make calls to vm_extend, vm_switch,
and vm_fault to copy the valid portion of the parent's arena to the child.

### vm_switch
The infrastructure calls vm_switch when the OS scheduler runs a new process. This allows your pager to do
whatever bookkeeping it needs to register the fact that a new process is running.

### vm_extend
vm_extend is called when a process wants to make another virtual page in its arena valid. vm_extend should
return the lowest-numbered byte of the new valid virtual page. E.g., if the arena before calling vm_extend is
0x60000000-0x60003fff, the return value of vm_extend will be 0x60004000, and the resulting valid part of
the arena will be 0x60000000-0x60004fff.
vm_extend should ensure that there are enough available disk blocks to hold all valid virtual pages (this is called
eager swap allocation). If there are no free disk blocks, vm_extend should return nullptr. The benefit of eager
swap allocation is that applications know at the time of vm_extend that there is no more swap space, rather than
when a page needs to be evicted to disk.
A non-zero share_id names the newly extended virtual page. All virtual pages with the same non-zero
share_id are shared with each other. The pager should manage all members of a set of shared virtual pages as a
single virtual page. E.g., a set of shared virtual pages should be represented as a single node on the clock queue.
Remember that an application should see each byte of a newly mapped virtual page as initialized with the value
0. However, the actual data initialization needed to provide this abstraction should be deferred as long as
possible.

### vm_fault
The vm_fault function is called in response to a read or write fault by the application. Your pager determines
which accesses in the arena will generate faults by setting the read_enable and write_enable fields in the
page table. Your pager determines which physical page is associated with a virtual page by setting the ppage
field in the page table.
vm_fault should return 0 after successfully handling a fault. vm_fault should return -1 if the address is to an
invalid page or is outside the arena.

### vm_destroy
vm_destroy is called by the infrastructure when the corresponding application exits. This function must
deallocate all resources held by that process. This includes page tables, physical pages, and disk blocks. Physical
pages that are released should be put back on the free list.

## 5 Interface used to access simulated hardware
The following portion of vm_pager.h describes the variables and utility functions for accessing this hardware.
```
/*
* *********************************************
* * Public interface for the disk abstraction *
* *********************************************
*
* Disk blocks are numbered from 0 to (disk_blocks-1), where disk_blocks
* is the parameter passed in vm_init().
*/
/*
* disk_read
*
* read block "block" from the disk into buf.
*/
extern void disk_read(unsigned int block, void *buf);
/*
* disk_write
*
* write the contents of buf to disk block "block".
*/
extern void disk_write(unsigned int block, void *buf);
/*
* ********************************************************
* * Public interface for the physical memory abstraction *
* ********************************************************
*
* Physical memory pages are numbered from 0 to (memory_pages-1), where
* memory_pages is the parameter passed in vm_init().
*
* Your pager accesses the data in physical memory through the variable
* vm_physmem, e.g., ((char *)vm_physmem)[5] is byte 5 in physical memory.
*/
extern void * const vm_physmem;
```

Physical memory is structured as a contiguous collection of N pages, numbered from 0 to N-1. It is settable
through the -m option when you run the external pager (e.g., by running pager -m 4). The minimum number of
physical pages is 2, the maximum is 1024, and the default is 4. Your pager can access the data in physical
memory via the array vm_physmem.
The disk is modeled as a single device that has disk_blocks blocks. Each disk block is the same size as a
physical memory page. Your pager will use two functions to access the disk: disk_write is used to write data
from a physical page out to disk, and disk_read is used to read data from disk into a physical page.
Physical pages should only be shared among virtual pages when those virtual pages are shared with each other.
That is, each physical page should be associated with at most one virtual page (or set of shared virtual pages) at
any given time.
Similarly, disk blocks should only be shared between virtual pages when those virtual pages are shared with
each other. That is, each disk block should be associated with at most one virtual page (or set of shared virtual
pages) at any given time.

## Setup

As per the makefile:

Compile pager - `make pager`

Compile tests - `make test%` where '%' is the suffix after 'test' in each test file

Remove all compiled files - `make clean`
