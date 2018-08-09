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

#include "zend_inheritance.h"

zend_class_entry *async_process_builder_ce;

static zend_object_handlers async_process_builder_handlers;

const zend_uchar ASYNC_PROCESS_STDIO_IGNORE = 0;
const zend_uchar ASYNC_PROCESS_STDIO_INHERIT = 1;
const zend_uchar ASYNC_PROCESS_STDIO_PIPE = 2;

#define ASYNC_PROCESS_BUILDER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_process_builder_ce, const_name, sizeof(const_name)-1, (zend_long)value);


typedef struct _proc_info {
	uv_process_t proc;

	uv_process_options_t options;

	async_awaitable_queue continuation;
} proc_info;


static void configure_stdio(async_process_builder *builder, int i, zend_long mode, zend_long fd, zend_execute_data *execute_data)
{
	if (mode == ASYNC_PROCESS_STDIO_IGNORE) {
		builder->stdio[i].flags = UV_IGNORE;
	} else if (mode == ASYNC_PROCESS_STDIO_INHERIT) {
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

static void async_process_exit(uv_process_t *process, int64_t status, int signal)
{
	proc_info *info;

	zval result;

	info = (proc_info *) process->data;

	ZVAL_LONG(&result, status);

	async_awaitable_trigger_continuation(&info->continuation, &result, 1);
}

static void async_process_closed(uv_handle_t *handle)
{
	proc_info *info;

	info = (proc_info *) handle->data;

	efree(info);
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

ZEND_METHOD(ProcessBuilder, execute)
{
	async_process_builder *builder;
	uv_loop_t *loop;

	uint32_t i;
	uint32_t count;

	zval *params;

	proc_info *info;
	int code;
	char **args;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, -1)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	builder = (async_process_builder *) Z_OBJ_P(getThis());
	loop = async_task_scheduler_get_loop();

	for (i = 0; i < 3; i++) {
		if (builder->stdio[i].flags & UV_CREATE_PIPE) {
			zend_throw_error(NULL, "Cannot use STDIO pipe in execute(), use start() instead");
			return;
		}
	}

	info = emalloc(sizeof(proc_info));
	ZEND_SECURE_ZERO(info, sizeof(proc_info));

	info->proc.data = info;

	args = ecalloc(sizeof(char *), count + 2);
	args[0] = builder->command;

	for (i = 0; i < count; i++) {
		args[i + 1] = Z_STRVAL_P(&params[i]);
	}

	args[count + 1] = NULL;

	info->options.file = builder->command;
	info->options.args = args;
	info->options.stdio_count = 3;
	info->options.stdio = builder->stdio;
	info->options.exit_cb = async_process_exit;
	info->options.flags = UV_PROCESS_WINDOWS_HIDE;

	code = uv_spawn(loop, &info->proc, &info->options);

	if (code != 0) {
		zend_throw_error(NULL, "Failed to launch process \"%s\": %s", builder->command, uv_strerror(code));

		goto DISPOSE;
	}

	async_task_suspend(&info->continuation, return_value, execute_data, 0);

DISPOSE:
	uv_close((uv_handle_t *) &info->proc, async_process_closed);

	efree(args);
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

static const zend_function_entry async_process_builder_functions[] = {
	ZEND_ME(ProcessBuilder, __construct, arginfo_process_builder_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(ProcessBuilder, configureStdin, arginfo_process_builder_configure_stdin, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, configureStdout, arginfo_process_builder_configure_stdout, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, configureStderr, arginfo_process_builder_configure_stderr, ZEND_ACC_PUBLIC)
	ZEND_ME(ProcessBuilder, execute, arginfo_process_builder_execute, ZEND_ACC_PUBLIC)
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

	ASYNC_PROCESS_BUILDER_CONST("STDIO_IGNORE", ASYNC_PROCESS_STDIO_IGNORE);
	ASYNC_PROCESS_BUILDER_CONST("STDIO_INHERIT", ASYNC_PROCESS_STDIO_INHERIT);
	ASYNC_PROCESS_BUILDER_CONST("STDIO_PIPE", ASYNC_PROCESS_STDIO_PIPE);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
