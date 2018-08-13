/*
  +----------------------------------------------------------------------+
  | PHP Version 7                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2018 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Martin Schröder <m.schroeder2007@gmail.com>                 |
  +----------------------------------------------------------------------+
*/

#include "php_async.h"

#include "async_helper.h"

#include "zend_inheritance.h"

zend_class_entry *async_process_builder_ce;
zend_class_entry *async_process_ce;
zend_class_entry *async_readable_pipe_ce;
zend_class_entry *async_writable_pipe_ce;

static zend_object_handlers async_process_builder_handlers;
static zend_object_handlers async_process_handlers;
static zend_object_handlers async_readable_pipe_handlers;
static zend_object_handlers async_writable_pipe_handlers;

const zend_uchar ASYNC_PROCESS_STDIN = 0;
const zend_uchar ASYNC_PROCESS_STDOUT = 1;
const zend_uchar ASYNC_PROCESS_STDERR = 2;

const zend_uchar ASYNC_PROCESS_STDIO_IGNORE = 0;
const zend_uchar ASYNC_PROCESS_STDIO_INHERIT = 1;
const zend_uchar ASYNC_PROCESS_STDIO_PIPE = 2;

#define ASYNC_PROCESS_BUILDER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_process_builder_ce, const_name, sizeof(const_name)-1, (zend_long)value);

static async_process *async_process_object_create();
static async_readable_pipe *async_readable_pipe_object_create(async_process *process, int i);
static async_writable_pipe *async_writable_pipe_object_create(async_process *process);


static void configure_stdio(async_process_builder *builder, int i, zend_long mode, zend_long fd, zend_execute_data *execute_data)
{
	if (mode == ASYNC_PROCESS_STDIO_IGNORE) {
		builder->stdio[i].flags = UV_IGNORE;
	} else if (mode == ASYNC_PROCESS_STDIO_INHERIT) {
		if (fd < 0 || fd > 2) {
			zend_throw_error(NULL, "Unsupported file descriptor, only STDIN, STDOUT and STDERR are supported");
			return;
		}

		if (i) {
			if (fd == 0) {
				zend_throw_error(NULL, "STDIN cannot be used as process output pipe");
				return;
			}
		} else {
			if (fd != 0) {
				zend_throw_error(NULL, "Only STDIN is supported as process input pipe");
				return;
			}
		}

		builder->stdio[i].flags = UV_INHERIT_FD;
		builder->stdio[i].data.fd = (int) fd;
	} else if (mode == ASYNC_PROCESS_STDIO_PIPE) {
		builder->stdio[i].flags = UV_CREATE_PIPE | UV_OVERLAPPED_PIPE;
		builder->stdio[i].flags |= i ? UV_WRITABLE_PIPE : UV_READABLE_PIPE;
	} else {
		zend_throw_error(NULL, "Unsupported process STDIO mode");
		return;
	}
}

static void dispose_write_state(uv_handle_t * handle)
{
	async_writable_pipe_state *state;

	state = (async_writable_pipe_state *) handle->data;

	OBJ_RELEASE(&state->process->std);
}

static void async_process_closed(uv_handle_t *handle)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	if (proc->options.stdio[0].flags & UV_CREATE_PIPE) {
		if (proc->stdin_state.writes.first != NULL) {
			zend_throw_error(NULL, "Process has been closed");

			ZVAL_OBJ(&proc->stdin_state.error, EG(exception));
			EG(exception) = NULL;

			async_awaitable_trigger_continuation(&proc->stdin_state.writes, &proc->stdin_state.error, 0);
		}

		if (!uv_is_closing((uv_handle_t *) &proc->stdin_state.handle)) {
			GC_ADDREF(&proc->std);

			uv_close((uv_handle_t *) &proc->stdin_state.handle, dispose_write_state);
		}
	}

	if (proc->stdout_pipe != NULL) {
		OBJ_RELEASE(&proc->stdout_pipe->std);
	}

	if (proc->stderr_pipe != NULL) {
		OBJ_RELEASE(&proc->stderr_pipe->std);
	}

	OBJ_RELEASE(&proc->std);
}

static void async_process_exit(uv_process_t *handle, int64_t status, int signal)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	GC_ADDREF(&proc->std);

	ZVAL_LONG(&proc->pid, 0);
	ZVAL_LONG(&proc->exit_code, status);

	async_awaitable_trigger_continuation(&proc->observers, &proc->exit_code, 1);

	uv_close((uv_handle_t *) &proc->handle, async_process_closed);
}

