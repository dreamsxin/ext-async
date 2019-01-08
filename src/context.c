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

zend_class_entry *async_context_ce;
zend_class_entry *async_context_var_ce;
zend_class_entry *async_cancellation_handler_ce;
zend_class_entry *async_cancellation_exception_ce;

static zend_object_handlers async_context_handlers;
static zend_object_handlers async_context_var_handlers;
static zend_object_handlers async_cancellation_handler_handlers;

static async_context *async_context_object_create(async_context_var *var, zval *value);
static async_cancellation_handler *async_cancellation_handler_object_create();


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

async_context *async_context_create_background()
{
	async_context *root;
	async_context *context;
	
	root = ASYNC_G(context);
	
	if (root == NULL) {
		root = async_context_object_create(NULL, NULL);
		
		ASYNC_G(context) = root;
	}
	
	context = async_context_object_create(NULL, NULL);
	context->background = 1;
	context->parent = root;
	
	ASYNC_ADDREF(&root->std);
	
	return context;
}

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

static void init_cancellation(async_cancellation_handler *handler, async_context *prev)
{
	async_context *context;

	context = async_context_object_create(NULL, NULL);
	context->parent = prev;
	context->background = prev->background;
	context->cancel = handler;

	handler->context = context;

	ASYNC_ADDREF(&prev->std);
	ASYNC_ADDREF(&handler->std);

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

static void close_timeout(uv_handle_t *handle)
{
	async_cancellation_handler *handler;

	handler = (async_cancellation_handler *) handle->data;

	ZEND_ASSERT(handler != NULL);

	ASYNC_DELREF(&handler->std);
}

static void timed_out(uv_timer_t *timer)
{
	async_cancellation_handler *handler;
	async_cancel_cb *cancel;

	handler = (async_cancellation_handler *) timer->data;

	ZEND_ASSERT(handler != NULL);

	if (Z_TYPE_P(&handler->error) != IS_UNDEF) {
		return;
	}

	ASYNC_PREPARE_EXCEPTION(&handler->error, async_cancellation_exception_ce, "Context has timed out");

	while (handler->callbacks.first != NULL) {
		ASYNC_Q_DEQUEUE(&handler->callbacks, cancel);

		cancel->func(cancel->object, &handler->error);
	}
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
		ASYNC_ADDREF(&var->std);
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
		ASYNC_DELREF(&context->var->std);
	}

	zval_ptr_dtor(&context->value);

	if (context->cancel != NULL) {
		ASYNC_DELREF(&context->cancel->std);
	}

	if (context->parent != NULL) {
		ASYNC_DELREF(&context->parent->std);
	}

	zend_object_std_dtor(&context->std);
}

static zval *read_context_prop(zval *object, zval *member, int type, void **cache_slot, zval *rv)
{
	async_context *context;
	
	char *key;
	
	context = (async_context *) Z_OBJ_P(object);
	
	key = Z_STRVAL_P(member);
	
	if (strcmp(key, "background") == 0) {
		ZVAL_BOOL(rv, context->background);
	} else if (strcmp(key, "cancelled") == 0) {
		ZVAL_BOOL(rv, context->cancel != NULL && Z_TYPE_P(&context->cancel->error) != IS_UNDEF);
	} else {
		rv = &EG(uninitialized_zval);
	}
	
	return rv;
}

static int has_context_prop(zval *object, zval *member, int has_set_exists, void **cache_slot)
{
	zval rv;
	zval *val;

    val = read_context_prop(object, member, 0, cache_slot, &rv);
    
    if (val == &EG(uninitialized_zval)) {
    	return 0;
    }
    
    switch (has_set_exists) {
    	case ZEND_PROPERTY_EXISTS:
    	case ZEND_PROPERTY_ISSET:
    		zval_ptr_dtor(val);
    		return 1;
    }
    
    convert_to_boolean(val);
    
    return (Z_TYPE_P(val) == IS_TRUE) ? 1 : 0;
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

		ASYNC_ADDREF(&current->std);
	}

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, withTimeout)
{
	async_context *context;
	async_cancellation_handler *handler;

	zend_long timeout;

	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_LONG(timeout)
	ZEND_PARSE_PARAMETERS_END();

	context = (async_context *) Z_OBJ_P(getThis());
	handler = async_cancellation_handler_object_create();

	init_cancellation(handler, context);

	ASYNC_DELREF(&handler->std);
	
	uv_timer_init(&handler->scheduler->loop, &handler->timer);

	handler->timer.data = handler;

	uv_timer_start(&handler->timer, timed_out, (uint64_t) timeout, 0);
	uv_unref((uv_handle_t *) &handler->timer);

	ZVAL_OBJ(&obj, &handler->context->std);

	RETURN_ZVAL(&obj, 1, 0);
}

ZEND_METHOD(Context, withCancel)
{
	async_context *context;
	async_cancellation_handler *handler;

	zval *val;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL_DEREF(val)
	ZEND_PARSE_PARAMETERS_END();
	
	zval_ptr_dtor(val);

	context = (async_context *) Z_OBJ_P(getThis());
	handler = async_cancellation_handler_object_create();

	init_cancellation(handler, context);
	
	ZVAL_OBJ(val, &handler->std);
	ZVAL_OBJ(&obj, &handler->context->std);

	RETURN_ZVAL(&obj, 1, 0);
}

