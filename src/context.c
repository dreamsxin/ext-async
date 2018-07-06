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

#include "php.h"
#include "zend.h"
#include "zend_API.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"

#include "php_task.h"

ZEND_DECLARE_MODULE_GLOBALS(task)

zend_class_entry *concurrent_context_ce;

static zend_object_handlers concurrent_context_handlers;


void concurrent_context_delegate_error(concurrent_context *context)
{
	zval error;
	zval args[1];
	zval retval;

	while (context != NULL && EG(exception) != NULL) {
		if (context->error_handler != NULL) {
			ZVAL_OBJ(&error, EG(exception));
			EG(exception) = NULL;

			ZVAL_COPY(&args[0], &error);

			context->error_handler->fci.param_count = 1;
			context->error_handler->fci.params = args;
			context->error_handler->fci.retval = &retval;

			zend_call_function(&context->error_handler->fci, &context->error_handler->fcc);

			zval_ptr_dtor(args);
			zval_ptr_dtor(&retval);
			zval_ptr_dtor(&error);
		}

		context = context->parent;
	}

	if (UNEXPECTED(EG(exception))) {
		zend_error_noreturn(E_ERROR, "Uncaught awaitable continuation error");
	}
}


concurrent_context *concurrent_context_object_create(HashTable *params)
{
	concurrent_context *context;
	HashPosition pos;
	zend_string *name;
	zend_ulong index;

	name = NULL;

	context = emalloc(sizeof(concurrent_context));
	ZEND_SECURE_ZERO(context, sizeof(concurrent_context));

	GC_ADDREF(&context->std);

	zend_object_std_init(&context->std, concurrent_context_ce);
	context->std.handlers = &concurrent_context_handlers;

	if (params != NULL) {
		context->param_count = zend_hash_num_elements(params);

		if (context->param_count == 1) {
			zend_hash_internal_pointer_reset_ex(params, &pos);
			zend_hash_get_current_key_ex(params, &name, &index, &pos);

			context->data.var.name = zend_string_copy(name);
			ZVAL_COPY(&context->data.var.value, zend_hash_get_current_data_ex(params, &pos));
		} else if (context->param_count > 1) {
			context->data.params = zend_array_dup(params);
		}
	}

	return context;
}

static concurrent_context *concurrent_context_object_create_single_var(zend_string *name, zval *value)
{
	concurrent_context *context;

	context = emalloc(sizeof(concurrent_context));
	ZEND_SECURE_ZERO(context, sizeof(concurrent_context));

	GC_ADDREF(&context->std);

	zend_object_std_init(&context->std, concurrent_context_ce);
	context->std.handlers = &concurrent_context_handlers;

	context->param_count = 1;

	context->data.var.name = zend_string_copy(name);
	ZVAL_COPY(&context->data.var.value, value);

	return context;
}


static void concurrent_context_object_destroy(zend_object *object)
{
	concurrent_context *context;

	context = (concurrent_context *) object;

	if (context->param_count == 1) {
		zend_string_release(context->data.var.name);
		zval_ptr_dtor(&context->data.var.value);
	} else if (context->param_count > 1 && context->data.params != NULL) {
		zend_hash_destroy(context->data.params);
		FREE_HASHTABLE(context->data.params);
	}

	if (context->error_handler != NULL) {
		zval_ptr_dtor(&context->error_handler->fci.function_name);
		efree(context->error_handler);

		context->error_handler = NULL;
	}

	if (context->parent != NULL) {
		OBJ_RELEASE(&context->parent->std);
	}

	zend_object_std_dtor(&context->std);
}

ZEND_METHOD(Context, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Context must not be constructed from userland code");
}

ZEND_METHOD(Context, get)
{
	concurrent_context *context;
	zend_string *key;

	zval *val;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	key = Z_STR_P(val);
	ZSTR_HASH(key);

	context = (concurrent_context *) Z_OBJ_P(getThis());

	do {
		if (context->param_count == 1) {
			if (zend_string_equals(key, context->data.var.name)) {
				RETURN_ZVAL(&context->data.var.value, 1, 0);
			}
		} else if (context->param_count > 1) {
			if (zend_hash_exists_ind(context->data.params, key)) {
				RETURN_ZVAL(zend_hash_find_ex_ind(context->data.params, key, 1), 1, 0);
			}
		}

		context = context->parent;
	} while (context != NULL);
}

