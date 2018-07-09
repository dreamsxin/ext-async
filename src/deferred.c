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
const zend_uchar CONCURRENT_DEFERRED_STATUS_RESOLVED = 1;
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

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_awaitable_ctor, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_awaitable_functions[] = {
	ZEND_ME(DeferredAwaitable, __construct, arginfo_deferred_awaitable_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
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

	defer = (concurrent_deferred *) object;

	zval_ptr_dtor(&defer->result);

	if (defer->continuation != NULL) {
		concurrent_awaitable_dispose_continuation(&defer->continuation);
	}

	zend_object_std_dtor(&defer->std);
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

ZEND_METHOD(Deferred, resolve)
{
	concurrent_deferred *defer;

	zval *val;

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

	defer->status = CONCURRENT_DEFERRED_STATUS_RESOLVED;

	concurrent_awaitable_trigger_continuation(&defer->continuation, &defer->result, 1);
}

ZEND_METHOD(Deferred, fail)
{
	concurrent_deferred *defer;

	zval *error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	defer = (concurrent_deferred *) Z_OBJ_P(getThis());

	if (defer->status != CONCURRENT_DEFERRED_STATUS_PENDING) {
		return;
	}

	ZVAL_COPY(&defer->result, error);

	defer->status = CONCURRENT_DEFERRED_STATUS_FAILED;

	concurrent_awaitable_trigger_continuation(&defer->continuation, &defer->result, 0);
}

ZEND_METHOD(Deferred, value)
{
	concurrent_deferred *defer;
	concurrent_deferred_awaitable *awaitable;

	zval *val;
	zval obj;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	defer = emalloc(sizeof(concurrent_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(concurrent_deferred));

	defer->status = CONCURRENT_DEFERRED_STATUS_RESOLVED;

	if (val == NULL) {
		ZVAL_NULL(&defer->result);
	} else {
		ZVAL_COPY(&defer->result, val);
	}

	zend_object_std_init(&defer->std, concurrent_deferred_ce);
	defer->std.handlers = &concurrent_deferred_handlers;

	awaitable = concurrent_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	OBJ_RELEASE(&defer->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Deferred, error)
{
	concurrent_deferred *defer;
	concurrent_deferred_awaitable *awaitable;

	zval *error;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	defer = emalloc(sizeof(concurrent_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(concurrent_deferred));

	defer->status = CONCURRENT_DEFERRED_STATUS_FAILED;

	ZVAL_COPY(&defer->result, error);

	zend_object_std_init(&defer->std, concurrent_deferred_ce);
	defer->std.handlers = &concurrent_deferred_handlers;

	awaitable = concurrent_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	OBJ_RELEASE(&defer->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_deferred_awaitable, 0, 0, Concurrent\\Awaitable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_resolve, 0, 0, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_fail, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_deferred_value, 0, 0, Concurrent\\Awaitable, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_deferred_error, 0, 1, Concurrent\\Awaitable, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_functions[] = {
	ZEND_ME(Deferred, awaitable, arginfo_deferred_awaitable, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, resolve, arginfo_deferred_resolve, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, fail, arginfo_deferred_fail, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, value, arginfo_deferred_value, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Deferred, error, arginfo_deferred_error, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


void concurrent_deferred_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Deferred", deferred_functions);
	concurrent_deferred_ce = zend_register_internal_class(&ce);
	concurrent_deferred_ce->ce_flags |= ZEND_ACC_FINAL;
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
