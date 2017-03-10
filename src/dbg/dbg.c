#include "dbg.h"
#include <inttypes.h>
#include <lip/memory.h>
#include <lip/io.h>
#include <lip/vm.h>
#include <lip/array.h>
#include <lip/print.h>
#include <cmp.h>
#define WBY_STATIC
#define WBY_IMPLEMENTATION
#include "vendor/wby.h"

#if defined(__unix__)
#	include <sched.h>
#	define yield() sched_yield()
#elif defined(_WIN32)
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#	define yield() SwitchToThread()
#else
#	define yield()
#endif

#define LIP_HAL_REL_BASE "http://lip.bullno1.com/hal/relations"

#define LIP_DBG(F) \
	F(LIP_DBG_BREAK) \
	F(LIP_DBG_CONTINUE) \
	F(LIP_DBG_STEP)

LIP_ENUM(lip_dbg_cmd_type_t, LIP_DBG)

struct lip_dbg_cmd_s
{
	lip_dbg_cmd_type_t type;
};

struct lip_dbg_s
{
	lip_vm_hook_t vtable;
	lip_dbg_config_t cfg;
	bool own_fs;
	struct wby_server server;
	struct lip_dbg_cmd_s cmd;
	lip_context_t* ctx;
	lip_vm_t* vm;
	lip_array(char) char_buf;
	lip_array(char) msg_buf;
	void* server_mem;
};

static const struct wby_header lip_msgpack_headers[] = {
	{ .name = "Content-Type", .value = "application/hal+msgpack" },
	{ .name = "Cache-Control", .value = "no-cache" },
	{ .name = "Access-Control-Allow-Origin", .value = "*" }
};

struct lip_dbg_msgpack_s
{
	struct wby_con* conn;
	cmp_ctx_t cmp;
	wby_size bytes_read;
	lip_array(char)* msg_buf;
};

static lip_string_ref_t
lip_string_ref_from_string(lip_string_t* str)
{
	return (lip_string_ref_t){
		.length = str->length,
		.ptr = str->ptr
	};
}

LIP_PRINTF_LIKE(2, 3) static size_t
lip_sprintf(lip_array(char)* buf, const char* fmt, ...)
{
	va_list args;
	struct lip_osstream_s osstream;
	lip_out_t* out = lip_make_osstream(buf, &osstream);
	va_start(args, fmt);
	size_t result = lip_vprintf(out, fmt, args);
	va_end(args);
	return result;
}

static bool
cmp_write_str_ref(cmp_ctx_t* cmp, lip_string_ref_t str)
{
	return cmp_write_str(cmp, str.ptr, str.length);
}

static bool
cmp_write_simple_link(cmp_ctx_t* cmp, const char* rel, const char* href)
{
	return cmp_write_str_ref(cmp, lip_string_ref(rel))
		&& cmp_write_map(cmp, 1)
		&& cmp_write_str_ref(cmp, lip_string_ref("href"))
		&& cmp_write_str_ref(cmp, lip_string_ref(href));
}

static bool
cmp_write_loc(cmp_ctx_t* cmp, lip_loc_t loc)
{
	return cmp_write_map(cmp, 2)
		&& cmp_write_str_ref(cmp, lip_string_ref("line"))
		&& cmp_write_u32(cmp, loc.line)
		&& cmp_write_str_ref(cmp, lip_string_ref("column"))
		&& cmp_write_u32(cmp, loc.column);
}

static bool
cmp_write_loc_range(cmp_ctx_t* cmp, lip_loc_range_t loc)
{
	return cmp_write_map(cmp, 2)
		&& cmp_write_str_ref(cmp, lip_string_ref("start"))
		&& cmp_write_loc(cmp, loc.start)
		&& cmp_write_str_ref(cmp, lip_string_ref("end"))
		&& cmp_write_loc(cmp, loc.end);
}

bool
cmp_write_curies(cmp_ctx_t* cmp)
{
	return cmp_write_str_ref(cmp, lip_string_ref("curies"))
		&& cmp_write_array(cmp, 1)
		&& cmp_write_map(cmp, 3)
		&& cmp_write_str_ref(cmp, lip_string_ref("name"))
		&& cmp_write_str_ref(cmp, lip_string_ref("lip"))
		&& cmp_write_str_ref(cmp, lip_string_ref("href"))
		&& cmp_write_str_ref(cmp, lip_string_ref(LIP_HAL_REL_BASE "/{rel}"))
		&& cmp_write_str_ref(cmp, lip_string_ref("templated"))
		&& cmp_write_bool(cmp, true);
}

