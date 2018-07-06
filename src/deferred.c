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

zend_class_entry *concurrent_deferred_ce;
zend_class_entry *concurrent_deferred_awaitable_ce;

const zend_uchar CONCURRENT_DEFERRED_STATUS_PENDING = 0;
const zend_uchar CONCURRENT_DEFERRED_STATUS_SUCCEEDED = 1;
const zend_uchar CONCURRENT_DEFERRED_STATUS_FAILED = 2;

static zend_object_handlers concurrent_deferred_handlers;
static zend_object_handlers concurrent_deferred_awaitable_handlers;


static concurrent_deferred_awaitable *concurrent_deferred_awaitable_object_create(concurrent_deferred *defer)
{
	concurrent_deferred_awaitable *awaitable;

	awaitable = emalloc(sizeof(concurrent_deferred));
	ZEND_SECURE_ZERO(awaitable, sizeof(concurrent_deferred));

	zend_object_std_init(&awaitable->std, concurrent_deferred_awaitable_ce);
	awaitable->std.handlers = &concurrent_deferred_awaitable_handlers;

	awaitable->defer = defer;

	GC_ADDREF(&defer->std);

	return awaitable;
}

static void concurrent_deferred_awaitable_object_destroy(zend_object *object)
{
	concurrent_deferred_awaitable *awaitable;

	awaitable = (concurrent_deferred_awaitable *) object;

	OBJ_RELEASE(&awaitable->defer->std);

	zend_object_std_dtor(&awaitable->std);
}

ZEND_METHOD(DeferredAwaitable, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Deferred awaitable must not be created from userland code");
}

