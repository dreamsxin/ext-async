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
#include "async_stream.h"
#include "zend_inheritance.h"

zend_class_entry *async_process_builder_ce;
zend_class_entry *async_process_ce;
zend_class_entry *async_readable_pipe_ce;
zend_class_entry *async_writable_pipe_ce;

static zend_object_handlers async_process_builder_handlers;
static zend_object_handlers async_process_handlers;
static zend_object_handlers async_readable_pipe_handlers;
static zend_object_handlers async_writable_pipe_handlers;

#define ASYNC_PROCESS_BUILDER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_process_builder_ce, const_name, sizeof(const_name)-1, (zend_long)value);
	
typedef struct _async_process async_process;
	
typedef struct {
	/* Fiber PHP object handle. */
	zend_object std;

	/* Command to be executed (without arguments). */
	zend_string *command;

	/* Number of additional arguments. */
	uint32_t argc;

	/* Additional args passed to the base command. */
	zval *argv;

	/* Current working directory for the process. */
	zend_string *cwd;

	/* Environment vars to be passed to the created process. */
	zval env;

	/* Set to inherit env vars from parent. */
	zend_bool inherit_env;

	/* STDIO pipe definitions for STDIN, STDOUT and STDERR. */
	uv_stdio_container_t stdio[3];
} async_process_builder;

typedef struct {
	async_process *process;

	uv_pipe_t handle;

	async_stream *stream;

	zval error;
} async_writable_pipe_state;

typedef struct {
	async_process *process;

	uv_pipe_t handle;

	async_stream *stream;

	zval error;
} async_readable_pipe_state;

struct _async_process {
	/* Fiber PHP object handle. */
	zend_object std;

	/* Task scheduler providing the event loop. */
	async_task_scheduler *scheduler;

	/* Process handle providing access to the running process instance. */
	uv_process_t handle;

	/* Process configuration, provided by process builder. */
	uv_process_options_t options;

	/* Process ID, will be 0 if the process has finished execution. */
	zval pid;

	/* Exit code returned by the process, will be -1 if the process has not terminated yet. */
	zval exit_code;

	async_writable_pipe_state stdin_state;
	async_readable_pipe_state stdout_state;
	async_readable_pipe_state stderr_state;

	zend_uchar pipes;

	/* Inlined cancel callback being used to dispose of the process. */
	async_cancel_cb cancel;

	/* Exit code / process termination observers. */
	async_op_queue observers;
};

typedef struct {
	/* Fiber PHP object handle. */
	zend_object std;

	async_readable_pipe_state *state;
} async_readable_pipe;

typedef struct {
	/* Fiber PHP object handle. */
	zend_object std;

	async_writable_pipe_state *state;
} async_writable_pipe;

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
		char **tmp;
		
		for (tmp = environ; tmp != NULL && *tmp != NULL; tmp++) {
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
	async_op *op;

	proc = (async_process *) handle->data;

	while (proc->observers.first != NULL) {
		ASYNC_DEQUEUE_OP(&proc->observers, op);
		ASYNC_FINISH_OP(op);
	}

	ASYNC_DELREF(&proc->std);
}

static void shutdown_process(void *obj, zval *error)
{
	async_process *proc;

	proc = (async_process *) obj;

	proc->cancel.func = NULL;

	uv_process_kill(&proc->handle, ASYNC_SIGNAL_SIGKILL);

	ASYNC_ADDREF(&proc->std);

	uv_close((uv_handle_t *) &proc->handle, dispose_process);
}

static void create_readable_state(async_process *process, async_readable_pipe_state *state, int i)
{
	uv_pipe_init(&process->scheduler->loop, &state->handle, 0);

	state->handle.data = state;
	state->process = process;

	process->options.stdio[i].data.stream = (uv_stream_t *) &state->handle;
	process->pipes++;

	state->stream = async_stream_init((uv_stream_t *) &state->handle, 0);
}

static void dispose_read_state(uv_handle_t *handle)
{
	async_readable_pipe_state *state;

	state = (async_readable_pipe_state *) handle->data;
	state->process->pipes--;

	async_stream_free(state->stream);
	efree(state->stream);

	ASYNC_DELREF(&state->process->std);
}

static void create_writable_state(async_process *process, async_writable_pipe_state *state, int i)
{
	uv_pipe_init(&process->scheduler->loop, &state->handle, 0);

	state->handle.data = state;
	state->process = process;

	process->options.stdio[i].data.stream = (uv_stream_t *) &state->handle;
	process->pipes++;

	state->stream = async_stream_init((uv_stream_t *) &state->handle, 0);
}

static void dispose_write_state(uv_handle_t *handle)
{
	async_writable_pipe_state *state;

	state = (async_writable_pipe_state *) handle->data;
	state->process->pipes--;

	async_stream_free(state->stream);
	efree(state->stream);

	ASYNC_DELREF(&state->process->std);
}

