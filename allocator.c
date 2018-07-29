void __attribute__ ((constructor)) mem_init(void);

#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define PAGESIZE 4096

typedef struct page_tag {	//Metadata for each chunk of memory
	struct page_tag *next;
} page_t;

typedef struct head_tag {	//Metadata for each header that points to a page of chunks
	page_t *start;
	struct head_tag *next_h;
} head_t;

head_t * head_page = NULL;	//Global pointer that will point to the beginning of the header page

/*	Initializes a page of memory for use by this allocator
	Separates the page into chunks of block_size bytes		*/

void init_pages(page_t * start, int block_size) {
	page_t * temp_p = start;
	
	temp_p->next = (page_t*)(temp_p) + 2;		//Point the header block's next two blocks away (first free block)
	temp_p = (page_t*)(temp_p) + 1;			
	
	temp_p->next = start;				//This second block points to a new page of block_size chunks when it is needed
	temp_p = (page_t*)(temp_p) + 1;

	//Separate the page into block_size chunks
	int i;	
	for (i = 0; i < (PAGESIZE-sizeof(page_t*))/(sizeof(page_t*) + block_size) - 1; i++) {
		temp_p->next = (page_t *)((uint8_t *)(temp_p) + block_size) + 1;
		temp_p = temp_p->next;
	}
	temp_p->next = NULL;	//Set last block's next to NULL
}


/*	Constructor function that initializes the header block page
	This page contains blocks each point to a page of sized chunks that the allocator will manage	*/

void mem_init (void) {

	int fd = -1;
	int i = 0;

	//Request a page from memory to keep the header blocks
	head_page = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);

	//Separate the page into blocks of head_t 's
	for (i = 0; i < PAGESIZE/sizeof(head_t) - 1; i++) {
		head_page[i].next_h = &(head_page[i+1]);
		head_page[i].start = NULL;
	}
	head_page[i].next_h = NULL;	//Set last block to NULL //head_page[i+1].next_h = NULL;
	head_page[i].start = NULL;

	//Map pages for the smaller byte sizes, ones we can separate into chunks
	for (i = 1; i < 11; i++) {
		head_page[i].start = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, fd, 0);
		init_pages(head_page[i].start, pow(2, i));
	}

}

//Function determines if a number is a power of 2, useful for finding offset
int power_of_two(size_t size) {
	if (size == 2 || size == 4 || size == 8 || size == 16 || size == 32 || size == 64 || size == 128 || size == 256 || size == 512 || size == 1024) return 1;
	else return 0;
}

/*	Allocates memory of a certain size by finding where the pointer should go using depending on its size
	The allocator returns a pointer to the user of the first available block in the corresponding page, then changes
	the first available block to the next available block in the page (if it exists) 	*/
