#include "vm_pager.h"

#include <unordered_map>
#include <queue>
#include <vector>
#include <assert.h>
#include <string.h>
#include <iostream>
#include <algorithm>

//using std::cout;
using std::endl;


struct Page
{
    bool dirty = 0;
    bool referenced = 0;
    bool resident = 0;
    int ref_count = 0;
    //char backing;
    std::string filename = "";
};

//std::unordered_map<uintptr_t, std::unordered_map<page_table_t*, uintptr_t> > num_to_vpn;

//FILE-BACKED , MULTIPLE VPNS IN A SINGLE PAGE TABLE CAN POINT TO THE SAME PPN!!!!!!
std::unordered_map<uintptr_t, std::unordered_map<page_table_t*, std::vector<uintptr_t> > > num_to_vpn;
//for(key in num_to_vpn[ppn]) 
//  num_to_vpn[ppn][key] -> vpn
//  key->ptes[vpn]
//TODO: fix all function to update this / functions that reference it

/*add:
    when a process adds the page to physmem
    when a process points to the same page (ref_count > 1)
remove:
    when the page is evicted from physmem
    when ref_count is decremented*/

unsigned int num_swap_blocks = 256;
unsigned int num_memory_pages = 4;
unsigned int num_used_swap = 0;

std::vector<int> swap_blocks_write;
std::unordered_map<page_table_t*, std::unordered_map<uintptr_t, int> > swap_blocks_read;

//this is so that when we read in a file-backed page, we can check if it is already in physmem, 
//and point to the same ppn if it is, or make a new entry. file-backed pages must be shared
std::unordered_map<std::string, std::unordered_map<unsigned int, uintptr_t> > fileblock_to_ppn; //maps file name to file-block to ppn
std::unordered_map<uintptr_t, std::pair<std::string, unsigned int> > ppn_to_fileblock; //maps ppn to its file and file-block

//maps ptbr to vpn to {filename, block#}
std::unordered_map<page_table_t*, std::unordered_map<uintptr_t, std::pair<std::string, unsigned int> > > file_blocks_read;

std::unordered_map<std::string, 
    std::unordered_map<unsigned int, 
        std::unordered_map<page_table_t*, std::vector<uintptr_t> > > > shared_pages;
//filename -> block -> ptbr -> vector<vpn>

std::unordered_map<pid_t, page_table_t*> proccess_to_pageTable;

std::unordered_map<page_table_t*, uintptr_t> maxArena;

//Physical page num to it's bits
std::unordered_map<uintptr_t, Page> bits;

//maps proccess to num swap blocks
std::unordered_map<page_table_t*, unsigned int> proccess_to_num_swaps;

std::queue<uintptr_t> clock_q;

std::priority_queue<uintptr_t, std::vector<uintptr_t>, std::greater<uintptr_t>> available_ppages;


void sync_shared(unsigned int ppage, unsigned int read_en, unsigned int write_en) {

    //std::string filename = file_blocks_read[page_table_base_register][vpn].first;
    //unsigned int block = file_blocks_read[page_table_base_register][vpn].second;
    std::string filename = ppn_to_fileblock[ppage].first;
    unsigned int block = ppn_to_fileblock[ppage].second;

    for(auto it : shared_pages[filename][block]) {
        for(auto x : it.second) {
            it.first->ptes[x].ppage = ppage;
            it.first->ptes[x].read_enable = read_en;
            it.first->ptes[x].write_enable = write_en;
        }
    }


}