static void prepare_process(async_process_builder *builder, async_process *proc, zval *params, uint32_t count, zval *return_value, zend_execute_data *execute_data)
{
	char **args;
	uint32_t i;

	args = ecalloc(sizeof(char *), count + 2);
	args[0] = builder->command;

	for (i = 0; i < count; i++) {
		args[i + 1] = Z_STRVAL_P(&params[i]);
	}

	args[count + 1] = NULL;

	proc->options.file = builder->command;
	proc->options.args = args;
	proc->options.stdio_count = 3;
	proc->options.stdio = builder->stdio;
	proc->options.exit_cb = async_process_exit;
	proc->options.flags = UV_PROCESS_WINDOWS_HIDE;
}

static void readable_pipe_closed(uv_handle_t *handle)
{
	async_readable_pipe *pipe;

	pipe = (async_readable_pipe *) handle->data;

	if (pipe->process != NULL) {
		OBJ_RELEASE(&pipe->process->std);
		pipe->process = NULL;
	}

	OBJ_RELEASE(&pipe->std);
}

static void pipe_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
	async_readable_pipe *pipe;
	async_awaitable_cb *cb;

	int size;

	pipe = (async_readable_pipe *) handle->data;

	ZEND_ASSERT(pipe->reads.first != NULL);

	cb = pipe->reads.first;

	while (Z_TYPE_P(&cb->data) == IS_UNDEF) {
		cb = cb->next;
	}

	size = (int) Z_LVAL_P(&cb->data);
	ZVAL_UNDEF(&cb->data);

	buffer->base = emalloc(size);
	buffer->len = MIN(size, suggested_size);
}

static void pipe_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
	async_readable_pipe *pipe;

	zval data;

	pipe = (async_readable_pipe *) stream->data;

	if (nread > 0) {
		ZVAL_STRINGL(&data, buffer->base, nread);

		efree(buffer->base);

		async_awaitable_trigger_next_continuation(&pipe->reads, &data, 1);

		zval_ptr_dtor(&data);

		if (pipe->reads.first == NULL) {
			uv_read_stop(stream);
		}

		return;
	}

	if (buffer->base != NULL) {
		efree(buffer->base);
	}

	if (nread == 0) {
		return;
	}

	uv_read_stop(stream);

	if (nread == UV_EOF) {
		pipe->eof = 1;

		ZVAL_NULL(&data);

		async_awaitable_trigger_continuation(&pipe->reads, &data, 1);

		GC_ADDREF(&pipe->std);

		uv_close((uv_handle_t *) stream, readable_pipe_closed);

		return;
	}

	zend_throw_error(NULL, "Pipe read error: %s", uv_strerror((int) nread));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&pipe->reads, &data, 0);
}

static void pipe_write(uv_write_t *write, int status)
{
	async_writable_pipe_state *state;

	zval data;

	state = (async_writable_pipe_state *) write->handle->data;

	if (status == 0) {
		ZVAL_NULL(&data);

		async_awaitable_trigger_next_continuation(&state->writes, &data, 1);

		return;
	}

	zend_throw_error(NULL, "Pipe write error: %s", uv_strerror(status));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_next_continuation(&state->writes, &data, 0);
}


static zend_object *async_process_builder_object_create(zend_class_entry *ce)
{
	async_process_builder *builder;

	builder = emalloc(sizeof(async_process_builder));
	ZEND_SECURE_ZERO(builder, sizeof(async_process_builder));

	zend_object_std_init(&builder->std, ce);
	builder->std.handlers = &async_process_builder_handlers;

	return &builder->std;
}

static void async_process_builder_object_destroy(zend_object *object)
{
	async_process_builder *builder;

	builder = (async_process_builder *) object;

	zend_object_std_dtor(&builder->std);
}

ZEND_METHOD(ProcessBuilder, __construct)
{
	async_process_builder *builder;

	size_t len;

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STRING(builder->command, len)
	ZEND_PARSE_PARAMETERS_END();
}

ZEND_METHOD(ProcessBuilder, configureStdin)
{
	async_process_builder *builder;

	zend_long mode;
	zend_long fd;

	fd = 0;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_LONG(mode)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(fd)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	configure_stdio(builder, 0, mode, fd, execute_data);
}