static size_t
lip_dbg_msgpack_write(cmp_ctx_t* ctx, const void* data, size_t count)
{
	struct lip_dbg_msgpack_s* msgpack = ctx->buf;
	struct lip_osstream_s osstream;
	return lip_write(data, count, lip_make_osstream(msgpack->msg_buf, &osstream));
}

static bool
lip_dbg_msgpack_read(cmp_ctx_t* ctx, void* data, size_t count)
{
	struct lip_dbg_msgpack_s* msgpack = ctx->buf;
	if(msgpack->bytes_read + count < (size_t)msgpack->conn->request.content_length)
	{
		bool succeeded = wby_read(msgpack->conn, data, count) == 0;
		msgpack->bytes_read += count;
		return succeeded;
	}
	else
	{
		return false;
	}
}

static cmp_ctx_t*
lip_dbg_begin_msgpack(
	struct lip_dbg_msgpack_s* msgpack,
	lip_array(char)* msg_buf,
	struct wby_con* conn
)
{
	*msgpack = (struct lip_dbg_msgpack_s){
		.conn = conn,
		.msg_buf = msg_buf,
	};
	cmp_init(&msgpack->cmp, msgpack, lip_dbg_msgpack_read, lip_dbg_msgpack_write);
	lip_array_clear(*msg_buf);

	return &msgpack->cmp;
}

static void
lip_dbg_end_msgpack(struct lip_dbg_msgpack_s* msgpack)
{
	wby_response_begin(
		msgpack->conn, 200, lip_array_len(*msgpack->msg_buf),
		lip_msgpack_headers, LIP_STATIC_ARRAY_LEN(lip_msgpack_headers)
	);
	wby_write(msgpack->conn, *msgpack->msg_buf, lip_array_len(*msgpack->msg_buf));
	wby_response_end(msgpack->conn);
}

static int
lip_dbg_simple_response(struct wby_con* conn, int status)
{
	wby_response_begin(conn, status, 0, NULL, 0);
	wby_response_end(conn);
	return 0;
}

