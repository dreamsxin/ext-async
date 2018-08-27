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

const zend_uchar ASYNC_PROCESS_STDIO_IGNORE = 16;
const zend_uchar ASYNC_PROCESS_STDIO_INHERIT = 17;
const zend_uchar ASYNC_PROCESS_STDIO_PIPE = 18;

#define ASYNC_PROCESS_BUILDER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_process_builder_ce, const_name, sizeof(const_name)-1, (zend_long)value);

static async_process *async_process_object_create();
static async_readable_pipe *async_readable_pipe_object_create(async_process *process, async_readable_pipe_state *state);
static async_writable_pipe *async_writable_pipe_object_create(async_process *process, async_writable_pipe_state *state);

#ifdef ZEND_WIN32

static char **populate_env(HashTable *add, zend_bool inherit)
{
	char **env;
	zend_string *key;
	zval *v;

	LPTCH envstr;
	char t;

	size_t len;
	int count;
	int i;
	char *j;

	count = 0;

	if (inherit) {
		envstr = GetEnvironmentStrings();

		for (i = 0;; i++) {
			if (envstr[i] == '\0') {
				if (t == '\0') {
					break;
				}

				count++;
			}

			t = envstr[i];
		}

		count -= 3;
	}

	env = ecalloc(zend_hash_num_elements(add) + count + 1, sizeof(char *));
	i = 0;

	if (inherit) {
		j = envstr;

		while (*j != '\0') {
			len = strlen(j) + 1;

			if (*j != '=') {
				env[i] = emalloc(sizeof(char) * len);
				memcpy(env[i++], j, len);
			}

			j += len;
		}

		FreeEnvironmentStrings(envstr);
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(add, key, v) {
		env[i] = emalloc(sizeof(char) * key->len + Z_STRLEN_P(v) + 2);

		slprintf(env[i++], key->len + Z_STRLEN_P(v) + 2, "%s=%s", key->val, Z_STRVAL_P(v));
	} ZEND_HASH_FOREACH_END();

	env[i] = NULL;

	return env;
}

#else

extern char **environ;

static char **populate_env(HashTable *add, zend_bool inherit)
{
	char **env;
	zend_string *key;
	zval *v;

	size_t len;
	int count;
	int i;

	i = 0;
	count = 0;

	if (inherit) {
		while (NULL != environ[count]) {
			count++;
		}
	}

	env = ecalloc(zend_hash_num_elements(add) + count + 1, sizeof(char *));

	if (inherit) {
		for (; i < count; i++) {
			len = strlen(environ[i]) + 1;

			env[i] = emalloc(len);
			memcpy(env[i], environ[i], len);
		}
	}

	ZEND_HASH_FOREACH_STR_KEY_VAL(add, key, v) {
		env[i] = emalloc(sizeof(char) * key->len + Z_STRLEN_P(v) + 2);

		slprintf(env[i++], key->len + Z_STRLEN_P(v) + 2, "%s=%s", key->val, Z_STRVAL_P(v));
	} ZEND_HASH_FOREACH_END();

	env[i] = NULL;

	return env;
}

#endif

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

static void dispose_process(uv_handle_t *handle)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	async_awaitable_trigger_continuation(&proc->observers, &proc->exit_code, 1);

	OBJ_RELEASE(&proc->std);
}

static void create_readable_state(async_process *process, async_readable_pipe_state *state, int i)
{
	uv_pipe_init(async_task_scheduler_get_loop(), &state->handle, 0);

	state->handle.data = state;
	state->process = process;

	process->options.stdio[i].data.stream = (uv_stream_t *) &state->handle;
	process->pipes++;

	state->buffer.size = 0x8000;
	state->buffer.base = emalloc(sizeof(char) * state->buffer.size);
}

static void dispose_read_state(uv_handle_t * handle)
{
	async_readable_pipe_state *state;

	state = (async_readable_pipe_state *) handle->data;
	state->process->pipes--;

	efree(state->buffer.base);
	state->buffer.base = NULL;

	if (state->process->pipes == 0 && Z_LVAL_P(&state->process->exit_code) >= 0) {
		uv_close((uv_handle_t *) &state->process->handle, dispose_process);
	} else {
		OBJ_RELEASE(&state->process->std);
	}
}