uintptr_t evict(uintptr_t page_num)
{
    char* buf = (char*)vm_physmem;
    Page bits_copy = bits[page_num];
    bits_copy.resident = 0;
    page_table_t* tempPtbr = nullptr;
    uintptr_t tempVpage = 0;

    //cout << "Enter evict with page_num: " << page_num << endl;
    //cout << num_to_vpn[page_num].size() << endl;
    if (bits_copy.filename == "")
    {
        assert(num_to_vpn[page_num].size() == 1);
        for (auto it : num_to_vpn[page_num])
        {
            tempPtbr = it.first;
            assert(it.second.size() == 1);

            tempVpage = it.second[0];
            //TODO
            it.first->ptes[it.second[0]].read_enable = 0;
            it.first->ptes[it.second[0]].write_enable = 0;
            it.first->ptes[it.second[0]].ppage = num_memory_pages + 1; //non-resident ppage for swap-backed (+2 can be filebacked hack)
        }
        num_to_vpn.erase(page_num);

        if ((bits_copy).dirty == 1)
        {
            int available_block = -1;
            for (size_t i = 0; i < swap_blocks_write.size(); ++i)
            {
                if (swap_blocks_write[i] == 0)
                {
                    //cout << "swap_block: " << i << endl;
                    available_block = i;
                    break;
                }
            }

            assert(available_block != -1);

            if (file_write(nullptr, available_block, (void*)(buf + (page_num * VM_PAGESIZE))) == -1)
            {
                //throw std::invalid_argument("file_write failed");
                return -1;
            }
            else {
                swap_blocks_write[available_block] = 1;
                swap_blocks_read[tempPtbr][tempVpage] = available_block;
            }

        }
    }
    else //file-backed
    {
        unsigned int temp_block = ppn_to_fileblock[page_num].second;
        for (auto it : num_to_vpn[page_num])
        {
            //TODO
            for (auto x : it.second) {
                //temp_block = file_blocks_read[it.first][x].second;
                fileblock_to_ppn[bits_copy.filename].erase(temp_block);
                ppn_to_fileblock.erase(page_num);

                it.first->ptes[x].read_enable = 0;
                it.first->ptes[x].write_enable = 0;
                it.first->ptes[x].ppage = num_memory_pages + 1; //non-resident ppage
            }

        }
        //cout << temp_block << endl;
        num_to_vpn.erase(page_num);

        if ((bits_copy).dirty == 1)
        {
            if (file_write(bits_copy.filename.c_str(), temp_block, (void*)(buf + (page_num * VM_PAGESIZE))) == -1)
            {
                //throw std::invalid_argument("file_write failed");
                return -1;
            }
        }

    }
    return page_num;
}

uintptr_t clock_eviction()
{
    assert(clock_q.size() == num_memory_pages - 1);
    //cout << "clock eviction swap_block_write[0]: " << swap_blocks_write[0] << endl;
    while (true)
    {
        uintptr_t next = clock_q.front();
        clock_q.pop();

        if (bits[next].referenced == 1)
        {

            for (auto it : num_to_vpn[next])
            {
                for (auto x : it.second)
                {
                    it.first->ptes[x].read_enable = 0;
                    it.first->ptes[x].write_enable = 0;
                }
            }

            if(bits[next].filename != "") {
                sync_shared(next, 0, 0);
            }

            bits[next].referenced = 0;
            clock_q.push(next);
        }
        else
        {
            if(bits[next].filename != "") {
                sync_shared(next, 0, 0);
            }
            return evict(next);
        }
    }
}

unsigned int copy_on_write(const void* addr)
{
    uintptr_t converted_addr = (uintptr_t)addr;
    //cout << "copy_oxn_write converted address " << converted_addr << endl;
    uintptr_t vpn = (converted_addr - (uintptr_t)VM_ARENA_BASEADDR) / VM_PAGESIZE;
    //cout << "vpn " << vpn << endl;
    page_table_entry_t* pte = &page_table_base_register->ptes[vpn];

    //num_to_vpn[pte.ppage].erase(page_table_base_register);


    char temp_buffer[VM_PAGESIZE];
    for (unsigned int i = 0; i < VM_PAGESIZE; ++i)
    {
        temp_buffer[i] = ((char*)vm_physmem)[pte->ppage * VM_PAGESIZE + i];
    }

    uintptr_t ppage;
    //cout << available_ppages.size() << " size" << endl;
    if (!available_ppages.empty())
    {
        ppage = available_ppages.top();
        //cout << "enter if ppage: " << ppage << endl;
        available_ppages.pop();
    }
    else
    {
        //cout << "enter else ppage: " << endl;
        ppage = clock_eviction();
    }

    for (unsigned int i = 0; i < VM_PAGESIZE; ++i)
    {
        ((char*)vm_physmem)[(ppage * VM_PAGESIZE + i)] = temp_buffer[i];
    }
    pte->ppage = ppage;
    //

    pte->write_enable = 1;
    pte->read_enable = 1;

    assert(num_to_vpn[pte->ppage][page_table_base_register].size() == 0);

    //cout << pte->ppage << " PPAGE AND VPN " << vpn << endl;
    auto start = num_to_vpn[0][page_table_base_register].begin();
    auto end = num_to_vpn[0][page_table_base_register].end();

    num_to_vpn[0][page_table_base_register].erase(std::find(start, end, vpn));
    num_to_vpn[pte->ppage][page_table_base_register].push_back(vpn);

    return pte->ppage;
}

