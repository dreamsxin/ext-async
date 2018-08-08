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

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_context_ce;
zend_class_entry *async_context_var_ce;
zend_class_entry *async_cancellation_handler_ce;

static zend_object_handlers async_context_handlers;
static zend_object_handlers async_context_var_handlers;
static zend_object_handlers async_cancellation_handler_handlers;


static async_context *async_context_object_create(async_context_var *var, zval *value);

async_context *async_context_get()
{
	async_context *context;

	context = ASYNC_G(current_context);

	if (context != NULL) {
		return context;
	}

	context = ASYNC_G(context);

	if (context != NULL) {
		return context;
	}

	context = async_context_object_create(NULL, NULL);

	ASYNC_G(context) = context;

	return context;
}

static async_context *async_context_object_create(async_context_var *var, zval *value)
{
	async_context *context;

	context = emalloc(sizeof(async_context));
	ZEND_SECURE_ZERO(context, sizeof(async_context));

	zend_object_std_init(&context->std, async_context_ce);
	context->std.handlers = &async_context_handlers;

	if (var != NULL) {
		context->var = var;
		GC_ADDREF(&var->std);
	}

	if (value == NULL) {
		ZVAL_NULL(&context->value);
	} else {
		ZVAL_COPY(&context->value, value);
	}

	return context;
}

static void async_context_object_destroy(zend_object *object)
{
	async_context *context;

	context = (async_context *) object;

	if (context->var != NULL) {
		OBJ_RELEASE(&context->var->std);
	}

	zval_ptr_dtor(&context->value);

	if (context->parent != NULL) {
		OBJ_RELEASE(&context->parent->std);
	}

	if (context->cancel != NULL) {
		OBJ_RELEASE(&context->cancel->std);
	}

	zend_object_std_dtor(&context->std);
}

ZEND_METHOD(Context, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Context must not be constructed from userland code");
}

ZEND_METHOD(Context, with)
{
	async_context *context;
	async_context *current;
	async_context_var *var;

	zval *key;
	zval *value;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_ZVAL(key)
		Z_PARAM_ZVAL(value)
	ZEND_PARSE_PARAMETERS_END();

	current = (async_context *) Z_OBJ_P(getThis());
	var = (async_context_var *) Z_OBJ_P(key);

	context = async_context_object_create(var, value);
	context->parent = current;

	if (current != NULL) {
		context->background = current->background;
		context->cancel = current->cancel;

		GC_ADDREF(&current->std);
	}

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, run)
{
	async_context *context;
	async_context *prev;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t count;

	zval *params;
	zval result;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	context = (async_context *) Z_OBJ_P(getThis());

	if (count == 0) {
		fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&fci, count, params);
	}

	fci.retval = &result;
	fci.no_separation = 1;

	prev = ASYNC_G(current_context);
	ASYNC_G(current_context) = context;

	zend_call_function(&fci, &fcc);

	ASYNC_G(current_context) = prev;

	if (count > 0) {
		zend_fcall_info_args_clear(&fci, 1);
	}

	RETURN_ZVAL(&result, 1, 1);
}

ZEND_METHOD(Context, isCancelled)
{
	async_context *context;

	ZEND_PARSE_PARAMETERS_NONE();

	context = (async_context *) Z_OBJ_P(getThis());

	if (context->cancel == NULL || Z_TYPE_P(&context->cancel->error) == IS_UNDEF) {
		RETURN_FALSE;
	}

	RETURN_TRUE;
}

ZEND_METHOD(Context, throwIfCancelled)
{
	async_context *context;

	ZEND_PARSE_PARAMETERS_NONE();

	context = (async_context *) Z_OBJ_P(getThis());

	if (context->cancel && Z_TYPE_P(&context->cancel->error) != IS_UNDEF) {
		Z_ADDREF_P(&context->cancel->error);

		execute_data->opline--;
		zend_throw_exception_internal(&context->cancel->error);
		execute_data->opline++;
	}
}

ZEND_METHOD(Context, current)
{
	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	ZVAL_OBJ(&obj, &async_context_get()->std);

	RETURN_ZVAL(&obj, 1, 0);
}

