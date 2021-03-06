#ifndef LIP_BIND_H
#define LIP_BIND_H

/**
 * @defgroup bind Bind
 * @brief Binding helpers
 *
 * @{
 */

/// Declare a bound function
#define lip_function(name) \
	lip_exec_status_t name(lip_vm_t* vm, lip_value_t* result)

#define lip_bind_track_native_location(vm) \
	lip_set_native_location(vm, __func__, __FILE__, __LINE__); \

/**
 * @brief  Declare `argc`, `argv` local variables to hold arguments.
 * @remarks #lip_bind_args will call this so do not call it unless you are doing manual type checking or binding a variadic function.
 */
#define lip_bind_prepare(vm) \
	uint8_t argc; const lip_value_t* argv = lip_get_args(vm, &argc); (void)argv;

/**
 * @brief Bind arguments to variables.
 *
 * Parameters must have one of the following forms:
 *
 * - `(type, name)`: Bind a required parameter of type `type` to a local variable `name`.
 * - `(type, name, (optional, default_value))`: Bind an optional parameter with default value `default_value`.
 *
 * Valid types are:
 * - `any`: Any type. A local variable with name `name` will be declared with type ::lip_value_s.
 * - `number`: Number type. A local variable with name `name` will be declared with type `double`.
 * - `string`: String type. A local variable with name `name` will be declared with type ::lip_value_s.
 * - `symbol`: Symbol type. A local variable with name  `name` will be declared with type ::lip_value_s.
 * - `list`: List type. A local variable with name `name` will be declared with type ::lip_value_s.
 */
#define lip_bind_args(...) \
	lip_bind_prepare(vm); \
	const unsigned int arity_min = 0 lip_pp_map(lip_bind_count_arity, __VA_ARGS__); \
	const unsigned int arity_max = lip_pp_len(__VA_ARGS__); \
	if(arity_min != arity_max) { \
		lip_bind_assert_argc_at_least(arity_min); \
		lip_bind_assert_argc_at_most(arity_max); \
	} else { \
		lip_bind_assert_argc(arity_min); \
	} \
	lip_pp_map(lip_bind_arg, __VA_ARGS__)

/**
 * @brief Bind an argument to a local variable.
 *
 * @param i Index of the argument (First argument is 0).
 * @param spec Specification for the argument.
 *
 * @see lip_bind_args
 */
#define lip_bind_arg(i, spec) \
	lip_bind_arg1( \
		i, \
		lip_pp_nth(1, spec, any), \
		lip_pp_nth(2, spec, missing_argument_name[-1]), \
		lip_pp_nth(3, spec, (required)) \
	)
#define lip_bind_arg1(i, type, name, extra) \
	lip_pp_concat(lip_bind_declare_, type)(name); \
	lip_pp_concat(lip_pp_concat(lip_bind_, lip_pp_nth(1, extra, required)), _arg)( \
		i, type, name, extra \
	)

#define lip_bind_count_arity(i, spec) \
    lip_bind_count_arity1(i, lip_pp_nth(1, lip_pp_nth(3, spec, (required)), required))
#define lip_bind_count_arity1(i, spec) \
	+ lip_pp_concat(lip_bind_count_arity_, spec)
#define lip_bind_count_arity_required 1
#define lip_bind_count_arity_optional 0

#define lip_bind_required_arg(i, type, name, extra) \
	lip_pp_concat(lip_bind_load_, type)(i, name, argv[i - 1]);

#define lip_bind_optional_arg(i, type, name, extra) \
	do { \
		if(i <= argc) { \
			lip_bind_required_arg(i, type, name, extra); \
		} else { \
			name = lip_pp_nth(2, extra, missing_optional); \
		} \
	} while(0);

#define lip_bind_assert_argc_at_least(arity_min) \
	lip_bind_assert_fmt( \
		argc >= arity_min, \
		"Bad number of arguments (at least %u expected, got %u)", \
		arity_min, argc \
	);

#define lip_bind_assert_argc_at_most(arity_max) \
	lip_bind_assert_fmt( \
		argc <= arity_max, \
		"Bad number of arguments (at most %u expected, got %u)", \
		arity_max, argc \
	);

#define lip_bind_assert_argc(arity) \
	lip_bind_assert_fmt( \
		argc == arity, \
		"Bad number of arguments (exactly %d expected, got %d)", \
		arity, argc \
	);