static void create_writable_state(async_process *process, async_writable_pipe_state *state, int i)
{
	uv_pipe_init(async_task_scheduler_get_loop(), &state->handle, 0);

	state->handle.data = state;
	state->process = process;

	process->options.stdio[i].data.stream = (uv_stream_t *) &state->handle;

	process->pipes++;
}

static void dispose_write_state(uv_handle_t * handle)
{
	async_writable_pipe_state *state;

	state = (async_writable_pipe_state *) handle->data;

	state->process->pipes--;

	if (state->process->pipes == 0 && Z_LVAL_P(&state->process->exit_code) >= 0) {
		uv_close((uv_handle_t *) &state->process->handle, dispose_process);
	} else {
		OBJ_RELEASE(&state->process->std);
	}
}

static void close_process(async_process *proc)
{
	zval data;

	if (proc->options.stdio[0].flags & UV_CREATE_PIPE) {
		if (proc->stdin_state.writes.first != NULL) {
			zend_throw_exception(async_stream_closed_exception_ce, "Process has been closed", 0);

			ZVAL_OBJ(&proc->stdin_state.error, EG(exception));
			EG(exception) = NULL;

			async_awaitable_trigger_continuation(&proc->stdin_state.writes, &proc->stdin_state.error, 0);
		}

		if (!uv_is_closing((uv_handle_t *) &proc->stdin_state.handle)) {
			GC_ADDREF(&proc->std);

			uv_close((uv_handle_t *) &proc->stdin_state.handle, dispose_write_state);
		}
	}

	if (proc->options.stdio[1].flags & UV_CREATE_PIPE) {
		proc->stdout_state.eof = 1;

		if (proc->stdout_state.reads.first != NULL) {
			ZVAL_NULL(&data);

			async_awaitable_trigger_continuation(&proc->stdout_state.reads, &data, 1);
		}

		if (!uv_is_closing((uv_handle_t *) &proc->stdout_state.handle)) {
			GC_ADDREF(&proc->std);

			uv_close((uv_handle_t *) &proc->stdout_state.handle, dispose_read_state);
		}
	}

	if (proc->options.stdio[2].flags & UV_CREATE_PIPE) {
		proc->stderr_state.eof = 1;

		if (proc->stderr_state.reads.first != NULL) {
			ZVAL_NULL(&data);

			async_awaitable_trigger_continuation(&proc->stderr_state.reads, &data, 1);
		}

		if (!uv_is_closing((uv_handle_t *) &proc->stderr_state.handle)) {
			GC_ADDREF(&proc->std);

			uv_close((uv_handle_t *) &proc->stderr_state.handle, dispose_read_state);
		}
	}

	if (proc->pipes == 0) {
		uv_close((uv_handle_t *) &proc->handle, dispose_process);
	} else {
		OBJ_RELEASE(&proc->std);
	}
}

static void async_process_exit(uv_process_t *handle, int64_t status, int signal)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	ZVAL_LONG(&proc->pid, 0);
	ZVAL_LONG(&proc->exit_code, status);

	if (proc->pipes == 0) {
		GC_ADDREF(&proc->std);

		close_process(proc);
	}
}

static void prepare_process(async_process_builder *builder, async_process *proc, zval *params, uint32_t count, zval *return_value, zend_execute_data *execute_data)
{
	char **args;
	uint32_t i;

	count += builder->argc;

	args = ecalloc(sizeof(char *), count + 2);
	args[0] = ZSTR_VAL(builder->command);

	for (i = 1; i <= builder->argc; i++) {
		args[i] = Z_STRVAL_P(&builder->argv[i - 1]);
	}

	for (; i <= count; i++) {
		args[i] = Z_STRVAL_P(&params[i - 1]);
	}

	args[count + 1] = NULL;

	proc->options.file = ZSTR_VAL(builder->command);
	proc->options.args = args;
	proc->options.stdio_count = 3;
	proc->options.stdio = builder->stdio;
	proc->options.exit_cb = async_process_exit;
	proc->options.flags = UV_PROCESS_WINDOWS_HIDE;

	if (builder->cwd != NULL) {
		proc->options.cwd = ZSTR_VAL(builder->cwd);
	}

	if (Z_TYPE_P(&builder->env) != IS_UNDEF) {
		proc->options.env = populate_env(Z_ARRVAL_P(&builder->env), builder->inherit_env);
	} else if (builder->inherit_env == 0) {
		proc->options.env = emalloc(sizeof(char *));
		proc->options.env[0] = NULL;
	}
}