void vm_init(unsigned int memory_pages, unsigned int swap_blocks)
{
    num_swap_blocks = swap_blocks;
    num_memory_pages = memory_pages;
    swap_blocks_write.resize(num_swap_blocks, 0);

    //cout << "start init" << endl;
    memset(vm_physmem, 0x00, VM_PAGESIZE);
    //cout << "passed memset" << endl;

    bits[0].dirty = 0;
    bits[0].referenced = 0;
    bits[0].resident = 1;
    bits[0].ref_count = 1;
    bits[0].filename = "";

    for (uintptr_t i = 1; i < num_memory_pages; i++) {
        available_ppages.push(i);
    }
    //cout << "finsihr edinit" << endl;
}

int vm_create(pid_t parent_pid, pid_t child_pid)
{

    //just have this for an intial draft, it should work. it might just not be optimized
    page_table_t* new_table = new page_table_t();
    page_table_entry_t temp;
    temp.ppage = num_memory_pages + 1;
    temp.read_enable = 0;
    temp.write_enable = 0;
    for (unsigned int i = 0; i < VM_ARENA_SIZE / VM_PAGESIZE; ++i) {
        new_table->ptes[i] = temp;
    }
    proccess_to_num_swaps[new_table] = 0;
    proccess_to_pageTable[child_pid] = new_table;
    maxArena[proccess_to_pageTable[child_pid]] = 0;
    
    return 0;
}

void vm_switch(pid_t pid)
{
    if (proccess_to_pageTable.find(pid) != proccess_to_pageTable.end())
        page_table_base_register = proccess_to_pageTable[pid];
}

int vm_fault(const void* addr, bool write_flag)
{
    //printf("faulted %d \n", write_flag);
    uintptr_t converted_addr = (uintptr_t)addr;
    uintptr_t vpn = (converted_addr - (uintptr_t)VM_ARENA_BASEADDR) / VM_PAGESIZE;
    page_table_entry_t* pte = &page_table_base_register->ptes[vpn];

    Page cbits = bits[pte->ppage]; //if page is non-resident, we are using the default for ppage..
    unsigned int old_ppage = pte->ppage;

    //start

    //transition (0)
    uintptr_t max_addr = maxArena[page_table_base_register] * VM_PAGESIZE + (uintptr_t)VM_ARENA_BASEADDR;
    if (converted_addr >= max_addr || converted_addr < (uintptr_t)VM_ARENA_BASEADDR)
    {
        //cout << "out of bounds" << endl;
        return -1;
    }

    if (!cbits.resident) //transition (1)
    {
        //todo page in
        //find an available page
        uintptr_t ppage;
        if (!available_ppages.empty())
        {
            ppage = available_ppages.top();
            available_ppages.pop();
        }
        else
        {
            ppage = clock_eviction();
        }

        char* buf = (char*)vm_physmem;
        //cout << "pre file_read\n";
        if (swap_blocks_read[page_table_base_register].find(vpn) != swap_blocks_read[page_table_base_register].end()) {
            int block = swap_blocks_read[page_table_base_register][vpn];
            if (file_read(nullptr, block, (void*)(buf + (ppage * VM_PAGESIZE))) == -1)
            {
                //throw std::invalid_argument("file_read failed");
                return -1;
            }

            //cout << "post file_read swap_blocks_write[0]: " << swap_blocks_write[0] << endl;

            //swap_blocks_write[block] = 0;
            //end page in


            cbits.filename = "";
        }
        else { //file-backed
            //read from a file-backed page
            std::string temp_filename = file_blocks_read[page_table_base_register][vpn].first;
            unsigned int block = file_blocks_read[page_table_base_register][vpn].second;
            //cout << temp_filename << " " << block << endl;
            if (file_read(temp_filename.c_str(), block, (void*)(buf + (ppage * VM_PAGESIZE))) == -1)
            {
                //throw std::invalid_argument("file_read failed");
                return -1;
            }
            else {
                //cout << temp_filename << " " << block << endl;
                fileblock_to_ppn[temp_filename][block] = ppage;
                ppn_to_fileblock[ppage] = { temp_filename, block };
                cbits.filename = temp_filename;
                
                //sync_shared(temp_filename, block, ppage, 1, 0);

                /*for(auto it : shared_pages[temp_filename][block]) {
                    for(auto x : it.second) {
                        it.first->ptes[x].ppage = ppage;
                        it.first->ptes[x].read_enable = 1;
                        it.first->ptes[x].write_enable = 0;
                    }
                }*/
            }
        }
        //cout << "reached soethinbg" << endl;

        pte->ppage = ppage;

        num_to_vpn[pte->ppage][page_table_base_register].push_back(vpn);

        cbits.dirty = 0;
        cbits.ref_count = 1;
        cbits.referenced = 1;

        cbits.resident = 1;

        clock_q.push(pte->ppage);
        //set other bits? update structures
    }
    else if (!cbits.referenced) //resident, transition (3)
    {
        //cout << "enter else if\n";
        cbits.referenced = 1;
        if (cbits.dirty) pte->write_enable = 1;
    }

    if (write_flag) //transition (2)
    {
        //
        if (cbits.ref_count > 1 && cbits.filename == "") { //resident, swap-backed only
            //copy on write
            assert(bits[old_ppage].ref_count > 1);
            bits[old_ppage].ref_count--;

            pte->ppage = copy_on_write(addr); //copy on write sets this pte.ppage as well TODO 

            clock_q.push(pte->ppage);
            cbits.filename = bits[old_ppage].filename;

            cbits.ref_count = 1;
            cbits.resident = 1;
        }
        cbits.dirty = 1;
        pte->write_enable = 1;
    }

    pte->read_enable = 1;

    if(cbits.filename != "") {
        sync_shared(pte->ppage, pte->read_enable, pte->write_enable);
    }

    bits[pte->ppage] = cbits;
    return 0;
    //end

}