ZEND_METHOD(Context, with)
{
	concurrent_context *context;
	concurrent_context *current;
	zend_string *str;

	zval *key;
	zval *value;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_ZVAL(key)
		Z_PARAM_ZVAL(value)
	ZEND_PARSE_PARAMETERS_END();

	current = (concurrent_context *) Z_OBJ_P(getThis());
	str = Z_STR_P(key);

	if (current->param_count == 0) {
		context = concurrent_context_object_create_single_var(str, value);
	} else if (current->param_count == 1 && zend_string_equals(str, current->data.var.name)) {
		if (zend_string_equals(str, current->data.var.name)) {
			context = concurrent_context_object_create_single_var(str, value);
		} else {
			context = concurrent_context_object_create(NULL);
			context->param_count = 2;

			ALLOC_HASHTABLE(context->data.params);
			zend_hash_init(context->data.params, 0, NULL, ZVAL_PTR_DTOR, 0);

			zend_hash_add(context->data.params, current->data.var.name, &current->data.var.value);
			zend_hash_add(context->data.params, str, value);
		}
	} else {
		context = concurrent_context_object_create(current->data.params);

		if (zend_hash_exists_ind(context->data.params, str)) {
			zend_hash_update_ind(context->data.params, str, value);
		} else {
			zend_hash_add(context->data.params, str, value);
			context->param_count++;
		}
	}

	context->parent = current->parent;
	context->scheduler = current->scheduler;

	if (context->parent != NULL) {
		GC_ADDREF(&context->parent->std);
	}

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, without)
{
	concurrent_context *context;
	concurrent_context *current;
	HashPosition pos;
	zend_string *str;
	zend_ulong index;

	zval *key;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(key)
	ZEND_PARSE_PARAMETERS_END();

	current = (concurrent_context *) Z_OBJ_P(getThis());

	str = Z_STR_P(key);

	if (current->param_count == 1) {
		if (zend_string_equals(str, current->data.var.name)) {
			context = concurrent_context_object_create(NULL);
		} else {
			context = concurrent_context_object_create_single_var(current->data.var.name, &current->data.var.value);
		}
	} else {
		context = concurrent_context_object_create(current->data.params);

		if (context->param_count > 1 && zend_hash_exists_ind(context->data.params, str)) {
			context->param_count--;

			if (context->param_count == 1) {
				zend_hash_internal_pointer_reset_ex(context->data.params, &pos);
				zend_hash_get_current_key_ex(context->data.params, &str, &index, &pos);

				context->data.var.name = zend_string_copy(str);
				ZVAL_COPY(&context->data.var.value, zend_hash_get_current_data_ex(context->data.params, &pos));
			} else {
				zend_hash_del_ind(context->data.params, str);
			}
		}
	}

	context->parent = current->parent;
	context->scheduler = current->scheduler;

	if (context->parent != NULL) {
		GC_ADDREF(&context->parent->std);
	}

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, withErrorHandler)
{
	concurrent_context *context;
	concurrent_context *current;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	zval obj;

	current = (concurrent_context *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	if (current->param_count == 1) {
		context = concurrent_context_object_create_single_var(current->data.var.name, &current->data.var.value);
	} else {
		context = concurrent_context_object_create(current->data.params);
	}

	context->parent = current->parent;
	context->scheduler = current->scheduler;

	context->error_handler = emalloc(sizeof(concurrent_context_error_handler));
	context->error_handler->fci = fci;
	context->error_handler->fcc = fcc;

	Z_TRY_ADDREF_P(&fci.function_name);

	if (context->parent != NULL) {
		GC_ADDREF(&context->parent->std);
	}

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, run)
{
	concurrent_context *context;
	concurrent_context *prev;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t param_count;

	zval *params;
	zval result;

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, param_count)
	ZEND_PARSE_PARAMETERS_END();

	context = (concurrent_context *) Z_OBJ_P(getThis());

	fci.params = params;
	fci.param_count = param_count;
	fci.retval = &result;
	fci.no_separation = 1;

	prev = TASK_G(current_context);
	TASK_G(current_context) = context;

	zend_call_function(&fci, &fcc);

	TASK_G(current_context) = prev;

	RETURN_ZVAL(&result, 1, 1);
}