static void exit_process(uv_process_t *handle, int64_t status, int signal)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	ZVAL_LONG(&proc->pid, 0);
	ZVAL_LONG(&proc->exit_code, status);

	if (proc->cancel.func != NULL) {
		ASYNC_Q_DETACH(&proc->scheduler->shutdown, &proc->cancel);

		proc->cancel.func(proc, NULL);
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
	proc->options.exit_cb = exit_process;
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
	async_context *context;
	async_op *op;

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

	code = uv_spawn(&proc->scheduler->loop, &proc->handle, &proc->options);

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
		context = async_context_get();

		if (context->background) {
			uv_unref((uv_handle_t *) &proc->handle);
		}

		ASYNC_ALLOC_OP(op);
		ASYNC_ENQUEUE_OP(&proc->observers, op);

		if (async_await_op(op) == FAILURE) {
			ASYNC_FORWARD_OP_ERROR(op);
		}

		if (op->flags & ASYNC_OP_FLAG_CANCELLED) {
			uv_process_kill(&proc->handle, ASYNC_SIGNAL_SIGKILL);
		}

		ASYNC_FREE_OP(op);

		if (EXPECTED(EG(exception) == NULL) && USED_RET()) {
			ZVAL_COPY(return_value, &proc->exit_code);
		}
	}

	ASYNC_DELREF(&proc->std);
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

	code = uv_spawn(&proc->scheduler->loop, &proc->handle, &proc->options);

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

		ASYNC_DELREF(&proc->std);
		return;
	}

	uv_unref((uv_handle_t *) &proc->handle);

	ASYNC_Q_ENQUEUE(&proc->scheduler->shutdown, &proc->cancel);

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
	ZEND_ME(ProcessBuilder, __construct, arginfo_process_builder_ctor, ZEND_ACC_PUBLIC)
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

	proc->scheduler = async_task_scheduler_get();

	ASYNC_ADDREF(&proc->scheduler->std);

	proc->cancel.object = proc;
	proc->cancel.func = shutdown_process;

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

	if (proc->cancel.func != NULL) {
		ASYNC_Q_DETACH(&proc->scheduler->shutdown, &proc->cancel);

		proc->cancel.func(proc, NULL);
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

	ASYNC_DELREF(&proc->scheduler->std);

	zend_object_std_dtor(&proc->std);
}

ZEND_METHOD(Process, __debugInfo)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	if (USED_RET()) {
		proc = (async_process *) Z_OBJ_P(getThis());
		
		array_init(return_value);
		
		add_assoc_zval(return_value, "pid", &proc->pid);
		add_assoc_zval(return_value, "exit_code", &proc->exit_code);
		add_assoc_bool(return_value, "running", Z_LVAL_P(&proc->exit_code) < 0);
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

	ASYNC_CHECK_ERROR(code != 0, "Failed to signal process: %s", uv_strerror(code));
}

ZEND_METHOD(Process, awaitExit)
{
	async_process *proc;
	async_context *context;
	async_op *op;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	if (Z_LVAL_P(&proc->exit_code) >= 0) {
		RETURN_ZVAL(&proc->exit_code, 1, 0);
	}
	
	context = async_context_get();

	if (!context->background) {
		uv_ref((uv_handle_t *) &proc->handle);
	}

	ASYNC_ALLOC_OP(op);
	ASYNC_ENQUEUE_OP(&proc->observers, op);

	if (async_await_op(op) == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
	}

	if (!context->background) {
		uv_unref((uv_handle_t *) &proc->handle);
	}

	ASYNC_FREE_OP(op);

	if (EXPECTED(EG(exception) == NULL)) {
		RETURN_ZVAL(&proc->exit_code, 1, 0);
	}
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

	ASYNC_ADDREF(&pipe->state->process->std);

	return pipe;
}

static void async_readable_pipe_object_destroy(zend_object *object)
{
	async_readable_pipe *pipe;

	pipe = (async_readable_pipe *) object;

	ASYNC_DELREF(&pipe->state->process->std);

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
	
	ASYNC_PREPARE_EXCEPTION(&pipe->state->error, async_stream_closed_exception_ce, "Pipe has been closed");
	
	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&pipe->state->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	if (!(pipe->state->stream->flags & ASYNC_STREAM_CLOSED)) {
		ASYNC_ADDREF(&pipe->state->process->std);

		async_stream_close(pipe->state->stream, dispose_read_state, pipe->state);
	}
}

ZEND_METHOD(ReadablePipe, read)
{
	async_readable_pipe *pipe;

	zend_string *str;
	zval *hint;
	size_t len;
	int code;

	hint = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(hint)
	ZEND_PARSE_PARAMETERS_END();

	pipe = (async_readable_pipe *) Z_OBJ_P(getThis());

	if (hint == NULL || Z_TYPE_P(hint) == IS_NULL) {
		len = pipe->state->stream->buffer.size;
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

	code = async_stream_read_string(pipe->state->stream, &str, len);

	if (code > 0) {
		RETURN_STR(str);
	}

	if (code == 0) {
		return;
	}

	ASYNC_CHECK_EXCEPTION(EG(exception) == NULL, async_stream_exception_ce, "Reading from pipe failed: %s", uv_strerror(code));
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

	ASYNC_ADDREF(&process->std);

	return pipe;
}

static void async_writable_pipe_object_destroy(zend_object *object)
{
	async_writable_pipe *pipe;

	pipe = (async_writable_pipe *) object;

	ASYNC_DELREF(&pipe->state->process->std);

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

	ASYNC_PREPARE_EXCEPTION(&pipe->state->error, async_stream_closed_exception_ce, "Pipe has been closed");

	if (!(pipe->state->stream->flags & ASYNC_STREAM_CLOSED)) {
		ASYNC_ADDREF(&pipe->state->process->std);

		async_stream_close(pipe->state->stream, dispose_write_state, pipe->state);
	}
}

ZEND_METHOD(WritablePipe, write)
{
	async_writable_pipe *pipe;

	zend_string *data;

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

	async_stream_write(pipe->state->stream, ZSTR_VAL(data), ZSTR_LEN(data));
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