static bool
lip_str_startswith(const char* str, const char* prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

static int
lip_dbg_handle_src(lip_dbg_t* dbg, struct wby_con* conn)
{
	if(strcmp(conn->request.method, "GET") != 0)
	{
		return lip_dbg_simple_response(conn, 405);
	}

	lip_fs_t* fs = dbg->cfg.fs;
	lip_in_t* file =
		fs->begin_read(fs, lip_string_ref(conn->request.uri + sizeof("/src/") - 1));
	if(!file) { return lip_dbg_simple_response(conn, 404); }

	char buf[2048];

	const struct wby_header headers[] = {
		{ .name = "Content-Type", .value = "text/plain" },
		{ .name = "Access-Control-Allow-Origin", .value = "*" }
	};
	wby_response_begin(conn, 200, -1, headers, LIP_STATIC_ARRAY_LEN(headers));
	while(true)
	{
		size_t bytes_read = lip_read(buf, sizeof(buf), file);
		if(bytes_read == 0) { break; }

		wby_write(conn, buf, bytes_read);
	}
	wby_response_end(conn);

	fs->end_read(fs, file);
	return 0;
}

static void
lip_dbg_write_stack_frame(
	lip_dbg_t* dbg,
	cmp_ctx_t* cmp,
	lip_stack_frame_t* fp,
	uint16_t index,
	bool summary
)
{
	lip_string_ref_t filename;
	lip_loc_range_t location;

	if(lip_stack_frame_is_native(fp))
	{
		filename = lip_string_ref(
			fp->native_filename ? fp->native_filename : "<native>"
		);
		if(fp->native_line)
		{
			location = (lip_loc_range_t){
				.start = { .line = fp->native_line },
				.end = { .line = fp->native_line }
			};
		}
		else
		{
			location = LIP_LOC_NOWHERE;
		}
	}
	else
	{
		lip_function_layout_t function_layout;
		lip_function_layout(fp->closure->function.lip, &function_layout);
		filename = lip_string_ref_from_string(function_layout.source_name);
		location = function_layout.locations[LIP_MAX(0, fp->pc - function_layout.instructions)];
	}

	lip_string_ref_t function_name;
	if(fp->closure == NULL)
	{
		function_name = fp->native_function
			? lip_string_ref(fp->native_function)
			: lip_string_ref("?");
	}
	else if(fp->closure->debug_name)
	{
		function_name = lip_string_ref_from_string(fp->closure->debug_name);
	}
	else if(fp->native_function)
	{
		function_name = lip_string_ref(fp->native_function);
	}
	else
	{
		function_name =  lip_string_ref("?");
	}

	cmp_write_map(cmp, 4);
	{
		cmp_write_str_ref(cmp, lip_string_ref("_links"));
		cmp_write_map(cmp, 2);
		{
			cmp_write_str_ref(cmp, lip_string_ref("self"));
			cmp_write_map(cmp, 1);
			{
				cmp_write_str_ref(cmp, lip_string_ref("href"));
				lip_array_clear(dbg->char_buf);
				lip_sprintf(&dbg->char_buf, "/vm/call_stack/%" PRIu16, index);
				cmp_write_str(cmp, dbg->char_buf, lip_array_len(dbg->char_buf));
			}

			cmp_write_str_ref(
				cmp,
				summary
					? lip_string_ref("lip:src")
					: lip_string_ref(LIP_HAL_REL_BASE "/src")
			);
			cmp_write_map(cmp, 1);
			{
				cmp_write_str_ref(cmp, lip_string_ref("href"));
				lip_array_clear(dbg->char_buf);
				lip_sprintf(&dbg->char_buf, "/src/%.*s", (int)filename.length, filename.ptr);
				cmp_write_str(cmp, dbg->char_buf, lip_array_len(dbg->char_buf));
			}
		}

		cmp_write_str_ref(cmp, lip_string_ref("filename"));
		cmp_write_str_ref(cmp, filename);

		cmp_write_str_ref(cmp, lip_string_ref("location"));
		cmp_write_loc_range(cmp, location);

		cmp_write_str_ref(cmp, lip_string_ref("function_name"));
		cmp_write_str_ref(cmp, function_name);
	}
}

static void
lip_dbg_write_call_stack(lip_dbg_t* dbg, lip_vm_t* vm, cmp_ctx_t* cmp)
{
	cmp_write_map(cmp, 2);
	{
		cmp_write_str_ref(cmp, lip_string_ref("_links"));
		cmp_write_map(cmp, 2);
		{
			cmp_write_simple_link(cmp, "self", "/vm/call_stack");
			cmp_write_curies(cmp);
		}

		cmp_write_str_ref(cmp, lip_string_ref("_embedded"));
		cmp_write_map(cmp, 1);
		{
			cmp_write_str_ref(cmp, lip_string_ref("item"));

			lip_memblock_info_t os_block, env_block, cs_block;
			lip_vm_memory_layout(&vm->config, &os_block, &env_block, &cs_block);
			lip_stack_frame_t* fp_min = lip_locate_memblock(vm->mem, &cs_block);
			size_t num_frames = vm->fp - fp_min + 1;

			cmp_write_array(cmp, num_frames);
			unsigned int i = 0;
			for(lip_stack_frame_t* fp = vm->fp; fp >= fp_min; --fp, ++i)
			{
				lip_dbg_write_stack_frame(dbg, cmp, fp, i, true);
			}
		}
	}
}

static int
lip_dbg_handle_call_stack(lip_dbg_t* dbg, struct wby_con* conn)
{
	if(strcmp(conn->request.method, "GET") != 0)
	{
		return lip_dbg_simple_response(conn, 405);
	}

	struct lip_dbg_msgpack_s msgpack;
	cmp_ctx_t* cmp = lip_dbg_begin_msgpack(&msgpack, &dbg->msg_buf, conn);

	lip_dbg_write_call_stack(dbg, dbg->vm, cmp);

	lip_dbg_end_msgpack(&msgpack);

	return 0;
}

static void
lip_dbg_write_vm(lip_dbg_t* dbg, lip_vm_t* vm, cmp_ctx_t* cmp)
{
	lip_memblock_info_t os_block, env_block, cs_block;
	lip_vm_memory_layout(&vm->config, &os_block, &env_block, &cs_block);

	cmp_write_map(cmp, 4);
	{
		cmp_write_str_ref(cmp, lip_string_ref("_links"));
		cmp_write_map(cmp, 1);
		{
			cmp_write_simple_link(cmp, "self", "/vm");
		}

		cmp_write_str_ref(cmp, lip_string_ref("status"));
		cmp_write_str_ref(cmp, lip_string_ref(lip_exec_status_t_to_str(vm->status)));

		cmp_write_str_ref(cmp, lip_string_ref("cfg"));
		cmp_write_map(cmp, 3);
		{
			cmp_write_str_ref(cmp, lip_string_ref("os_len"));
			cmp_write_integer(cmp, vm->config.os_len);

			cmp_write_str_ref(cmp, lip_string_ref("cs_len"));
			cmp_write_integer(cmp, vm->config.cs_len);

			cmp_write_str_ref(cmp, lip_string_ref("env_len"));
			cmp_write_integer(cmp, vm->config.env_len);
		}

		cmp_write_str_ref(cmp, lip_string_ref("_embedded"));
		cmp_write_map(cmp, 1);
		{
			cmp_write_str_ref(cmp, lip_string_ref(LIP_HAL_REL_BASE  "/call_stack"));
			lip_dbg_write_call_stack(dbg, vm, cmp);
		}
	}
}

static int
lip_dbg_handle_vm(lip_dbg_t* dbg, struct wby_con* conn)
{
	if(strcmp(conn->request.method, "GET") != 0)
	{
		return lip_dbg_simple_response(conn, 405);
	}

	struct lip_dbg_msgpack_s msgpack;
	cmp_ctx_t* cmp = lip_dbg_begin_msgpack(&msgpack, &dbg->msg_buf, conn);

	lip_dbg_write_vm(dbg, dbg->vm, cmp);

	lip_dbg_end_msgpack(&msgpack);

	return 0;
}

static int
lip_dbg_handle_dbg(lip_dbg_t* dbg, struct wby_con* conn)
{
	if(strcmp(conn->request.method, "GET") != 0)
	{
		return lip_dbg_simple_response(conn, 405);
	}

	struct lip_dbg_msgpack_s msgpack;
	cmp_ctx_t* cmp = lip_dbg_begin_msgpack(&msgpack, &dbg->msg_buf, conn);

	cmp_write_map(cmp, 3);
	{
		cmp_write_str_ref(cmp, lip_string_ref("command"));
		cmp_write_str_ref(cmp, lip_string_ref(lip_dbg_cmd_type_t_to_str(dbg->cmd.type)));

		cmp_write_str_ref(cmp, lip_string_ref("_links"));
		cmp_write_map(cmp, 2);
		{
			cmp_write_simple_link(cmp, "self", "/dbg");
			cmp_write_simple_link(cmp, LIP_HAL_REL_BASE "/command", "/command");
		}

		cmp_write_str_ref(cmp, lip_string_ref("_embedded"));
		cmp_write_map(cmp, 1);
		{
			cmp_write_str_ref(cmp, lip_string_ref(LIP_HAL_REL_BASE "/vm"));
			lip_dbg_write_vm(dbg, dbg->vm, cmp);
		}
	}

	lip_dbg_end_msgpack(&msgpack);
	return 0;
}

static int
lip_dbg_handle_command(lip_dbg_t* dbg, struct wby_con* conn)
{
#define LIP_MSGPACK_READ(op, ctx, ...) \
	LIP_ENSURE(cmp_read_##op (ctx, __VA_ARGS__))
#define LIP_ENSURE(cond) \
	do { \
		if(!(cond)) { \
			return lip_dbg_simple_response(conn, 400); \
		} \
	} while(0)

	if(strcmp(conn->request.method, "POST") != 0)
	{
		return lip_dbg_simple_response(conn, 405);
	}

	struct lip_dbg_msgpack_s msgpack;
	cmp_ctx_t* cmp = lip_dbg_begin_msgpack(&msgpack, &dbg->msg_buf, conn);

	cmp_object_t obj;
	LIP_MSGPACK_READ(object, cmp, &obj);
	if(false
		|| obj.type == CMP_TYPE_STR8
		|| obj.type == CMP_TYPE_STR16
		|| obj.type == CMP_TYPE_STR32
		|| obj.type == CMP_TYPE_FIXSTR
	)
	{
		char cmd_buf[32];
		LIP_ENSURE(obj.as.str_size < sizeof(cmd_buf));
		LIP_ENSURE(wby_read(conn, cmd_buf, obj.as.str_size) == 0);
		cmd_buf[obj.as.str_size] = '\0';

		if(strcmp(cmd_buf, "step") == 0)
		{
			dbg->cmd = (struct lip_dbg_cmd_s) {
				.type = LIP_DBG_STEP
			};
			return lip_dbg_simple_response(conn, 202);
		}
		else if(strcmp(cmd_buf, "continue") == 0)
		{
			dbg->cmd = (struct lip_dbg_cmd_s) {
				.type = LIP_DBG_CONTINUE
			};
			return lip_dbg_simple_response(conn, 202);
		}
		else if(strcmp(cmd_buf, "break") == 0)
		{
			dbg->cmd = (struct lip_dbg_cmd_s) {
				.type = LIP_DBG_BREAK
			};
			return lip_dbg_simple_response(conn, 202);
		}
		else
		{
			return lip_dbg_simple_response(conn, 400);
		}
	}
	else
	{
		return lip_dbg_simple_response(conn, 400);
	}

	lip_dbg_end_msgpack(&msgpack);
	return 0;
}

static int
lip_dbg_dispatch(struct wby_con* conn, void* userdata)
{
	lip_dbg_t* dbg = userdata;
	const char* uri = conn->request.uri;

	if(strcmp(uri, "/dbg") == 0)
	{
		return lip_dbg_handle_dbg(dbg, conn);
	}
	else if(strcmp(uri, "/vm") == 0)
	{
		return lip_dbg_handle_vm(dbg, conn);
	}
	else if(strcmp(uri, "/vm/call_stack") == 0)
	{
		return lip_dbg_handle_call_stack(dbg, conn);
	}
	else if(lip_str_startswith(uri, "/src/"))
	{
		return lip_dbg_handle_src(dbg, conn);
	}
	else if(strcmp(uri, "/command") == 0)
	{
		return lip_dbg_handle_command(dbg, conn);
	}
	else
	{
		return 1;
	}
}

static int
lip_dbg_ws_connect(struct wby_con* conn, void* userdata)
{
	(void)conn;
	(void)userdata;
	return 1;
}

static void
lip_dbg_ws_connected(struct wby_con* conn, void* userdata)
{
	(void)conn;
	(void)userdata;
}

static void
lip_dbg_ws_closed(struct wby_con* conn, void* userdata)
{
	(void)conn;
	(void)userdata;
}

static int
lip_dbg_ws_frame(
	struct wby_con* connection, const struct wby_frame* frame, void* userdata
)
{
	(void)connection;
	(void)frame;
	(void)userdata;
	return 0;
}

static void
lip_dbg_step(
	lip_vm_hook_t* vtable, const lip_vm_t* vm
)
{
	(void)vm;
	lip_dbg_t* dbg = LIP_CONTAINER_OF(vtable, lip_dbg_t, vtable);

	wby_update(&dbg->server);

	while(dbg->cmd.type == LIP_DBG_BREAK)
	{
		wby_update(&dbg->server);
		if(dbg->cmd.type != LIP_DBG_BREAK) { break; }
		yield();
	}

	if(dbg->cmd.type == LIP_DBG_STEP)
	{
		dbg->cmd.type = LIP_DBG_BREAK;
	}
}

void
lip_reset_dbg_config(lip_dbg_config_t* cfg)
{
	*cfg = (lip_dbg_config_t){
		.allocator = lip_default_allocator,
		.port = 8081
	};
}

lip_dbg_t*
lip_create_debugger(lip_dbg_config_t* cfg)
{
	lip_dbg_t* dbg = lip_new(cfg->allocator, lip_dbg_t);
	*dbg = (lip_dbg_t){
		.vtable = { .step = lip_dbg_step },
		.cfg = *cfg,
		.own_fs = cfg->fs == NULL,
		.cmd = { .type = LIP_DBG_BREAK },
		.char_buf = lip_array_create(cfg->allocator, char, 64),
		.msg_buf = lip_array_create(cfg->allocator, char, 1024),
	};

	if(dbg->own_fs)
	{
		dbg->cfg.fs = lip_create_native_fs(cfg->allocator);
	}

	struct wby_config wby_config = {
		.address = "127.0.0.1",
		.port = cfg->port,
		.connection_max = 4,
		.request_buffer_size = 4096,
		.io_buffer_size = 4096,
		.dispatch = lip_dbg_dispatch,
		.ws_connect = lip_dbg_ws_connect,
		.ws_connected = lip_dbg_ws_connected,
		.ws_closed = lip_dbg_ws_closed,
		.ws_frame = lip_dbg_ws_frame,
		.userdata = dbg
	};
	size_t needed_memory;
	wby_init(&dbg->server, &wby_config, &needed_memory);
	dbg->server_mem = lip_malloc(cfg->allocator, needed_memory);
	wby_start(&dbg->server, dbg->server_mem);

	return dbg;
}

void
lip_destroy_debugger(lip_dbg_t* dbg)
{
	wby_stop(&dbg->server);
	if(dbg->own_fs)
	{
		lip_destroy_native_fs(dbg->cfg.fs);
	}

	lip_array_destroy(dbg->msg_buf);
	lip_array_destroy(dbg->char_buf);
	lip_free(dbg->cfg.allocator, dbg->server_mem);
	lip_free(dbg->cfg.allocator, dbg);
}

void
lip_attach_debugger(lip_dbg_t* dbg, lip_vm_t* vm)
{
	lip_set_vm_hook(vm, &dbg->vtable);
	dbg->vm = vm;
}