static void pipe_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buffer)
{
	async_readable_pipe_state *state;

	state = (async_readable_pipe_state *) handle->data;

	state->buffer.current = state->buffer.base;
	state->buffer.len = 0;

	buffer->base = state->buffer.base;
	buffer->len = state->buffer.size;
}

static void pipe_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buffer)
{
	async_readable_pipe_state *state;

	zval data;

	state = (async_readable_pipe_state *) stream->data;

	if (nread == 0) {
		return;
	}

	uv_read_stop(stream);

	if (nread > 0) {
		state->buffer.len = (size_t) nread;

		ZVAL_NULL(&data);

		async_awaitable_trigger_next_continuation(&state->reads, &data, 1);

		return;
	}

	state->buffer.len = 0;

	if (nread == UV_EOF) {
		state->eof = 1;

		ZVAL_NULL(&data);

		async_awaitable_trigger_continuation(&state->reads, &data, 1);

		GC_ADDREF(&state->process->std);

		uv_close((uv_handle_t *) stream, dispose_read_state);

		return;
	}

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Pipe read error: %s", uv_strerror((int) nread));

	ZVAL_OBJ(&data, EG(exception));
	EG(exception) = NULL;

	async_awaitable_trigger_continuation(&state->reads, &data, 0);
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

	zend_throw_exception_ex(async_stream_exception_ce, 0, "Pipe write error: %s", uv_strerror(status));

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

	ZVAL_UNDEF(&builder->env);

	builder->inherit_env = 1;

	return &builder->std;
}

static void async_process_builder_object_destroy(zend_object *object)
{
	async_process_builder *builder;
	uint32_t i;

	builder = (async_process_builder *) object;

	zend_string_release(builder->command);

	if (builder->cwd != NULL) {
		zend_string_release(builder->cwd);
	}

	if (builder->argc > 0) {
		for (i = 0; i < builder->argc; i++) {
			zval_ptr_dtor(&builder->argv[i]);
		}

		efree(builder->argv);
	}

	zval_ptr_dtor(&builder->env);

	zend_object_std_dtor(&builder->std);
}

ZEND_METHOD(ProcessBuilder, __construct)
{
	async_process_builder *builder;
	uint32_t i;

	zval *params;

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_STR(builder->command)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, builder->argc)
	ZEND_PARSE_PARAMETERS_END();

	if (builder->argc > 0) {
		builder->argv = ecalloc(builder->argc, sizeof(zval));

		for (i = 0; i < builder->argc; i++) {
			ZVAL_COPY(&builder->argv[i], &params[i]);
		}
	}
}

ZEND_METHOD(ProcessBuilder, setDirectory)
{
	async_process_builder *builder;

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_STR(builder->cwd)
	ZEND_PARSE_PARAMETERS_END();
}

ZEND_METHOD(ProcessBuilder, inheritEnv)
{
	async_process_builder *builder;

	zend_long flag;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_LONG(flag)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	builder->inherit_env = flag ? 1 : 0;
}

ZEND_METHOD(ProcessBuilder, setEnv)
{
	async_process_builder *builder;

	zval *env;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ARRAY_EX(env, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&builder->env) != IS_UNDEF) {
		zval_ptr_dtor(&builder->env);
	}

	ZVAL_COPY(&builder->env, env);
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
	int x;

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

	if (proc->options.env != NULL) {
		x = 0;

		while (proc->options.env[x] != NULL) {
			efree(proc->options.env[x++]);
		}

		efree(proc->options.env);
	}

	if (code != 0) {
		zend_throw_error(NULL, "Failed to launch process \"%s\": %s", ZSTR_VAL(builder->command), uv_strerror(code));
	} else {
		async_task_suspend(&proc->observers, return_value, execute_data, NULL);
	}

	OBJ_RELEASE(&proc->std);
}