//P - TODO - in general, I think we should consider in what ways we can defer work in this function
void vm_destroy()
{
    //TODO case for filebacked
    //cout << "DESTROY clock size: " << clock_q.size() << endl;
    //questions:
    //what should we destroy
    //what should we defer (swap-backed vs file-backed)
    for (auto it : swap_blocks_read[page_table_base_register])
    {
        //cout << "enter destroy with swap_blocks_write[0]: " << swap_blocks_write[0] << endl;
        swap_blocks_write[it.second] = 0;
    }
    num_used_swap -= proccess_to_num_swaps[page_table_base_register];

    swap_blocks_read.erase(page_table_base_register);

    size_t clock_size = clock_q.size();
    for (size_t i = 0; i < clock_size; ++i)
    {
        //cout << i << " out of " << clock_q.size() << endl;
        uintptr_t temp = clock_q.front();
        clock_q.pop();
        //cout << "physmem page: " << temp << endl;
        if (num_to_vpn[temp].find(page_table_base_register) != num_to_vpn[temp].end())
        {
            bits[temp].ref_count--;
            //cout << "exists in the table with decremented ref count: " << bits[temp].ref_count << endl;
        }

        if (bits[temp].filename == "" && bits[temp].ref_count == 0)
        {
            available_ppages.push(temp);
            bits[temp].resident = 0;
            num_to_vpn.erase(temp);
        }
        else
        {
            clock_q.push(temp);
        }
    }

    for (auto it : num_to_vpn)
    {
        num_to_vpn[it.first].erase(page_table_base_register);
        //decerement refcount, free if == 0
    }

    file_blocks_read.erase(page_table_base_register);
    maxArena.erase(page_table_base_register);

    for(auto it : shared_pages) {
        for(auto block_it : shared_pages[it.first]) {
            shared_pages[it.first][block_it.first].erase(page_table_base_register);
        }
    }
    //cout << clock_q.size() << " + " << available_ppages.size() << " IS SIZE" << endl;

    proccess_to_num_swaps.erase(page_table_base_register);

    delete page_table_base_register; //delete page table
}