ZEND_METHOD(Context, var)
{
	concurrent_context *context;
	zend_string *key;

	zval *val;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	key = Z_STR_P(val);
	ZSTR_HASH(key);

	context = TASK_G(current_context);

	while (context != NULL) {
		if (context->param_count == 1) {
			if (zend_string_equals(key, context->data.var.name)) {
				RETURN_ZVAL(&context->data.var.value, 1, 0);
			}
		} else if (context->param_count > 1) {
			if (zend_hash_exists_ind(context->data.params, key)) {
				RETURN_ZVAL(zend_hash_find_ex_ind(context->data.params, key, 1), 1, 0);
			}
		}

		context = context->parent;
	}
}

ZEND_METHOD(Context, current)
{
	concurrent_context *context;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	context = TASK_G(current_context);

	if (UNEXPECTED(context == NULL)) {
		zend_throw_error(NULL, "Cannot access current context when no context is running");
		return;
	}

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, inherit)
{
	concurrent_context *context;
	concurrent_context *current;

	zval *params;
	HashTable *table;
	zval obj;

	params = NULL;
	table = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(params)
	ZEND_PARSE_PARAMETERS_END();

	current = TASK_G(current_context);

	if (UNEXPECTED(current == NULL)) {
		zend_throw_error(NULL, "Cannot inherit context when no context is running");
		return;
	}

	if (params != NULL && Z_TYPE_P(params) == IS_ARRAY) {
		table = Z_ARRVAL_P(params);
	}

	context = concurrent_context_object_create(table);
	context->parent = current;
	context->scheduler = current->scheduler;

	GC_ADDREF(&context->parent->std);

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, background)
{
	concurrent_context *context;
	concurrent_context *current;

	zval *params;
	HashTable *table;
	zval obj;

	params = NULL;
	table = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(params)
	ZEND_PARSE_PARAMETERS_END();

	current = TASK_G(current_context);

	if (UNEXPECTED(current == NULL)) {
		zend_throw_error(NULL, "Cannot inherit background context when no context is running");
		return;
	}

	while (current->parent != NULL) {
		current = current->parent;
	}

	if (params != NULL && Z_TYPE_P(params) == IS_ARRAY) {
		table = Z_ARRVAL_P(params);
	}

	context = concurrent_context_object_create(table);
	context->parent = current;
	context->scheduler = current->scheduler;

	GC_ADDREF(&current->std);

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO(arginfo_context_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_context_get, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, var, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_with, 0, 2, Concurrent\\Context, 0)
	ZEND_ARG_TYPE_INFO(0, var, IS_STRING, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_without, 0, 1, Concurrent\\Context, 0)
	ZEND_ARG_TYPE_INFO(0, var, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_err, 0, 1, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_context_run, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_context_var, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, var, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_current, 0, 0, Concurrent\\Context, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_inherit, 0, 0, Concurrent\\Context, 0)
	ZEND_ARG_ARRAY_INFO(0, params, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_background, 0, 0, Concurrent\\Context, 0)
	ZEND_ARG_ARRAY_INFO(0, params, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry task_context_functions[] = {
	ZEND_ME(Context, __construct, arginfo_context_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(Context, get, arginfo_context_get, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, with, arginfo_context_with, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, without, arginfo_context_without, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, withErrorHandler, arginfo_context_err, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, run, arginfo_context_run, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, var, arginfo_context_var, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Context, current, arginfo_context_current, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Context, inherit, arginfo_context_inherit, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Context, background, arginfo_context_background, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


void concurrent_context_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Context", task_context_functions);
	concurrent_context_ce = zend_register_internal_class(&ce);
	concurrent_context_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_context_ce->serialize = zend_class_serialize_deny;
	concurrent_context_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_context_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_context_handlers.free_obj = concurrent_context_object_destroy;
	concurrent_context_handlers.clone_obj = NULL;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
