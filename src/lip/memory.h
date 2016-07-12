#ifndef LIP_MEMORY_H
#define LIP_MEMORY_H

#include "common.h"

#define LIP_ALIGN_OF(TYPE) offsetof(struct { char c; TYPE t;}, t)
#define lip_new(ALLOCATOR, TYPE) (TYPE*)(lip_malloc(ALLOCATOR, sizeof(TYPE)))
#define LIP_ARRAY_BLOCK(TYPE, LENGTH) \
	{ \
		.element_size = sizeof(TYPE), \
		.num_elements = LENGTH, \
		.alignment = LIP_ALIGN_OF(TYPE) \
	}
#define LIP_STATIC_ARRAY_LEN(ARRAY) (sizeof((ARRAY)) / sizeof((ARRAY)[0]))

typedef struct lip_memblock_info_s lip_memblock_info_t;

struct lip_allocator_s
{
	void*(*realloc)(void* self, void* old, size_t size);
	void(*free)(void* self, void* mem);
};

struct lip_memblock_info_s
{
	size_t element_size;
	size_t num_elements;
	uint8_t alignment;
	ptrdiff_t offset;
};

extern lip_allocator_t* lip_default_allocator;

lip_memblock_info_t
lip_align_memblocks(unsigned int num_blocks, lip_memblock_info_t** blocks);

static inline void*
lip_locate_memblock(void* base, lip_memblock_info_t* block)
{
	return (char*)base + block->offset;
}

static inline void*
lip_realloc(lip_allocator_t* allocator, void* ptr, size_t size)
{
	return allocator->realloc(allocator, ptr, size);
}

static inline void*
lip_malloc(lip_allocator_t* allocator, size_t size)
{
	return lip_realloc(allocator, 0, size);
}

static inline void
lip_free(lip_allocator_t* allocator, void* ptr)
{
	allocator->free(allocator, ptr);
}

static inline void*
lip_align_ptr(void* ptr, size_t alignment)
{
	uintptr_t rem = (uintptr_t)ptr % alignment;
	uintptr_t shift = alignment - rem;
	return (char*)ptr + (rem == 0 ? 0 : shift);
}

#endif