void* vm_map(const char* filename, unsigned int block)
{
    if (maxArena[page_table_base_register] * VM_PAGESIZE >= VM_ARENA_SIZE)
        return nullptr;

    //int length = strlen(filename);

    page_table_entry_t new_pte;

    if (filename == nullptr)
    {
        if (num_used_swap == num_swap_blocks)
            return nullptr;

        num_used_swap++;
        proccess_to_num_swaps[page_table_base_register]++;

        bits[0].ref_count++;
        num_to_vpn[0][page_table_base_register].push_back(maxArena[page_table_base_register]);

        //page_table_entry_t new_pte; 

        new_pte.read_enable = 1;
        new_pte.write_enable = 0;
        new_pte.ppage = 0; //pinned page

        //page_table_base_register->ptes[maxArena[page_table_base_register]] = new_pte;
        //maxArena[page_table_base_register]++;
        //return (void*)((uintptr_t)VM_ARENA_BASEADDR + (maxArena[page_table_base_register] - 1) * VM_PAGESIZE);
    }
    else
    {
        //cout << filename << endl;
        //TODO check that cstring is within address space
        //page is file backed
        //readable: 0, non-resident(defer work), unreferenced
        uintptr_t fileAdr = (uintptr_t)filename;
        uintptr_t fileVpn = (fileAdr - (uintptr_t)VM_ARENA_BASEADDR) / VM_PAGESIZE;
        uintptr_t fileOffset = (fileAdr - (uintptr_t)VM_ARENA_BASEADDR) % VM_PAGESIZE;
        std::string file_name = "";
        char* memory = (char*)vm_physmem;
        bool found = false;

        //cout << "enter map" << endl;
        //page_table_entry_t new_pte;
        uintptr_t vpn = maxArena[page_table_base_register];
        if (fileAdr < (uintptr_t)VM_ARENA_BASEADDR || fileAdr >= vpn * VM_PAGESIZE + (uintptr_t)VM_ARENA_BASEADDR) {
            return nullptr;
        }
        //cout << "after initial check" << endl;

        //Make sure all vpages that filename is in is in physical memory
        for (uintptr_t i = fileVpn; i < vpn; ++i) {
            if (page_table_base_register->ptes[i].read_enable == 0) {
                vm_fault((void*)(i * VM_PAGESIZE + (uintptr_t)VM_ARENA_BASEADDR), false);
            }
            uintptr_t ppn = page_table_base_register->ptes[i].ppage;
            for (uintptr_t j = fileOffset; j < VM_PAGESIZE; ++j) {
                if (*(memory + (ppn * VM_PAGESIZE + j)) == '\0') {
                    found = true;
                    break;
                }
                file_name += *(memory + (ppn * VM_PAGESIZE + j));
            }
            if (found)
                break;
            fileOffset = 0;
        }
        //cout << "after name check" << endl;

        //ending of filename must be in unmapped arena
        if (!found) {
            return nullptr;
        }
        //cout << "after name check 2" << endl;

        //cout << vpn << " " << file_name << " " << block << endl;
        file_blocks_read[page_table_base_register][vpn] = {file_name, block};

        //cout << endl;
        //cout << "current filename: " << file_name << endl << endl;
        //cout << "all filenames: ";

        shared_pages[file_name][block][page_table_base_register].push_back(vpn);
        //cout << "checks to see if file exists" << endl;
        if(fileblock_to_ppn.find(file_name) != fileblock_to_ppn.end() && 
            fileblock_to_ppn[file_name].find(block) != fileblock_to_ppn[file_name].end()) {

            //cout << "already in physmem" << endl;
            new_pte.ppage = fileblock_to_ppn[file_name][block];

            /*auto it = num_to_vpn[new_pte.ppage].begin(); //first ptbr
            auto x = it->second; //first vpn ?
            new_pte.read_enable = it->first->ptes[x[0]].read_enable;
            new_pte.write_enable = it->first->ptes[x[0]].write_enable;*/
            Page temp_page = bits[new_pte.ppage];
            if(temp_page.resident && temp_page.referenced) {
                new_pte.read_enable = 1;
                if(temp_page.dirty == 1) {
                    new_pte.write_enable = 1;
                }
            }
            else {
                new_pte.read_enable = 0;
                new_pte.write_enable = 0;
            }
        }
        else {
            //cout << "new" << endl;
            new_pte.read_enable = 0;
            new_pte.write_enable = 0;

            new_pte.ppage = num_memory_pages + 1; //default (nonresident) value
        }
    }

    page_table_base_register->ptes[maxArena[page_table_base_register]] = new_pte;
    maxArena[page_table_base_register]++;
    return (void*)((uintptr_t)VM_ARENA_BASEADDR + (maxArena[page_table_base_register] - 1) * VM_PAGESIZE);

}