/**
 * @brief Create a wrapper for a function.
 *
 * Currently, only types described in #lip_bind_args are supported.
 *
 * @param name Name of the function.
 * @param return_type Return type of the function.
 * @param ... Argument types of the function.
 *
 * @see lip_bind_args
 */
#define lip_bind_wrap_function(name, return_type, ...) \
	lip_function(lip_bind_wrapper(name)) { \
		lip_bind_args(lip_pp_map(lip_bind_wrapper_gen_binding, __VA_ARGS__)); \
		lip_pp_concat(lip_bind_store_, return_type)( \
			(*result), \
			name(lip_pp_map(lip_bind_wrapper_ref_binding, __VA_ARGS__)) \
		); \
		return LIP_EXEC_OK; \
	}

#define lip_bind_wrapper_gen_binding(i, type) lip_pp_sep(i) (type, lip_pp_concat(arg, i))
#define lip_bind_wrapper_ref_binding(i, type) lip_pp_sep(i) lip_pp_concat(arg, i)

/// Retrieve the name of a wrapper function created with #lip_bind_wrap_function.
#define lip_bind_wrapper(name) lip_pp_concat(lip_, lip_pp_concat(name, _wrapper))

#define lip_bind_declare_any(name) lip_value_t name;
#define lip_bind_load_any(i, name, value) name = value;

#define lip_bind_declare_string(name) lip_value_t name;
#define lip_bind_load_string(i, name, value) \
	do { \
		lip_bind_check_type(i, LIP_VAL_STRING, value.type); \
		name = value; \
	} while(0)

#define lip_bind_declare_symbol(name) lip_value_t name;
#define lip_bind_load_symbol(i, name, value) \
	do { \
		lip_bind_check_type(i, LIP_VAL_SYMBOL, value.type); \
		name = value; \
	} while(0)

#define lip_bind_declare_boolean(name) bool name;
#define lip_bind_load_boolean(i, name, value) \
	do { \
		lip_bind_check_type(i, LIP_VAL_BOOLEAN, value.type); \
		name = value.data.boolean; \
	} while(0)

#define lip_bind_declare_number(name) double name;
#define lip_bind_load_number(i, name, value) \
	do { \
		lip_bind_check_type(i, LIP_VAL_NUMBER, value.type); \
		name = value.data.number; \
	} while(0)
#define lip_bind_store_number(target, value) \
	target = (lip_value_t){ \
		.type = LIP_VAL_NUMBER, \
		.data = { .number = value } \
	}

#define lip_bind_declare_list(name) lip_value_t name;
#define lip_bind_load_list(i, name, value) \
	do { \
		lip_bind_check_type(i, LIP_VAL_LIST, value.type); \
		name = value; \
	} while(0)

#define lip_bind_declare_function(name) lip_value_t name;
#define lip_bind_load_function(i, name, value) \
	do { \
		lip_bind_check_type(i, LIP_VAL_FUNCTION, value.type); \
		name = value; \
	} while(0)

#define lip_bind_check_type(i, expected_type, actual_type) \
	lip_bind_assert_fmt( \
		expected_type == actual_type, \
		"Bad argument #%d (%s expected, got %s)", \
		i, lip_value_type_t_to_str(expected_type), lip_value_type_t_to_str(actual_type) \
	)

/**
 * @brief Assert inside a binding function.
 *
 * @param cond Condition to assert.
 * @param msg Error message when assertion fails.
 */
#define lip_bind_assert(cond, msg) \
	do { if(!(cond)) { lip_throw(msg); } } while(0)

/**
 * @brief Assert inside a binding function.
 *
 * @param cond Condition to assert.
 * @param fmt Format string.
 * @param ... Parameters for format string.
 */
#define lip_bind_assert_fmt(cond, fmt, ...) \
	do { if(!(cond)) { lip_throw_fmt(fmt, __VA_ARGS__); } } while(0)

/// Return from a binding function.
#define lip_return(val) do { *result = val; return LIP_EXEC_OK; } while(0)

/**
 * @brief Throw an error from a binding function.
 *
 * @param err Error message.
 */
#define lip_throw(err) \
		do { \
			*result = lip_make_string_copy(vm, lip_string_ref(err)); \
			lip_bind_track_native_location(vm); \
			return LIP_EXEC_ERROR; \
		} while(0)

/**
 * @brief Throw an error from a binding function.
 *
 * @param fmt Format string.
 * @param ... Parameters for format string.
 */