ZEND_METHOD(ProcessBuilder, start)
{
	async_process_builder *builder;
	async_process *proc;

	uint32_t count;
	zval *params;
	zval obj;

	int code;
	int x;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, -1)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());
	proc = async_process_object_create();

	prepare_process(builder, proc, params, count, return_value, execute_data);

	if (proc->options.stdio[0].flags & UV_CREATE_PIPE) {
		create_writable_state(proc, &proc->stdin_state, 0);
	}

	if (proc->options.stdio[1].flags & UV_CREATE_PIPE) {
		create_readable_state(proc, &proc->stdout_state, 1);
	}

	if (proc->options.stdio[2].flags & UV_CREATE_PIPE) {
		create_readable_state(proc, &proc->stderr_state, 2);
	}

	code = uv_spawn(async_task_scheduler_get_loop(), &proc->handle, &proc->options);

	efree(proc->options.args);

	if (proc->options.env != NULL) {
		x = 0;

		while (proc->options.env[x] != NULL) {
			efree(proc->options.env[x++]);
		}

		efree(proc->options.env);
	}

	if (code != 0) {
		zend_throw_error(NULL, "Failed to launch process \"%s\": %s", ZSTR_VAL(builder->command), uv_strerror(code));

		OBJ_RELEASE(&proc->std);
		return;
	}

	ZVAL_LONG(&proc->pid, proc->handle.pid);

	ZVAL_OBJ(&obj, &proc->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_process_builder_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, command, IS_STRING, 0)
	ZEND_ARG_VARIADIC_TYPE_INFO(0, arguments, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_set_directory, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, dir, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_inherit_env, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, inherit, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_builder_set_env, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, env, IS_ARRAY, 0)
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
	ZEND_ME(ProcessBuilder, setDirectory, arginfo_process_builder_set_directory, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, inheritEnv, arginfo_process_builder_inherit_env, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, setEnv, arginfo_process_builder_set_env, ZEND_ACC_PUBLIC)
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
	ZVAL_UNDEF(&proc->stdout_state.error);
	ZVAL_UNDEF(&proc->stderr_state.error);

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

		close_process(proc);
	}
}