ZEND_METHOD(Context, shield)
{
	async_context *prev;
	async_context *context;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	context = async_context_object_create(NULL, NULL);
	prev = (async_context *) Z_OBJ_P(getThis());

	context->parent = prev;
	context->background = prev->background;

	ASYNC_ADDREF(&prev->std);

	ZVAL_OBJ(&obj, &context->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Context, throwIfCancelled)
{
	async_context *context;

	ZEND_PARSE_PARAMETERS_NONE();

	context = (async_context *) Z_OBJ_P(getThis());

	if (context->cancel != NULL && Z_TYPE_P(&context->cancel->error) != IS_UNDEF) {
		Z_ADDREF_P(&context->cancel->error);

		execute_data->opline--;
		zend_throw_exception_internal(&context->cancel->error);
		execute_data->opline++;
	}
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

ZEND_METHOD(Context, current)
{
	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	ZVAL_OBJ(&obj, &async_context_get()->std);

	RETURN_ZVAL(&obj, 1, 0);
}

ZEND_METHOD(Context, isBackground)
{
	RETURN_BOOL(async_context_get()->background);
}

ZEND_BEGIN_ARG_INFO(arginfo_context_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_with, 0, 2, Concurrent\\Context, 0)
	ZEND_ARG_OBJ_INFO(0, var, Concurrent\\ContextVar, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_with_timeout, 0, 1, Concurrent\\Context, 0)
	ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_with_cancel, 0, 1, Concurrent\\Context, 0)
	ZEND_ARG_INFO(1, cancel)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_shield, 0, 0, Concurrent\\Context, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_context_is_cancelled, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_context_throw_if_cancelled, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_context_run, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_context_current, 0, 0, Concurrent\\Context, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_context_is_background, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_context_functions[] = {
	ZEND_ME(Context, __construct, arginfo_context_ctor, ZEND_ACC_PRIVATE)
	ZEND_ME(Context, with, arginfo_context_with, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, withTimeout, arginfo_context_with_timeout, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, withCancel, arginfo_context_with_cancel, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, shield, arginfo_context_shield, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, throwIfCancelled, arginfo_context_throw_if_cancelled, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, run, arginfo_context_run, ZEND_ACC_PUBLIC)
	ZEND_ME(Context, current, arginfo_context_current, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Context, isBackground, arginfo_context_is_background, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
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
	ZEND_ME(ContextVar, __construct, arginfo_context_var_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(ContextVar, get, arginfo_context_var_get, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static async_cancellation_handler *async_cancellation_handler_object_create()
{
	async_cancellation_handler *handler;

	handler = emalloc(sizeof(async_cancellation_handler));
	ZEND_SECURE_ZERO(handler, sizeof(async_cancellation_handler));

	zend_object_std_init(&handler->std, async_cancellation_handler_ce);
	handler->std.handlers = &async_cancellation_handler_handlers;

	ZVAL_UNDEF(&handler->error);
	
	handler->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&handler->scheduler->std);

	return handler;
}

static void async_cancellation_handler_object_dtor(zend_object *object)
{
	async_cancellation_handler *handler;

	handler = (async_cancellation_handler *) object;

	if (handler->timer.data != NULL) {
		uv_timer_stop(&handler->timer);

		if (!uv_is_closing((uv_handle_t *) &handler->timer)) {
			ASYNC_ADDREF(&handler->std);

			uv_close((uv_handle_t *) &handler->timer, close_timeout);
		}
	}
}

static void async_cancellation_handler_object_destroy(zend_object *object)
{
	async_cancellation_handler *handler;
	async_context *parent;

	handler = (async_cancellation_handler *) object;

	if (handler->context != NULL) {
		if (handler->context->parent != NULL) {
			parent = handler->context->parent;

			if (parent->cancel != NULL) {
				ASYNC_Q_DETACH(&parent->cancel->callbacks, &handler->chain);
			}

			ASYNC_DELREF(&parent->std);
		}
		
		ASYNC_DELREF(&handler->context->std);
	}

	zval_ptr_dtor(&handler->error);
	
	ASYNC_DELREF(&handler->scheduler->std);

	zend_object_std_dtor(&handler->std);
}

ZEND_METHOD(CancellationHandler, __invoke)
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

	if (handler->scheduler != NULL) {
		uv_timer_stop(&handler->timer);
	}

	ASYNC_PREPARE_EXCEPTION(&handler->error, async_cancellation_exception_ce, "Context has been cancelled");

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

ZEND_BEGIN_ARG_INFO_EX(arginfo_cancellation_handler_invoke, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

static const zend_function_entry async_cancellation_handler_functions[] = {
	ZEND_ME(CancellationHandler, __invoke, arginfo_cancellation_handler_invoke, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static const zend_function_entry empty_funcs[] = {
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
	async_context_handlers.has_property = has_context_prop;
	async_context_handlers.read_property = read_context_prop;

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
	async_cancellation_handler_ce->create_object = NULL;
	async_cancellation_handler_ce->serialize = zend_class_serialize_deny;
	async_cancellation_handler_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_cancellation_handler_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_cancellation_handler_handlers.dtor_obj = async_cancellation_handler_object_dtor;
	async_cancellation_handler_handlers.free_obj = async_cancellation_handler_object_destroy;
	async_cancellation_handler_handlers.clone_obj = NULL;
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\CancellationException", empty_funcs);
	async_cancellation_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_cancellation_exception_ce, zend_ce_exception);
}

void async_context_shutdown()
{
	async_context *context;

	context = ASYNC_G(context);

	if (context != NULL) {
		ASYNC_DELREF(&context->std);
	}
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