ZEND_METHOD(Context, background)
{
	async_context *context;
	async_context *current;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	current = async_context_get();

	context = async_context_object_create(NULL, NULL);
	context->parent = current;
	context->cancel = current->cancel;
	context->background = 1;

	GC_ADDREF(&current->std);

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO(arginfo_context_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_with, 0, 2, Concurrent\\Context, 0)
	ZEND_ARG_OBJ_INFO(0, var, Concurrent\\ContextVar, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_context_run, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_context_is_cancelled, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_context_throw_if_cancelled, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_current, 0, 0, Concurrent\\Context, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_background, 0, 0, Concurrent\\Context, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_context_functions[] = {
	ZEND_ME(Context, __construct, arginfo_context_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(Context, with, arginfo_context_with, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, run, arginfo_context_run, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, isCancelled, arginfo_context_is_cancelled, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, throwIfCancelled, arginfo_context_throw_if_cancelled, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, current, arginfo_context_current, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Context, background, arginfo_context_background, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


static zend_object *async_context_var_object_create(zend_class_entry *ce)
{
	async_context_var *var;

	var = emalloc(sizeof(async_context_var));
	ZEND_SECURE_ZERO(var, sizeof(async_context_var));

	zend_object_std_init(&var->std, ce);
	var->std.handlers = &async_context_var_handlers;

	return &var->std;
}

static void async_context_var_object_destroy(zend_object *object)
{
	async_context_var *var;

	var = (async_context_var *) object;

	zend_object_std_dtor(&var->std);
}

ZEND_METHOD(ContextVar, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();
}

ZEND_METHOD(ContextVar, get)
{
	async_context_var *var;
	async_context *context;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	var = (async_context_var *) Z_OBJ_P(getThis());

	if (val == NULL || Z_TYPE_P(val) == IS_NULL) {
		context = async_context_get();
	} else {
		context = (async_context *) Z_OBJ_P(val);
	}

	do {
		if (context->var == var) {
			RETURN_ZVAL(&context->value, 1, 0);
		}

		context = context->parent;
	} while (context != NULL);
}

ZEND_BEGIN_ARG_INFO(arginfo_context_var_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_context_var_get, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_context_var_functions[] = {
	ZEND_ME(ContextVar, __construct, arginfo_context_var_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(ContextVar, get, arginfo_context_var_get, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static void chain_handler(void *obj, zval *error)
{
	async_cancellation_handler *handler;
	async_cancel_cb *cancel;

	handler = (async_cancellation_handler *) obj;

	ZVAL_COPY(&handler->error, error);

	while (handler->callbacks.first != NULL) {
		ASYNC_Q_DEQUEUE(&handler->callbacks, cancel);

		cancel->func(cancel->object, &handler->error);
	}
}

static zend_object *async_cancellation_handler_object_create(zend_class_entry *ce)
{
	async_cancellation_handler *handler;

	handler = emalloc(sizeof(async_cancellation_handler));
	ZEND_SECURE_ZERO(handler, sizeof(async_cancellation_handler));

	zend_object_std_init(&handler->std, ce);
	handler->std.handlers = &async_cancellation_handler_handlers;

	ZVAL_UNDEF(&handler->error);

	return &handler->std;
}

static void async_cancellation_handler_object_destroy(zend_object *object)
{
	async_cancellation_handler *handler;
	async_context *parent;

	handler = (async_cancellation_handler *) object;

	ZEND_ASSERT(handler->callbacks.first == NULL);

	if (handler->context != NULL) {
		if (handler->context->parent != NULL) {
			parent = handler->context->parent;

			if (parent->cancel != NULL) {
				ASYNC_Q_DETACH(&parent->cancel->callbacks, &handler->chain);
			}

			OBJ_RELEASE(&parent->std);
		}

		OBJ_RELEASE(&handler->context->std);
	}

	zval_ptr_dtor(&handler->error);

	zend_object_std_dtor(&handler->std);
}

ZEND_METHOD(CancellationHandler, __construct)
{
	async_cancellation_handler *handler;
	async_context *context;
	async_context *prev;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	handler = (async_cancellation_handler *) Z_OBJ_P(getThis());

	if (val == NULL || Z_TYPE_P(val) == IS_NULL) {
		prev = async_context_get();
	} else {
		prev = (async_context *) Z_OBJ_P(val);
	}

	context = async_context_object_create(NULL, NULL);
	context->parent = prev;
	context->cancel = handler;

	handler->context = context;

	GC_ADDREF(&prev->std);
	GC_ADDREF(&handler->std);

	// Connect cancellation to parent cancellation handler.
	if (prev->cancel != NULL) {
		if (Z_TYPE_P(&prev->cancel->error) != IS_UNDEF) {
			ZVAL_COPY(&handler->error, &prev->cancel->error);

			return;
		}

		handler->chain.object = handler;
		handler->chain.func = chain_handler;

		ASYNC_Q_ENQUEUE(&prev->cancel->callbacks, &handler->chain);
	}
}

ZEND_METHOD(CancellationHandler, context)
{
	async_cancellation_handler *handler;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	handler = (async_cancellation_handler *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &handler->context->std);

	RETURN_ZVAL(&obj, 1, 0);
}

ZEND_METHOD(CancellationHandler, cancel)
{
	async_cancellation_handler *handler;
	async_cancel_cb *cancel;

	zval *err;

	err = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(err)
	ZEND_PARSE_PARAMETERS_END();

	handler = (async_cancellation_handler *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&handler->error) != IS_UNDEF) {
		return;
	}

	zend_throw_error(NULL, "Context has been cancelled");

	ZVAL_OBJ(&handler->error, EG(exception));
	EG(exception) = NULL;

	if (err != NULL && Z_TYPE_P(err) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&handler->error), Z_OBJ_P(err));
		GC_ADDREF(Z_OBJ_P(err));
	}

	while (handler->callbacks.first != NULL) {
		ASYNC_Q_DEQUEUE(&handler->callbacks, cancel);

		cancel->func(cancel->object, &handler->error);
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_cancellation_handler_ctor, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_cancellation_handler_context, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cancellation_handler_cancel, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_cancellation_handler_functions[] = {
	ZEND_ME(CancellationHandler, __construct, arginfo_cancellation_handler_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(CancellationHandler, context, arginfo_cancellation_handler_context, ZEND_ACC_PUBLIC)
	ZEND_ME(CancellationHandler, cancel, arginfo_cancellation_handler_cancel, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_context_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Context", async_context_functions);
	async_context_ce = zend_register_internal_class(&ce);
	async_context_ce->ce_flags |= ZEND_ACC_FINAL;
	async_context_ce->serialize = zend_class_serialize_deny;
	async_context_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_context_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_context_handlers.free_obj = async_context_object_destroy;
	async_context_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\ContextVar", async_context_var_functions);
	async_context_var_ce = zend_register_internal_class(&ce);
	async_context_var_ce->ce_flags |= ZEND_ACC_FINAL;
	async_context_var_ce->create_object = async_context_var_object_create;
	async_context_var_ce->serialize = zend_class_serialize_deny;
	async_context_var_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_context_var_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_context_var_handlers.free_obj = async_context_var_object_destroy;
	async_context_var_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\CancellationHandler", async_cancellation_handler_functions);
	async_cancellation_handler_ce = zend_register_internal_class(&ce);
	async_cancellation_handler_ce->ce_flags |= ZEND_ACC_FINAL;
	async_cancellation_handler_ce->create_object = async_cancellation_handler_object_create;
	async_cancellation_handler_ce->serialize = zend_class_serialize_deny;
	async_cancellation_handler_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_cancellation_handler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_cancellation_handler_handlers.free_obj = async_cancellation_handler_object_destroy;
	async_cancellation_handler_handlers.clone_obj = NULL;
}

void async_context_shutdown()
{
	async_context *context;

	context = ASYNC_G(context);

	if (context != NULL) {
		OBJ_RELEASE(&context->std);
	}
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