static void async_process_object_destroy(zend_object *object)
{
	async_process *proc;

	proc = (async_process *) object;

	zval_ptr_dtor(&proc->pid);
	zval_ptr_dtor(&proc->exit_code);

	zval_ptr_dtor(&proc->stdin_state.error);
	zval_ptr_dtor(&proc->stdout_state.error);
	zval_ptr_dtor(&proc->stderr_state.error);

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

	pipe = async_writable_pipe_object_create(proc, &proc->stdin_state);

	ZVAL_OBJ(&obj, &pipe->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Process, getStdout)
{
	async_process *proc;
	async_readable_pipe *pipe;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(!(proc->options.stdio[1].flags & UV_CREATE_PIPE), "Cannot access STDOUT because it is not configured to be a pipe");

	pipe = async_readable_pipe_object_create(proc, &proc->stdout_state);

	ZVAL_OBJ(&obj, &pipe->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Process, getStderr)
{
	async_process *proc;
	async_readable_pipe *pipe;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(!(proc->options.stdio[2].flags & UV_CREATE_PIPE), "Cannot access STDERR because it is not configured to be a pipe");

	pipe = async_readable_pipe_object_create(proc, &proc->stderr_state);

	ZVAL_OBJ(&obj, &pipe->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Process, signal)
{
	async_process *proc;

	zend_long signum;
	int code;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_LONG(signum)
	ZEND_PARSE_PARAMETERS_END();

	proc = (async_process *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(Z_LVAL_P(&proc->exit_code) >= 0, "Cannot signal a process that has alredy been terminated");

	code = uv_process_kill(&proc->handle, (int) signum);

	if (code != 0) {
		zend_throw_error(NULL, "Failed to signal process: %s", uv_strerror(code));
		return;
	}
}

ZEND_METHOD(Process, awaitExit)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	if (Z_LVAL_P(&proc->exit_code) >= 0) {
		RETURN_ZVAL(&proc->exit_code, 1, 0);
	}
	async_task_suspend(&proc->observers, return_value, execute_data, NULL);
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

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_signal, 0, 1, IS_VOID, 0)
	ZEND_ARG_TYPE_INFO(0, signum, IS_LONG, 0)
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
	ZEND_ME(Process, signal, arginfo_process_signal, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, awaitExit, arginfo_process_await_exit, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_readable_pipe *async_readable_pipe_object_create(async_process *process, async_readable_pipe_state *state)
{
	async_readable_pipe *pipe;

	pipe = emalloc(sizeof(async_readable_pipe));
	ZEND_SECURE_ZERO(pipe, sizeof(async_readable_pipe));

	zend_object_std_init(&pipe->std, async_readable_pipe_ce);
	pipe->std.handlers = &async_readable_pipe_handlers;

	pipe->state = state;

	GC_ADDREF(&pipe->state->process->std);

	return pipe;
}

static void async_readable_pipe_object_destroy(zend_object *object)
{
	async_readable_pipe *pipe;

	pipe = (async_readable_pipe *) object;

	OBJ_RELEASE(&pipe->state->process->std);

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

	if (Z_TYPE_P(&pipe->state->error) != IS_UNDEF) {
		return;
	}

	zend_throw_exception(async_stream_closed_exception_ce, "Pipe has been closed", 0);

	ZVAL_OBJ(&pipe->state->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&pipe->state->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	if (pipe->state->eof == 0) {
		pipe->state->eof = 1;

		GC_ADDREF(&pipe->std);

		uv_close((uv_handle_t *) &pipe->state->handle, dispose_write_state);
	}

	async_awaitable_trigger_continuation(&pipe->state->reads, &pipe->state->error, 0);
}

ZEND_METHOD(ReadablePipe, read)
{
	async_readable_pipe *pipe;
	zend_bool cancelled;

	zval *hint;
	zval chunk;
	size_t len;

	hint = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(hint)
	ZEND_PARSE_PARAMETERS_END();

	pipe = (async_readable_pipe *) Z_OBJ_P(getThis());

	if (hint == NULL) {
		len = pipe->state->buffer.size;
	} else if (Z_LVAL_P(hint) < 1) {
		zend_throw_error(NULL, "Invalid read length: %d", (int) Z_LVAL_P(hint));
		return;
	} else {
		len = (size_t) Z_LVAL_P(hint);
	}

	if (Z_TYPE_P(&pipe->state->error) != IS_UNDEF) {
		Z_ADDREF_P(&pipe->state->error);

		execute_data->opline--;
		zend_throw_exception_internal(&pipe->state->error);
		execute_data->opline++;

		return;
	}

	if (pipe->state->reads.first != NULL) {
		zend_throw_exception(async_pending_read_exception_ce, "Cannot read from pipe while another read is pending", 0);
		return;
	}

	if (pipe->state->eof) {
		return;
	}

	if (pipe->state->buffer.len == 0) {
		uv_read_start((uv_stream_t *) &pipe->state->handle, pipe_read_alloc, pipe_read);

		async_task_suspend(&pipe->state->reads, return_value, execute_data, &cancelled);

		if (cancelled) {
			uv_read_stop((uv_stream_t *) &pipe->state->handle);
			return;
		}

		if (pipe->state->eof || UNEXPECTED(EG(exception))) {
			return;
		}
	}

	len = MIN(len, pipe->state->buffer.len);

	ZVAL_STRINGL(&chunk, pipe->state->buffer.current, len);

	pipe->state->buffer.current += len;
	pipe->state->buffer.len -= len;

	RETURN_ZVAL(&chunk, 1, 1);
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


static async_writable_pipe *async_writable_pipe_object_create(async_process *process, async_writable_pipe_state *state)
{
	async_writable_pipe *pipe;

	pipe = emalloc(sizeof(async_writable_pipe));
	ZEND_SECURE_ZERO(pipe, sizeof(async_writable_pipe));

	zend_object_std_init(&pipe->std, async_writable_pipe_ce);
	pipe->std.handlers = &async_writable_pipe_handlers;

	pipe->state = state;

	GC_ADDREF(&process->std);

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

	zend_throw_exception(async_stream_closed_exception_ce, "Pipe has been closed", 0);

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
				zend_throw_exception_ex(async_stream_exception_ce, 0, "Pipe write error: %s", uv_strerror(result));

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

	async_task_suspend(&pipe->state->writes, return_value, execute_data, NULL);
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