void * malloc (size_t size) {

	void * ptr = NULL;

	if (size <= 1024) {		//Allocate within normal blocks
		int offset;
		if (power_of_two(size) == 1) offset = log(size)/log(2);
		else if (size == 0) offset = 1;
		else offset = log(size)/log(2) + 1;

		if (head_page[offset].start->next == NULL) {  //at last block, need new page of block_size chunks

			page_t * new_page = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			new_page->next = (head_page[offset].start + 1)->next;
			(head_page[offset].start + 1)->next = new_page;		//Set the second block of the first page's next to the new page

			head_page[offset].start->next = (page_t*)(new_page) + 1;

			page_t * temp_p = head_page[offset].start->next;
			int i;
			int block_size = pow(2,offset);
			for (i = 0; i < (PAGESIZE-sizeof(page_t*))/(sizeof(page_t*) + block_size) - 2; i++) {
				temp_p->next = (page_t *)((uint8_t *)(temp_p) + block_size) + 1;
				temp_p = temp_p->next;
			}
			temp_p->next = NULL;

		}
		//Set the pointer to be returned after the metadata

		page_t * ptr_start = head_page[offset].start->next;
		head_page[offset].start->next = head_page[offset].start->next->next;

		ptr = (page_t *)(ptr_start) + 1;
 
		ptr_start->next = NULL;
				
	}
	
	else {		//Special case, need to allocate at least one page for this size

		head_t * offset_find = &(head_page[11]);
		while (offset_find->start != NULL) {	//Find first available head_t we can map a page to

			offset_find = offset_find->next_h;
		}
		if (offset_find->next_h == NULL) {	//at last head_t, need new page of headers
			head_t * new_page = mmap(NULL, PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			int j;
			for (j = 0; j < PAGESIZE/sizeof(head_t) - 1; j++) {
				new_page[j].next_h = &(new_page[j+1]);
				new_page[j].start = NULL;
			}
			new_page[j].next_h = NULL;
			new_page[j].start = NULL;
			offset_find->next_h = new_page;
			
		}
		int num_pages = (size+sizeof(page_t*)+sizeof(uint64_t *))/PAGESIZE + 1;	//Calculate number of pages needed
		
		offset_find->start = mmap(NULL, num_pages*PAGESIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		uint64_t * page_size = (uint64_t*)((page_t*)(offset_find->start) + 1);	//Set size in metadata for later
		*page_size = size;
		ptr = (uint64_t *)(page_size) + 1;
	
	}

	return ptr;
}

/*	Frees a specified pointer by first finding what page the pointer is located
	Adds the pointer's chunk back to the free list (in the front) or unmaps its page*/

void free (void * ptr) {


	if (ptr == NULL) return;

	uint64_t free_pg = (uint64_t)(ptr) & 0xfffffffffffff000;	//This address is the start of the page where the pointer is located

	int i = 1;
	int found = 0;
	head_t * offset_find = &(head_page[1]);

	while ((page_t *)free_pg != offset_find->start) {

		if (i < 11 && found == 0) {	//Go through the any subsequent pages with the same chunk size
			page_t * next_page = (offset_find->start + 1)->next;
			while (next_page != offset_find->start) {
				if (next_page == (page_t *)free_pg) {
					found = 1;
					break;
				}
				else next_page = next_page->next;
			}
		}
		if (found == 1) break;
		
		i++;
		offset_find = offset_find->next_h;		
		
	}

	if (i > 10) {	//Special size, unmap page
		uint64_t size = *(uint64_t*)((page_t*)(offset_find->start) + 1);
		int num_pages = (size + sizeof(page_t*) + sizeof(uint64_t*))/PAGESIZE + 1;
		munmap(offset_find->start, num_pages*PAGESIZE);
		offset_find->start = NULL;
	}
	else {	//Free the pointer in the smaller size pages
		page_t * free_ptr = (page_t *)ptr - 1;
		free_ptr->next = offset_find->start->next;
		offset_find->start->next = free_ptr;
	}

}

/*	Re-allocates the specified pointer to the new size
	Finds the original pointer and gets its size and determines if a new pointer should be made
	If the new size is larger, a new pointer is allocated, the original data is copied, and the original pointer is free'd	*/

void * realloc (void * ptr, size_t size) {

	uint64_t old_ptr = (uint64_t)(ptr) & 0xfffffffffffff000;	//Start of the page with the original pointer
	int i = 1;
	int found = 0;
	head_t * offset_find = &(head_page[1]);

	while ((page_t *)old_ptr != offset_find->start) {

		if (i < 11 && found == 0) {		//Goes through subsequent pages of the same size chunks
			page_t * next_page = (offset_find->start + 1)->next;
			while (next_page != offset_find->start) {
				if (next_page == (page_t *)old_ptr) {
					found = 1;
					break;
				}
				else next_page = next_page->next;
			}
		}
		if (found == 1) break;
		
		i++;
		offset_find = offset_find->next_h;		
		
	}

	int old_size;
	if (i > 10) {
		old_size = *(uint64_t *)((page_t *)(offset_find->start + 1));
	}
	else old_size = pow(2, i);

	void * new_ptr = NULL;

	if (old_size > size) return ptr;	//If the new size is smaller, just return the original pointer
	else {								//Else allocate a new pointer, copy the original data, and free the original
		new_ptr = malloc(size);	
		for (i = 0; i < old_size; i++) {
			*((char *)(new_ptr) + i) = *((char *)(ptr) + i);
		}
	

		free(ptr);

		return new_ptr;
	}

}

/*	Allocates memory for a number of items of the specified size
	Uses the current malloc and writes 0 into the data			*/

void * calloc (size_t nitems, size_t size) {

	void * ptr = malloc(nitems * size);
	*((uint8_t*)(ptr)) = 0;

	return ptr;
		
}