ZEND_METHOD(ProcessBuilder, configureStdout)
{
	async_process_builder *builder;

	zend_long mode;
	zend_long fd;

	fd = 0;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_LONG(mode)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(fd)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	configure_stdio(builder, 1, mode, fd, execute_data);
}

ZEND_METHOD(ProcessBuilder, configureStderr)
{
	async_process_builder *builder;

	zend_long mode;
	zend_long fd;

	fd = 0;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_LONG(mode)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(fd)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	configure_stdio(builder, 2, mode, fd, execute_data);
}

ZEND_METHOD(ProcessBuilder, execute)
{
	async_process_builder *builder;
	async_process *proc;

	uint32_t i;
	uint32_t count;
	zval *params;

	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, -1)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	for (i = 0; i < 3; i++) {
		if (builder->stdio[i].flags & UV_CREATE_PIPE) {
			zend_throw_error(NULL, "Cannot use STDIO pipe in execute(), use start() instead");
			return;
		}
	}

	proc = async_process_object_create();

	prepare_process(builder, proc, params, count, return_value, execute_data);

	code = uv_spawn(async_task_scheduler_get_loop(), &proc->handle, &proc->options);

	efree(proc->options.args);

	if (code != 0) {
		zend_throw_error(NULL, "Failed to launch process \"%s\": %s", builder->command, uv_strerror(code));
	} else {
		async_task_suspend(&proc->observers, return_value, execute_data, 0, NULL);
	}

	OBJ_RELEASE(&proc->std);
}

static void create_writable_state(async_process *process, async_writable_pipe_state *state)
{
	uv_pipe_init(async_task_scheduler_get_loop(), &state->handle, 0);

	state->handle.data = state;
	state->process = process;

	process->options.stdio[0].data.stream = (uv_stream_t *) &state->handle;
}

