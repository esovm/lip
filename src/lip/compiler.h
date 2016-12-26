#ifndef LIP_COMPILER_H
#define LIP_COMPILER_H

#include "ex/common.h"
#include "ast.h"
#include "vendor/khash.h"

typedef struct lip_compiler_s lip_compiler_t;
typedef struct lip_scope_s lip_scope_t;

KHASH_DECLARE(lip_string_ref_set, lip_string_ref_t, char)

struct lip_compiler_s
{
	lip_allocator_t* allocator;
	lip_allocator_t* arena_allocator;
	lip_string_ref_t source_name;
	lip_scope_t* current_scope;
	lip_scope_t* free_scopes;
	khash_t(lip_string_ref_set)* free_var_names;
};

void
lip_compiler_init(lip_compiler_t* compiler, lip_allocator_t* allocator);

void
lip_compiler_cleanup(lip_compiler_t* compiler);

void
lip_compiler_begin(lip_compiler_t* compiler, lip_string_ref_t source_name);

void
lip_compiler_add_ast(lip_compiler_t* compiler, const lip_ast_t* ast);

lip_function_t*
lip_compiler_end(lip_compiler_t* compiler, lip_allocator_t* allocator);

#endif