ZEND_METHOD(DeferredAwaitable, continueWith)
{
	concurrent_deferred *defer;
	concurrent_deferred_continuation_cb *cont;
	concurrent_deferred_continuation_cb *tmp;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval args[2];
	zval result;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	fci.no_separation = 1;

	defer = ((concurrent_deferred_awaitable *) Z_OBJ_P(getThis()))->defer;

	if (defer->status == CONCURRENT_DEFERRED_STATUS_SUCCEEDED) {
		ZVAL_NULL(&args[0]);
		ZVAL_COPY(&args[1], &defer->result);

		fci.param_count = 2;
		fci.params = args;
		fci.retval = &result;

		zend_call_function(&fci, &fcc);
		zval_ptr_dtor(args);
		zval_ptr_dtor(&result);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(defer->context);
		}

		return;
	}

	if (defer->status == CONCURRENT_DEFERRED_STATUS_FAILED) {
		ZVAL_COPY(&args[0], &defer->result);

		fci.param_count = 1;
		fci.params = args;
		fci.retval = &result;

		zend_call_function(&fci, &fcc);
		zval_ptr_dtor(args);
		zval_ptr_dtor(&result);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(defer->context);
		}

		return;
	}

	cont = emalloc(sizeof(concurrent_deferred_continuation_cb));
	cont->fci = fci;
	cont->fcc = fcc;
	cont->next = NULL;

	if (defer->continuation == NULL) {
		defer->continuation = cont;
	} else {
		tmp = defer->continuation;

		while (tmp->next != NULL) {
			tmp = tmp->next;
		}

		tmp->next = cont;
	}

	Z_TRY_ADDREF_P(&fci.function_name);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_awaitable_ctor, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_awaitable_continue_with, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_awaitable_functions[] = {
	ZEND_ME(DeferredAwaitable, __construct, arginfo_deferred_awaitable_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(DeferredAwaitable, continueWith, arginfo_deferred_awaitable_continue_with, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *concurrent_deferred_object_create(zend_class_entry *ce)
{
	concurrent_deferred *defer;

	defer = emalloc(sizeof(concurrent_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(concurrent_deferred));

	defer->status = CONCURRENT_DEFERRED_STATUS_PENDING;

	ZVAL_NULL(&defer->result);

	zend_object_std_init(&defer->std, ce);
	defer->std.handlers = &concurrent_deferred_handlers;

	return &defer->std;
}

static void concurrent_deferred_object_destroy(zend_object *object)
{
	concurrent_deferred *defer;
	concurrent_deferred_continuation_cb *cont;

	defer = (concurrent_deferred *) object;

	zval_ptr_dtor(&defer->result);

	while (defer->continuation != NULL) {
		cont = defer->continuation;
		defer->continuation = cont->next;

		zval_ptr_dtor(&cont->fci.function_name);

		efree(cont);
	}

	if (defer->context != NULL) {
		OBJ_RELEASE(&defer->context->std);
	}

	zend_object_std_dtor(&defer->std);
}

ZEND_METHOD(Deferred, __construct)
{
	concurrent_deferred *defer;
	concurrent_context *context;

	zval *ctx;

	ctx = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(ctx)
	ZEND_PARSE_PARAMETERS_END();

	defer = (concurrent_deferred *) Z_OBJ_P(getThis());

	if (ctx == NULL || Z_TYPE_P(ctx) == IS_NULL) {
		context = TASK_G(current_context);

		if (UNEXPECTED(context == NULL)) {
			zend_throw_error(NULL, "No context passed to constructor and no context is running");
			return;
		}
	} else {
		context = (concurrent_context *) Z_OBJ_P(ctx);
	}

	defer->context = context;

	GC_ADDREF(&defer->context->std);
}

ZEND_METHOD(Deferred, awaitable)
{
	concurrent_deferred *defer;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	defer = (concurrent_deferred *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &concurrent_deferred_awaitable_object_create(defer)->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Deferred, succeed)
{
	concurrent_deferred *defer;
	concurrent_deferred_continuation_cb *cont;
	concurrent_context *context;

	zval *val;
	zval args[2];
	zval retval;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	defer = (concurrent_deferred *) Z_OBJ_P(getThis());

	if (defer->status != CONCURRENT_DEFERRED_STATUS_PENDING) {
		return;
	}

	if (val != NULL) {
		ZVAL_COPY(&defer->result, val);
	}

	defer->status = CONCURRENT_DEFERRED_STATUS_SUCCEEDED;

	context = TASK_G(current_context);
	TASK_G(current_context) = defer->context;

	while (defer->continuation != NULL) {
		cont = defer->continuation;
		defer->continuation = cont->next;

		ZVAL_NULL(&args[0]);
		ZVAL_COPY(&args[1], &defer->result);

		cont->fci.param_count = 2;
		cont->fci.params = args;
		cont->fci.retval = &retval;

		zend_call_function(&cont->fci, &cont->fcc);

		zval_ptr_dtor(args);
		zval_ptr_dtor(&retval);
		zval_ptr_dtor(&cont->fci.function_name);

		efree(cont);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(defer->context);
		}
	}

	TASK_G(current_context) = context;
}

ZEND_METHOD(Deferred, fail)
{
	concurrent_deferred *defer;
	concurrent_deferred_continuation_cb *cont;
	concurrent_context *context;

	zval *error;
	zval args[1];
	zval retval;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	defer = (concurrent_deferred *) Z_OBJ_P(getThis());

	if (defer->status != CONCURRENT_DEFERRED_STATUS_PENDING) {
		return;
	}

	ZVAL_COPY(&defer->result, error);

	defer->status = CONCURRENT_DEFERRED_STATUS_FAILED;

	context = TASK_G(current_context);
	TASK_G(current_context) = defer->context;

	while (defer->continuation != NULL) {
		cont = defer->continuation;
		defer->continuation = cont->next;

		ZVAL_COPY(&args[0], &defer->result);

		cont->fci.param_count = 1;
		cont->fci.params = args;
		cont->fci.retval = &retval;

		zend_call_function(&cont->fci, &cont->fcc);

		zval_ptr_dtor(args);
		zval_ptr_dtor(&retval);
		zval_ptr_dtor(&cont->fci.function_name);

		efree(cont);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(defer->context);
		}
	}

	TASK_G(current_context) = context;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_ctor, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_deferred_awaitable, 0, 0, Concurrent\\Awaitable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_succeed, 0, 0, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_fail, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_functions[] = {
	ZEND_ME(Deferred, __construct, arginfo_deferred_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_FINAL | ZEND_ACC_CTOR)
	ZEND_ME(Deferred, awaitable, arginfo_deferred_awaitable, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, succeed, arginfo_deferred_succeed, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, fail, arginfo_deferred_fail, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void concurrent_deferred_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Deferred", deferred_functions);
	concurrent_deferred_ce = zend_register_internal_class(&ce);
	concurrent_deferred_ce->create_object = concurrent_deferred_object_create;
	concurrent_deferred_ce->serialize = zend_class_serialize_deny;
	concurrent_deferred_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_deferred_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_deferred_handlers.free_obj = concurrent_deferred_object_destroy;
	concurrent_deferred_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\DeferredAwaitable", deferred_awaitable_functions);
	concurrent_deferred_awaitable_ce = zend_register_internal_class(&ce);
	concurrent_deferred_awaitable_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_deferred_awaitable_ce->serialize = zend_class_serialize_deny;
	concurrent_deferred_awaitable_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_deferred_awaitable_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_deferred_awaitable_handlers.free_obj = concurrent_deferred_awaitable_object_destroy;
	concurrent_deferred_awaitable_handlers.clone_obj = NULL;

	zend_class_implements(concurrent_deferred_awaitable_ce, 1, concurrent_awaitable_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
