#include <lip/core/prim_ops.h>
#include <lip/core.h>
#include <lip/core/extra.h>
#include <string.h>
#include "utils.h"

#define lip_prim_op_bind_args(...) \
	const unsigned int arity_min = 0 + lip_pp_map(lip_bind_count_arity, __VA_ARGS__); \
	const unsigned int arity_max = lip_pp_len(__VA_ARGS__); \
	if(arity_min != arity_max) { \
		lip_bind_assert_argc_at_least(arity_min); \
		lip_bind_assert_argc_at_most(arity_max); \
	} else { \
		lip_bind_assert_argc(arity_min); \
	} \
	lip_pp_map(lip_bind_arg, __VA_ARGS__)

#define LIP_DECLARE_CMP_OP_FN(op, name) \
	LIP_PRIM_OP_FN(name) \
	{ \
		lip_prim_op_bind_args((any, lhs), (any, rhs)); \
		lip_return(lip_make_boolean(vm, lip_gen_cmp(lhs, rhs) op 0)); \
	}

LIP_PRIM_OP_FN(ADD)
{
	double sum = 0;
	for(unsigned int i = 1; i <= argc; ++i)
	{
		lip_bind_arg(i, (number, x));
		sum += x;
	}
	lip_return(lip_make_number(vm, sum));
}

LIP_PRIM_OP_FN(SUB)
{
	lip_prim_op_bind_args((number, lhs), (number, rhs, (optional, 0)));
	lip_return(lip_make_number(vm, argc == 1 ? -lhs : lhs - rhs));
}

LIP_PRIM_OP_FN(MUL)
{
	double product = 1.0;
	for(unsigned int i = 1; i <= argc; ++i)
	{
		lip_bind_arg(i, (number, x));
		product *= x;
	}
	lip_return(lip_make_number(vm, product));
}

LIP_PRIM_OP_FN(FDIV)
{
	lip_prim_op_bind_args((number, lhs), (number, rhs, (optional, 0)));
	lip_return(lip_make_number(vm, argc == 1 ? 1.0 / lhs : lhs / rhs));
}

LIP_PRIM_OP_FN(NOT)
{
	lip_bind_assert_argc(1);
	bool is_false =
		(argv[0].type == LIP_VAL_NIL)
		|| (argv[0].type == LIP_VAL_BOOLEAN && !argv[0].data.boolean);
	lip_return(lip_make_boolean(vm, is_false));
}

static int
lip_gen_cmp(lip_value_t lhs, lip_value_t rhs)
{
	int type_cmp = lhs.type - rhs.type;
	if(LIP_UNLIKELY(type_cmp != 0)) { return type_cmp; }

	switch(lhs.type)
	{
		case LIP_VAL_NIL:
			return 0;
		case LIP_VAL_NUMBER:
			return lhs.data.number - rhs.data.number;
		case LIP_VAL_BOOLEAN:
			return lhs.data.boolean - rhs.data.boolean;
		case LIP_VAL_STRING:
			{
				lip_string_t* lstr = lip_as_string(lhs);
				lip_string_t* rstr = lip_as_string(rhs);
				size_t min_len = LIP_MIN(lstr->length, rstr->length);
				int cmp = memcmp(lstr->ptr, rstr->ptr, min_len);
				return cmp != 0 ? cmp : (int)(lstr->length - rstr->length);
			}
			break;
		case LIP_VAL_PLACEHOLDER:
			return lhs.data.index - rhs.data.index;
		case LIP_VAL_LIST:
			{
				const lip_list_t* llist = lip_as_list(lhs);
				const lip_list_t* rlist = lip_as_list(rhs);

				size_t cmp_len = LIP_MIN(llist->length, rlist->length);
				for(size_t i = 0; i < cmp_len; ++i)
				{
					int cmp = lip_gen_cmp(llist->elements[i], rlist->elements[i]);
					if(cmp != 0) { return cmp; }
				}

				return (int)(llist->length - rlist->length);
			}
		default:
			return (ptrdiff_t)((char*)lhs.data.reference - (char*)rhs.data.reference);
	}
}

LIP_PRIM_OP_FN(CMP)
{
	lip_prim_op_bind_args((any, lhs), (any, rhs)); \
	lip_return(lip_make_number(vm, lip_gen_cmp(lhs, rhs))); \
}

LIP_CMP_OP(LIP_DECLARE_CMP_OP_FN)
