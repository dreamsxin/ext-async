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

static zend_object_handlers async_process_builder_handlers;
static zend_object_handlers async_process_handlers;

const zend_uchar ASYNC_PROCESS_STDIN = 0;
const zend_uchar ASYNC_PROCESS_STDOUT = 1;
const zend_uchar ASYNC_PROCESS_STDERR = 2;

const zend_uchar ASYNC_PROCESS_STDIO_IGNORE = 0;
const zend_uchar ASYNC_PROCESS_STDIO_INHERIT = 1;
const zend_uchar ASYNC_PROCESS_STDIO_PIPE = 2;

#define ASYNC_PROCESS_BUILDER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_process_builder_ce, const_name, sizeof(const_name)-1, (zend_long)value);

static async_process *async_process_object_create();


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

static void async_process_exit(uv_process_t *handle, int64_t status, int signal)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	ZVAL_LONG(&proc->pid, 0);
	ZVAL_LONG(&proc->exit_code, status);

	async_awaitable_trigger_continuation(&proc->observers, &proc->exit_code, 1);

	OBJ_RELEASE(&proc->std);
}

static void async_process_closed(uv_handle_t *handle)
{
	async_process *proc;

	proc = (async_process *) handle->data;

	OBJ_RELEASE(&proc->std);
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

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_LONG(mode)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(fd)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());

	configure_stdio(builder, 2, mode, fd, execute_data);
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
		async_task_suspend(&proc->observers, return_value, execute_data, 0);
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

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, -1)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());
	proc = async_process_object_create();

	prepare_process(builder, proc, params, count, return_value, execute_data);

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

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_process_builder_start, 0, 0, Concurrent\\Process, 0)
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

	return proc;
}

static void async_process_object_dtor(zend_object *object)
{
	async_process *proc;

	proc = (async_process *) object;

	if (Z_LVAL_P(&proc->exit_code) < 0) {
		uv_process_kill(&proc->handle, 2);
	}

	GC_ADDREF(&proc->std);

	uv_close((uv_handle_t *) &proc->handle, async_process_closed);
}

static void async_process_object_destroy(zend_object *object)
{
	async_process *proc;

	proc = (async_process *) object;

	zval_ptr_dtor(&proc->pid);
	zval_ptr_dtor(&proc->exit_code);

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

ZEND_METHOD(Process, pid)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	RETURN_ZVAL(&proc->pid, 1, 0);
}

ZEND_METHOD(Process, awaitExit)
{
	async_process *proc;

	ZEND_PARSE_PARAMETERS_NONE();

	proc = (async_process *) Z_OBJ_P(getThis());

	if (Z_LVAL_P(&proc->exit_code) >= 0) {
		RETURN_ZVAL(&proc->exit_code, 1, 0);
	}

	async_task_suspend(&proc->observers, return_value, execute_data, 0);
}

ZEND_BEGIN_ARG_INFO(arginfo_process_debug_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_is_running, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_pid, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_process_await_exit, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_process_functions[] = {
	ZEND_ME(Process, __debugInfo, arginfo_process_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, isRunning, arginfo_process_is_running, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, pid, arginfo_process_pid, ZEND_ACC_PUBLIC)
	ZEND_ME(Process, awaitExit, arginfo_process_await_exit, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_process_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\ProcessBuilder", async_process_builder_functions);
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

	INIT_CLASS_ENTRY(ce, "Concurrent\\Process", async_process_functions);
	async_process_ce = zend_register_internal_class(&ce);
	async_process_ce->ce_flags |= ZEND_ACC_FINAL;
	async_process_ce->serialize = zend_class_serialize_deny;
	async_process_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_process_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_process_handlers.dtor_obj = async_process_object_dtor;
	async_process_handlers.free_obj = async_process_object_destroy;
	async_process_handlers.clone_obj = NULL;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
