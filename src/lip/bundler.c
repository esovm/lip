#include "bundler.h"
#include <string.h>
#include "module.h"
#include "array.h"
#include "allocator.h"
#include "function.h"
#include "utils.h"

void lip_bundler_init(
	lip_bundler_t* bundler,
	lip_allocator_t* allocator
)
{
	bundler->allocator = allocator;
	bundler->symbols = lip_array_new(allocator);
	bundler->functions = lip_array_new(allocator);
}

void lip_bundler_begin(lip_bundler_t* bundler)
{
	lip_array_clear(bundler->symbols);
	lip_array_clear(bundler->functions);
}

void lip_bundler_add_lip_function(
	lip_bundler_t* bundler,
	lip_string_ref_t name,
	lip_function_t* function
)
{
	lip_array_push(bundler->symbols, name);
	unsigned int index = lip_array_len(bundler->functions);
	lip_array_resize(bundler->functions, index + 1);
	lip_closure_t* closure = bundler->functions + index;
	closure->info.is_native = false;
	closure->function_ptr.lip = function;
	closure->environment_size = 0;
}

void lip_bundler_add_native_function(
	lip_bundler_t* bundler,
	lip_string_ref_t name,
	lip_native_function_t function,
	uint8_t arity
)
{
	lip_array_push(bundler->symbols, name);
	unsigned int index = lip_array_len(bundler->functions);
	lip_array_resize(bundler->functions, index + 1);
	lip_closure_t* closure = bundler->functions + index;
	closure->info.is_native = true;
	closure->info.native_arity = arity;
	closure->function_ptr.native = function;
	closure->environment_size = 0;
}

lip_module_t* lip_bundler_end(lip_bundler_t* bundler)
{
	// Calculate the size of the memory block for the module
	size_t header_size = sizeof(lip_module_t);
	unsigned int num_symbols = lip_array_len(bundler->symbols);
	size_t symbol_table_size = num_symbols * sizeof(lip_string_t*);
	size_t symbol_section_size = 0;
	lip_array_foreach(lip_string_ref_t, itr, bundler->symbols)
	{
		symbol_section_size += lip_string_align(itr->length);
	}
	size_t value_table_size = num_symbols * sizeof(lip_value_t);
	size_t closure_section_size = num_symbols * sizeof(lip_closure_t);

	size_t block_size =
		  header_size
		+ symbol_table_size
		+ symbol_section_size
		+ value_table_size
		+ closure_section_size;

	lip_module_t* module = lip_malloc(bundler->allocator, block_size);
	// Write header
	module->num_symbols = num_symbols;
	char* ptr = (char*)module + header_size;

	// Write symbol table
	module->symbols = (lip_string_t**)ptr;
	ptr += symbol_table_size;

	// Write value table
	module->values = (lip_value_t*)ptr;
	ptr += value_table_size;

	// Write symbol section
	for(unsigned int i = 0; i < num_symbols; ++i)
	{
		lip_string_ref_t* symbol = bundler->symbols + i;
		lip_string_t* entry = (lip_string_t*)ptr;
		entry->length = symbol->length;
		memcpy(&entry->ptr, symbol->ptr, symbol->length);
		module->symbols[i] = (lip_string_t*)ptr;

		ptr += lip_string_align(symbol->length);
	}

	// Write value section
	memcpy(ptr, bundler->functions, closure_section_size);
	for(unsigned int i = 0; i < num_symbols; ++i)
	{
		module->values[i].type = LIP_VAL_CLOSURE;
		module->values[i].data.reference = (lip_closure_t*)ptr + i;
	}

	return module;
}

void lip_bundler_cleanup(lip_bundler_t* bundler)
{
	lip_array_delete(bundler->functions);
	lip_array_delete(bundler->symbols);
}