ZEND_METHOD(ProcessBuilder, start)
{
	async_process_builder *builder;
	async_process *proc;

	uint32_t count;
	zval *params;
	zval obj;

	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, -1)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());
	proc = async_process_object_create();

	prepare_process(builder, proc, params, count, return_value, execute_data);

	if (proc->options.stdio[0].flags & UV_CREATE_PIPE) {
		create_writable_state(proc, &proc->stdin_state);
	}

	if (proc->options.stdio[1].flags & UV_CREATE_PIPE) {
		proc->stdout_pipe = async_readable_pipe_object_create(proc, 1);
	}

	if (proc->options.stdio[2].flags & UV_CREATE_PIPE) {
		proc->stderr_pipe = async_readable_pipe_object_create(proc, 2);
	}

	code = uv_spawn(async_task_scheduler_get_loop(), &proc->handle, &proc->options);

	efree(proc->options.args);

	if (code != 0) {
		zend_throw_error(NULL, "Failed to launch process \"%s\": %s", builder->command, uv_strerror(code));

		OBJ_RELEASE(&proc->std);
		return;
	}

	ZVAL_LONG(&proc->pid, proc->handle.pid);

	ZVAL_OBJ(&obj, &proc->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_process_builder_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, command, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_configure_stdin, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, mode, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, fd, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_configure_stdout, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, mode, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, fd, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_configure_stderr, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, mode, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, fd, IS_LONG, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_execute, 0, 0, IS_LONG, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, arguments, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_process_builder_start, 0, 0, Concurrent\\Process\\Process, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, arguments, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_process_builder_functions[] = {
	ZEND_ME(ProcessBuilder, __construct, arginfo_process_builder_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(ProcessBuilder, configureStdin, arginfo_process_builder_configure_stdin, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, configureStdout, arginfo_process_builder_configure_stdout, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, configureStderr, arginfo_process_builder_configure_stderr, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, execute, arginfo_process_builder_execute, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, start, arginfo_process_builder_start, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};

static async_process *async_process_object_create()
{
	async_process *proc;

	proc = emalloc(sizeof(async_process));
	ZEND_SECURE_ZERO(proc, sizeof(async_process));

	zend_object_std_init(&proc->std, async_process_ce);
	proc->std.handlers = &async_process_handlers;

	proc->handle.data = proc;

	ZVAL_UNDEF(&proc->pid);
	ZVAL_LONG(&proc->exit_code, -1);

	ZVAL_UNDEF(&proc->stdin_state.error);

	return proc;
}

static void async_process_object_dtor(zend_object *object)
{
	async_process *proc;

	proc = (async_process *) object;

	if (Z_LVAL_P(&proc->exit_code) < 0) {
		uv_process_kill(&proc->handle, 2);
	}

	if (!uv_is_closing((uv_handle_t *) &proc->handle)) {
		GC_ADDREF(&proc->std);

		uv_close((uv_handle_t *) &proc->handle, async_process_closed);
	}
}

static void async_process_object_destroy(zend_object *object)
{
	async_process *proc;

	proc = (async_process *) object;

	zval_ptr_dtor(&proc->pid);
	zval_ptr_dtor(&proc->exit_code);

	zval_ptr_dtor(&proc->stdin_state.error);

	zend_object_std_dtor(&proc->std);
}

ZEND_METHOD(Process, __debugInfo)
{
	async_process *proc;
	HashTable *info;

	ZEND_PARSE_PARAMETERS_NONE();

	if (USED_RET()) {
		proc = (async_process *) Z_OBJ_P(getThis());
		info = async_info_init();

		async_info_prop(info, "pid", &proc->pid);
		async_info_prop(info, "exit_code", &proc->exit_code);
		async_info_prop_bool(info, "running", Z_LVAL_P(&proc->exit_code) < 0);

		RETURN_ARR(info);
	}
}

ZEND_METHOD(Process, isRunning)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	RETURN_BOOL(Z_LVAL_P(&proc->exit_code) < 0);
}

ZEND_METHOD(Process, getPid)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	RETURN_ZVAL(&proc->pid, 1, 0);
}

ZEND_METHOD(Process, getStdin)
{
	async_process *proc;
	async_writable_pipe *pipe;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(!(proc->options.stdio[0].flags & UV_CREATE_PIPE), "Cannot access STDIN because it is not configured to be a pipe");

	pipe = async_writable_pipe_object_create(proc);

	ZVAL_OBJ(&obj, &pipe->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Process, getStdout)
{
	async_process *proc;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(proc->stdout_pipe == NULL, "Cannot access STDOUT because it is not configured to be a pipe");

	GC_ADDREF(&proc->stdout_pipe->std);

	ZVAL_OBJ(&obj, &proc->stdout_pipe->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Process, getStderr)
{
	async_process *proc;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(proc->stderr_pipe == NULL, "Cannot access STDERR because it is not configured to be a pipe");

	GC_ADDREF(&proc->stderr_pipe->std);

	ZVAL_OBJ(&obj, &proc->stderr_pipe->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Process, awaitExit)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	if (Z_LVAL_P(&proc->exit_code) >= 0) {
		RETURN_ZVAL(&proc->exit_code, 1, 0);
	}

	async_task_suspend(&proc->observers, return_value, execute_data, 0, NULL);
}

ZEND_BEGIN_ARG_INFO(arginfo_process_debug_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_is_running, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_get_pid, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_process_get_stdin, 0, 0, Concurrent\\Stream\\WritableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_process_get_stdout, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_process_get_stderr, 0, 0, Concurrent\\Stream\\ReadableStream, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_await_exit, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_process_functions[] = {
	ZEND_ME(Process, __debugInfo, arginfo_process_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, isRunning, arginfo_process_is_running, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, getPid, arginfo_process_get_pid, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, getStdin, arginfo_process_get_stdin, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, getStdout, arginfo_process_get_stdout, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, getStderr, arginfo_process_get_stderr, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, awaitExit, arginfo_process_await_exit, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_readable_pipe *async_readable_pipe_object_create(async_process *process, int i)
{
	async_readable_pipe *pipe;

	pipe = emalloc(sizeof(async_readable_pipe));
	ZEND_SECURE_ZERO(pipe, sizeof(async_readable_pipe));

	zend_object_std_init(&pipe->std, async_readable_pipe_ce);
	pipe->std.handlers = &async_readable_pipe_handlers;

	pipe->process = process;

	GC_ADDREF(&process->std);

	ZVAL_UNDEF(&pipe->error);

	uv_pipe_init(async_task_scheduler_get_loop(), &pipe->handle, 0);

	pipe->handle.data = pipe;

	process->options.stdio[i].data.stream = (uv_stream_t *) &pipe->handle;

	return pipe;
}

static void async_readable_pipe_object_dtor(zend_object *object)
{
	async_readable_pipe *pipe;

	pipe = (async_readable_pipe *) object;

	if (!uv_is_closing((uv_handle_t *) &pipe->handle) && Z_TYPE_P(&pipe->error) == IS_UNDEF) {
		GC_ADDREF(&pipe->std);

		uv_close((uv_handle_t *) &pipe->handle, readable_pipe_closed);
	} else if (pipe->process != NULL) {
		OBJ_RELEASE(&pipe->process->std);
		pipe->process = NULL;
	}
}

static void async_readable_pipe_object_destroy(zend_object *object)
{
	async_readable_pipe *pipe;

	pipe = (async_readable_pipe *) object;

	zval_ptr_dtor(&pipe->error);

	zend_object_std_dtor(&pipe->std);
}

ZEND_METHOD(ReadablePipe, close)
{
	async_readable_pipe *pipe;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	pipe = (async_readable_pipe *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&pipe->error) != IS_UNDEF) {
		return;
	}

	zend_throw_error(NULL, "Pipe has been closed");

	ZVAL_OBJ(&pipe->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&pipe->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	if (pipe->eof == 0) {
		pipe->eof = 1;

		GC_ADDREF(&pipe->std);

		uv_close((uv_handle_t *) &pipe->handle, readable_pipe_closed);
	}

	async_awaitable_trigger_continuation(&pipe->reads, &pipe->error, 0);
}

ZEND_METHOD(ReadablePipe, read)
{
	async_readable_pipe *pipe;

	zval *hint;
	zval len;

	hint = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(hint)
	ZEND_PARSE_PARAMETERS_END();

	if (hint == NULL) {
		ZVAL_LONG(&len, 8192);
	} else if (Z_LVAL_P(hint) < 1) {
		zend_throw_error(NULL, "Invalid read length: %d", (int) Z_LVAL_P(hint));
		return;
	} else {
		ZVAL_COPY(&len, hint);
	}

	pipe = (async_readable_pipe *) Z_OBJ_P(getThis());

	if (pipe->eof) {
		return;
	}

	if (Z_TYPE_P(&pipe->error) != IS_UNDEF) {
		Z_ADDREF_P(&pipe->error);

		execute_data->opline--;
		zend_throw_exception_internal(&pipe->error);
		execute_data->opline++;

		return;
	}

	if (pipe->reads.first == NULL) {
		uv_read_start((uv_stream_t *) &pipe->handle, pipe_read_alloc, pipe_read);
	}

	async_task_suspend(&pipe->reads, return_value, execute_data, 0, &len);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_pipe_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_readable_pipe_read, 0, 0, IS_STRING, 1)
	ZEND_ARG_TYPE_INFO(0, length, IS_LONG, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_readable_pipe_functions[] = {
	ZEND_ME(ReadablePipe, close, arginfo_readable_pipe_close, ZEND_ACC_PUBLIC)
	ZEND_ME(ReadablePipe, read, arginfo_readable_pipe_read, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_writable_pipe *async_writable_pipe_object_create(async_process *process)
{
	async_writable_pipe *pipe;

	pipe = emalloc(sizeof(async_writable_pipe));
	ZEND_SECURE_ZERO(pipe, sizeof(async_writable_pipe));

	zend_object_std_init(&pipe->std, async_writable_pipe_ce);
	pipe->std.handlers = &async_writable_pipe_handlers;

	pipe->state = &process->stdin_state;

	GC_ADDREF(&pipe->state->process->std);

	return pipe;
}

static void async_writable_pipe_object_destroy(zend_object *object)
{
	async_writable_pipe *pipe;

	pipe = (async_writable_pipe *) object;

	OBJ_RELEASE(&pipe->state->process->std);

	zend_object_std_dtor(&pipe->std);
}

ZEND_METHOD(WritablePipe, close)
{
	async_writable_pipe *pipe;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	pipe = (async_writable_pipe *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&pipe->state->error) != IS_UNDEF) {
		return;
	}

	zend_throw_error(NULL, "Pipe has been closed");

	ZVAL_OBJ(&pipe->state->error, EG(exception));
	EG(exception) = NULL;

	GC_ADDREF(&pipe->state->process->std);

	async_awaitable_trigger_continuation(&pipe->state->writes, &pipe->state->error, 0);

	uv_close((uv_handle_t *) &pipe->state->handle, dispose_write_state);
}

ZEND_METHOD(WritablePipe, write)
{
	async_writable_pipe *pipe;

	zend_string *data;
	uv_write_t write;
	uv_buf_t buffer[1];

	int result;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(data)
	ZEND_PARSE_PARAMETERS_END();

	pipe = (async_writable_pipe *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&pipe->state->error) != IS_UNDEF) {
		Z_ADDREF_P(&pipe->state->error);

		execute_data->opline--;
		zend_throw_exception_internal(&pipe->state->error);
		execute_data->opline++;

		return;
	}

	buffer[0].base = ZSTR_VAL(data);
	buffer[0].len = ZSTR_LEN(data);

	// Attempt a non-blocking write first before queueing up writes.
	if (pipe->state->writes.first == NULL) {
		do {
			result = uv_try_write((uv_stream_t *) &pipe->state->handle, buffer, 1);

			if (result == UV_EAGAIN) {
				break;
			} else if (result < 0) {
				zend_throw_error(NULL, "Pipe write error: %s", uv_strerror(result));

				return;
			}

			if (result == buffer[0].len) {
				return;
			}

			buffer[0].base += result;
			buffer[0].len -= result;
		} while (1);
	}

	uv_write(&write, (uv_stream_t *) &pipe->state->handle, buffer, 1, pipe_write);

	async_task_suspend(&pipe->state->writes, return_value, execute_data, 0, NULL);
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_pipe_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_writable_pipe_write, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, data, IS_STRING, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_writable_pipe_functions[] = {
	ZEND_ME(WritablePipe, close, arginfo_writable_pipe_close, ZEND_ACC_PUBLIC)
	ZEND_ME(WritablePipe, write, arginfo_writable_pipe_write, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_process_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Process\\ProcessBuilder", async_process_builder_functions);
	async_process_builder_ce = zend_register_internal_class(&ce);
	async_process_builder_ce->ce_flags |= ZEND_ACC_FINAL;
	async_process_builder_ce->create_object = async_process_builder_object_create;
	async_process_builder_ce->serialize = zend_class_serialize_deny;
	async_process_builder_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_process_builder_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_process_builder_handlers.free_obj = async_process_builder_object_destroy;
	async_process_builder_handlers.clone_obj = NULL;

	ASYNC_PROCESS_BUILDER_CONST("STDIN", ASYNC_PROCESS_STDIN);
	ASYNC_PROCESS_BUILDER_CONST("STDOUT", ASYNC_PROCESS_STDOUT);
	ASYNC_PROCESS_BUILDER_CONST("STDERR", ASYNC_PROCESS_STDERR);

	ASYNC_PROCESS_BUILDER_CONST("STDIO_IGNORE", ASYNC_PROCESS_STDIO_IGNORE);
	ASYNC_PROCESS_BUILDER_CONST("STDIO_INHERIT", ASYNC_PROCESS_STDIO_INHERIT);
	ASYNC_PROCESS_BUILDER_CONST("STDIO_PIPE", ASYNC_PROCESS_STDIO_PIPE);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Process\\Process", async_process_functions);
	async_process_ce = zend_register_internal_class(&ce);
	async_process_ce->ce_flags |= ZEND_ACC_FINAL;
	async_process_ce->serialize = zend_class_serialize_deny;
	async_process_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_process_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_process_handlers.dtor_obj = async_process_object_dtor;
	async_process_handlers.free_obj = async_process_object_destroy;
	async_process_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Process\\ReadablePipe", async_readable_pipe_functions);
	async_readable_pipe_ce = zend_register_internal_class(&ce);
	async_readable_pipe_ce->ce_flags |= ZEND_ACC_FINAL;
	async_readable_pipe_ce->serialize = zend_class_serialize_deny;
	async_readable_pipe_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_readable_pipe_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_readable_pipe_handlers.dtor_obj = async_readable_pipe_object_dtor;
	async_readable_pipe_handlers.free_obj = async_readable_pipe_object_destroy;
	async_readable_pipe_handlers.clone_obj = NULL;

	zend_class_implements(async_readable_pipe_ce, 1, async_readable_stream_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\Process\\WritablePipe", async_writable_pipe_functions);
	async_writable_pipe_ce = zend_register_internal_class(&ce);
	async_writable_pipe_ce->ce_flags |= ZEND_ACC_FINAL;
	async_writable_pipe_ce->serialize = zend_class_serialize_deny;
	async_writable_pipe_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_writable_pipe_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_writable_pipe_handlers.free_obj = async_writable_pipe_object_destroy;
	async_writable_pipe_handlers.clone_obj = NULL;

	zend_class_implements(async_writable_pipe_ce, 1, async_writable_stream_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