#define lip_throw_fmt(fmt, ...) \
		do { \
			*result = lip_make_string(vm, fmt, __VA_ARGS__); \
			lip_bind_track_native_location(vm); \
			return LIP_EXEC_ERROR; \
		} while(0)

// Preprocessor abuse ahead

#define lip_pp_map(f, ...) \
	lip_pp_msvc_vararg_expand( \
        lip_pp_map1(f, lip_pp_concat(lip_pp_apply, lip_pp_len(__VA_ARGS__)), __VA_ARGS__) \
    )
#define lip_pp_map1(f, apply_n, ...) \
	lip_pp_msvc_vararg_expand( \
        apply_n(lip_pp_tail((lip_pp_seq())), f, __VA_ARGS__) \
    )
#define lip_pp_apply1(seq, f, a) f(lip_pp_head(seq), a)
#define lip_pp_apply2(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply1(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply3(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply2(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply4(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply3(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply5(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply4(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply6(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply5(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply7(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply6(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply8(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply7(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply9(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply8(lip_pp_tail(seq), f, __VA_ARGS__))
#define lip_pp_apply10(seq, f, a, ...) f(lip_pp_head(seq), a) lip_pp_msvc_vararg_expand(lip_pp_apply9(lip_pp_tail(seq), f, __VA_ARGS__))

#define lip_pp_len(...) lip_pp_msvc_vararg_expand(lip_pp_len1(__VA_ARGS__, lip_pp_inv_seq()))
#define lip_pp_len1(...) lip_pp_msvc_vararg_expand(lip_pp_len2(__VA_ARGS__))
#define lip_pp_len2( \
		x15, x14, x13, x12, \
		x11, x10,  x9,  x8, \
		 x7,  x6,  x5,  x4, \
		 x3,  x2,  x1,   N, \
         ...) N
#define lip_pp_inv_seq() \
		15, 14, 13, 12, 11, 10,  9,  8, \
		 7,  6,  5,  4,  3,  2,  1,  0
#define lip_pp_seq() \
		 0,  1,  2,  3,  4,  5,  6,  7, \
		 8,  9, 10, 11, 12, 13, 14, 15

#define lip_pp_concat(a, b) lip_pp_concat1(a, b)
#define lip_pp_concat1(a, b) lip_pp_concat2(a, b)
#define lip_pp_concat2(a, b) a ## b

#define lip_pp_nth(n, tuple, default_) \
	lip_pp_nth_(n, lip_pp_pad(tuple, default_))
#define lip_pp_nth_(n, tuple) lip_pp_concat(lip_pp_nth, n) tuple
#define lip_pp_nth1(x, ...) x
#define lip_pp_nth2(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth1(__VA_ARGS__))
#define lip_pp_nth3(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth2(__VA_ARGS__))
#define lip_pp_nth4(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth3(__VA_ARGS__))
#define lip_pp_nth5(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth4(__VA_ARGS__))
#define lip_pp_nth6(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth5(__VA_ARGS__))
#define lip_pp_nth7(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth6(__VA_ARGS__))
#define lip_pp_nth8(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth7(__VA_ARGS__))
#define lip_pp_nth9(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth8(__VA_ARGS__))
#define lip_pp_nth10(x, ...) lip_pp_msvc_vararg_expand(lip_pp_nth9(__VA_ARGS__))

#define lip_pp_pad(tuple, val) (lip_pp_expand(tuple), val, val, val, val, val, val, val, val, val, val, val, val, val, val, val, val)

#define lip_pp_expand(...) lip_pp_msvc_vararg_expand(lip_pp_expand1 __VA_ARGS__)
#define lip_pp_expand1(...) __VA_ARGS__

#define lip_pp_head(x) lip_pp_head_ x
#define lip_pp_head_(x, ...) x

#define lip_pp_tail(x) lip_pp_tail_ x
#define lip_pp_tail_(x, ...) (__VA_ARGS__)

#define lip_pp_sep(x) lip_pp_concat(lip_pp_sep_, x)
#define lip_pp_sep_1
#define lip_pp_sep_2 ,
#define lip_pp_sep_3 ,
#define lip_pp_sep_4 ,
#define lip_pp_sep_5 ,
#define lip_pp_sep_6 ,
#define lip_pp_sep_7 ,
#define lip_pp_sep_8 ,
#define lip_pp_sep_9 ,
#define lip_pp_sep_10 ,

#define lip_pp_msvc_vararg_expand(x) x

/**
 * @}
 */

#endif
