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

typedef struct _concurrent_defer_combine concurrent_defer_combine;

struct _concurrent_defer_combine {
	concurrent_deferred *defer;
	zend_long counter;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

static void concurrent_defer_combine_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	concurrent_defer_combine *combined;

	zval args[4];
	zval retval;

	combined = (concurrent_defer_combine *) obj;
	combined->counter--;

	ZVAL_OBJ(&args[0], &combined->defer->std);
	ZVAL_COPY(&args[1], data);

	if (success) {
		ZVAL_NULL(&args[2]);
		ZVAL_COPY(&args[3], result);

		combined->fci.param_count = 4;
	} else {
		ZVAL_COPY(&args[2], result);

		combined->fci.param_count = 3;
	}

	combined->fci.params = args;
	combined->fci.retval = &retval;

	zend_call_function(&combined->fci, &combined->fcc);

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	zval_ptr_dtor(&args[2]);
	zval_ptr_dtor(&args[3]);
	zval_ptr_dtor(&retval);

	if (UNEXPECTED(EG(exception))) {
		if (combined->defer->status == CONCURRENT_DEFERRED_STATUS_PENDING) {
			combined->defer->status = CONCURRENT_DEFERRED_STATUS_FAILED;

			ZVAL_OBJ(&combined->defer->result, EG(exception));
			EG(exception) = NULL;

			concurrent_awaitable_trigger_continuation(&combined->defer->continuation, &combined->defer->result, 0);
		} else {
			EG(exception) = NULL;
		}
	}

	if (combined->counter == 0) {
		zval_ptr_dtor(&combined->fci.function_name);

		if (combined->defer->status == CONCURRENT_DEFERRED_STATUS_PENDING) {
			concurrent_awaitable_dispose_continuation(&combined->defer->continuation);
		}

		OBJ_RELEASE(&combined->defer->std);

		efree(combined);
	}
}

ZEND_METHOD(Deferred, combine)
{
	concurrent_deferred *defer;
	concurrent_deferred_awaitable *awaitable;
	concurrent_defer_combine *combined;
	concurrent_task *task;
	concurrent_deferred_awaitable *inner;

	zend_class_entry *ce;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	zval *args;
	zend_ulong i;
	zend_string *k;
	zval key;
	zval *entry;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_ARRAY(args)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	fci.no_separation = 1;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(args), entry) {
		if (Z_TYPE_P(entry) != IS_OBJECT) {
			zend_throw_error(NULL, "All input elements must be awaitable");
			return;
		}

		ce = Z_OBJCE_P(entry);

		if (ce != concurrent_task_ce && ce != concurrent_deferred_awaitable_ce) {
			zend_throw_error(NULL, "All input elements must be awaitable");
			return;
		}
	} ZEND_HASH_FOREACH_END();

	defer = emalloc(sizeof(concurrent_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(concurrent_deferred));

	zend_object_std_init(&defer->std, concurrent_deferred_ce);
	defer->std.handlers = &concurrent_deferred_handlers;

	defer->status = CONCURRENT_DEFERRED_STATUS_PENDING;

	awaitable = concurrent_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	combined = emalloc(sizeof(concurrent_defer_combine));
	combined->defer = defer;
	combined->counter = zend_array_count(Z_ARRVAL_P(args));
	combined->fci = fci;
	combined->fcc = fcc;

	Z_TRY_ADDREF_P(&combined->fci.function_name);

	ZEND_HASH_FOREACH_KEY_VAL_IND(Z_ARRVAL_P(args), i, k, entry) {
		ce = Z_OBJCE_P(entry);

		if (k == NULL) {
			ZVAL_LONG(&key, i);
		} else {
			ZVAL_STR(&key, k);
		}

		if (ce == concurrent_task_ce) {
			task = (concurrent_task *) Z_OBJ_P(entry);

			if (task->fiber.status == CONCURRENT_FIBER_STATUS_FINISHED) {
				concurrent_defer_combine_continuation(combined, &key, &task->result, 1);
			} else if (task->fiber.status == CONCURRENT_FIBER_STATUS_DEAD) {
				concurrent_defer_combine_continuation(combined, &key, &task->result, 0);
			} else {
				if (task->continuation == NULL) {
					task->continuation = concurrent_awaitable_create_continuation(combined, &key, concurrent_defer_combine_continuation);
				} else {
					concurrent_awaitable_append_continuation(task->continuation, combined, &key, concurrent_defer_combine_continuation);
				}
			}
		} else {
			inner = (concurrent_deferred_awaitable *) Z_OBJ_P(entry);

			if (inner->defer->status == CONCURRENT_DEFERRED_STATUS_RESOLVED) {
				concurrent_defer_combine_continuation(combined, &key, &inner->defer->result, 1);
			} else if (inner->defer->status == CONCURRENT_DEFERRED_STATUS_FAILED) {
				concurrent_defer_combine_continuation(combined, &key, &inner->defer->result, 0);
			} else {
				if (inner->defer->continuation == NULL) {
					inner->defer->continuation = concurrent_awaitable_create_continuation(combined, &key, concurrent_defer_combine_continuation);
				} else {
					concurrent_awaitable_append_continuation(inner->defer->continuation, combined, &key, concurrent_defer_combine_continuation);
				}
			}
		}

		zval_ptr_dtor(&key);
	} ZEND_HASH_FOREACH_END();

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

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_deferred_combine, 0, 2, Concurrent\\Awaitable, 0)
	ZEND_ARG_ARRAY_INFO(0, params, 0)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_functions[] = {
	ZEND_ME(Deferred, awaitable, arginfo_deferred_awaitable, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, resolve, arginfo_deferred_resolve, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, fail, arginfo_deferred_fail, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, value, arginfo_deferred_value, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Deferred, error, arginfo_deferred_error, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Deferred, combine, arginfo_deferred_combine, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
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
