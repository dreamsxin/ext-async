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

#include "php_async.h"

zend_class_entry *async_deferred_ce;
zend_class_entry *async_deferred_awaitable_ce;

const zend_uchar ASYNC_DEFERRED_STATUS_PENDING = 0;
const zend_uchar ASYNC_DEFERRED_STATUS_RESOLVED = ASYNC_OP_RESOLVED;
const zend_uchar ASYNC_DEFERRED_STATUS_FAILED = ASYNC_OP_FAILED;

static zend_object_handlers async_deferred_handlers;
static zend_object_handlers async_deferred_awaitable_handlers;

static void invoke_continuation_callback(zend_fcall_info *fci, zend_fcall_info_cache *fcc)
{
	async_task_scheduler *scheduler;
	zend_bool flag;

	scheduler = ASYNC_G(current_scheduler);

	if (scheduler == NULL) {
		scheduler = async_task_scheduler_get();

		flag = scheduler->running;
		scheduler->running = 1;

		zend_call_function(fci, fcc);

		scheduler->running = flag;
	} else {
		flag = scheduler->dispatching;
		scheduler->dispatching = 0;

		zend_call_function(fci, fcc);

		scheduler->dispatching = flag;
	}
}

static void combine_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_deferred_combine *combined;
	uint32_t i;

	zval args[5];
	zval retval;

	combined = (async_deferred_combine *) obj;
	combined->counter--;

	ZVAL_OBJ(&args[0], &combined->defer->std);
	GC_ADDREF(&combined->defer->std);

	ZVAL_BOOL(&args[1], combined->counter == 0);
	ZVAL_COPY(&args[2], data);

	if (result == NULL) {
		zend_throw_error(NULL, "Awaitable has been disposed before it was resolved");

		ZVAL_OBJ(&args[3], EG(exception));
		EG(exception) = NULL;

		ZVAL_NULL(&args[4]);
	} else if (success) {
		ZVAL_NULL(&args[3]);
		ZVAL_COPY(&args[4], result);
	} else {
		ZVAL_COPY(&args[3], result);
		ZVAL_NULL(&args[4]);
	}

	combined->fci.param_count = 5;
	combined->fci.params = args;
	combined->fci.retval = &retval;

	invoke_continuation_callback(&combined->fci, &combined->fcc);

	for (i = 0; i < 5; i++) {
		zval_ptr_dtor(&args[i]);
	}

	zval_ptr_dtor(&retval);

	if (UNEXPECTED(EG(exception))) {
		if (combined->defer->status == ASYNC_DEFERRED_STATUS_PENDING) {
			combined->defer->status = ASYNC_DEFERRED_STATUS_FAILED;

			ZVAL_OBJ(&combined->defer->result, EG(exception));
			EG(exception) = NULL;

			async_awaitable_trigger_continuation(&combined->defer->continuation, &combined->defer->result, 0);
		} else {
			EG(exception) = NULL;
		}
	}

	if (combined->counter == 0) {
		zval_ptr_dtor(&combined->fci.function_name);

		if (combined->defer->status == ASYNC_DEFERRED_STATUS_PENDING) {
			combined->defer->status = ASYNC_DEFERRED_STATUS_FAILED;

			zend_throw_error(NULL, "Awaitable has been disposed before it was resolved");

			ZVAL_OBJ(&combined->defer->result, EG(exception));
			EG(exception) = NULL;

			async_awaitable_trigger_continuation(&combined->defer->continuation, &combined->defer->result, 0);
		}

		OBJ_RELEASE(&combined->defer->std);

		efree(combined);
	}
}

static void transform_continuation(void *obj, zval *data, zval *result, zend_bool success)
{
	async_deferred_transform *trans;

	zval args[2];
	zval retval;

	trans = (async_deferred_transform *) obj;

	if (result == NULL) {
		zend_throw_error(NULL, "Awaitable has been disposed before it was resolved");

		ZVAL_OBJ(&args[0], EG(exception));
		EG(exception) = NULL;

		ZVAL_NULL(&args[1]);
	} else if (success) {
		ZVAL_NULL(&args[0]);
		ZVAL_COPY(&args[1], result);
	} else {
		ZVAL_COPY(&args[0], result);
		ZVAL_NULL(&args[1]);
	}

	trans->fci.param_count = 2;
	trans->fci.params = args;
	trans->fci.retval = &retval;

	invoke_continuation_callback(&trans->fci, &trans->fcc);

	zval_ptr_dtor(&trans->fci.function_name);

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);

	if (UNEXPECTED(EG(exception))) {
		trans->defer->status = ASYNC_DEFERRED_STATUS_FAILED;

		ZVAL_OBJ(&trans->defer->result, EG(exception));
		EG(exception) = NULL;

		async_awaitable_trigger_continuation(&trans->defer->continuation, &trans->defer->result, 0);
	} else {
		trans->defer->status = ASYNC_DEFERRED_STATUS_RESOLVED;

		ZVAL_COPY(&trans->defer->result, &retval);

		async_awaitable_trigger_continuation(&trans->defer->continuation, &trans->defer->result, 1);
	}

	zval_ptr_dtor(&retval);

	OBJ_RELEASE(&trans->defer->std);

	efree(trans);
}


static async_deferred_awaitable *async_deferred_awaitable_object_create(async_deferred *defer)
{
	async_deferred_awaitable *awaitable;

	awaitable = emalloc(sizeof(async_deferred));
	ZEND_SECURE_ZERO(awaitable, sizeof(async_deferred));

	zend_object_std_init(&awaitable->std, async_deferred_awaitable_ce);
	awaitable->std.handlers = &async_deferred_awaitable_handlers;

	awaitable->defer = defer;

	GC_ADDREF(&defer->std);

	return awaitable;
}

static void async_deferred_awaitable_object_destroy(zend_object *object)
{
	async_deferred_awaitable *awaitable;

	awaitable = (async_deferred_awaitable *) object;

	OBJ_RELEASE(&awaitable->defer->std);

	zend_object_std_dtor(&awaitable->std);
}

ZEND_METHOD(DeferredAwaitable, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Deferred awaitable must not be created from userland code");
}

ZEND_METHOD(DeferredAwaitable, __debugInfo)
{
	async_deferred *defer;

	HashTable *info;

	ZEND_PARSE_PARAMETERS_NONE();

	defer = ((async_deferred_awaitable *) Z_OBJ_P(getThis()))->defer;

	if (USED_RET()) {
		info = async_info_init();

		async_info_prop_cstr(info, "status", async_status_label(defer->status));
		async_info_prop(info, "result", &defer->result);

		RETURN_ARR(info);
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_deferred_awaitable_debug_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_deferred_awaitable_ctor, 0, 0, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_awaitable_functions[] = {
	ZEND_ME(DeferredAwaitable, __construct, arginfo_deferred_awaitable_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(DeferredAwaitable, __debugInfo, arginfo_deferred_awaitable_debug_info, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *async_deferred_object_create(zend_class_entry *ce)
{
	async_deferred *defer;

	defer = emalloc(sizeof(async_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(async_deferred));

	defer->status = ASYNC_DEFERRED_STATUS_PENDING;

	ZVAL_NULL(&defer->result);

	zend_object_std_init(&defer->std, ce);
	defer->std.handlers = &async_deferred_handlers;

	return &defer->std;
}

static void async_deferred_object_destroy(zend_object *object)
{
	async_deferred *defer;

	defer = (async_deferred *) object;

	defer->status = ASYNC_DEFERRED_STATUS_FAILED;

	async_awaitable_trigger_continuation(&defer->continuation, NULL, 0);

	zval_ptr_dtor(&defer->result);

	zend_object_std_dtor(&defer->std);
}

ZEND_METHOD(Deferred, __debugInfo)
{
	async_deferred *defer;

	HashTable *info;

	ZEND_PARSE_PARAMETERS_NONE();

	defer = (async_deferred *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		info = async_info_init();

		async_info_prop_cstr(info, "status", async_status_label(defer->status));
		async_info_prop(info, "result", &defer->result);

		RETURN_ARR(info);
	}
}

ZEND_METHOD(Deferred, awaitable)
{
	async_deferred *defer;

	zval obj;

	ZEND_PARSE_PARAMETERS_NONE();

	defer = (async_deferred *) Z_OBJ_P(getThis());

	ZVAL_OBJ(&obj, &async_deferred_awaitable_object_create(defer)->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Deferred, resolve)
{
	async_deferred *defer;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	defer = (async_deferred *) Z_OBJ_P(getThis());

	if (val != NULL && Z_TYPE_P(val) == IS_OBJECT) {
		if (instanceof_function_ex(Z_OBJCE_P(val), async_awaitable_ce, 1) != 0) {
			zend_throw_error(NULL, "Deferred must not be resolved with an object implementing Awaitable");
			return;
		}
	}

	if (defer->status != ASYNC_DEFERRED_STATUS_PENDING) {
		return;
	}

	if (val != NULL) {
		ZVAL_COPY(&defer->result, val);
	}

	defer->status = ASYNC_DEFERRED_STATUS_RESOLVED;

	async_awaitable_trigger_continuation(&defer->continuation, &defer->result, 1);
}

ZEND_METHOD(Deferred, fail)
{
	async_deferred *defer;

	zval *error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	defer = (async_deferred *) Z_OBJ_P(getThis());

	if (defer->status != ASYNC_DEFERRED_STATUS_PENDING) {
		return;
	}

	ZVAL_COPY(&defer->result, error);

	defer->status = ASYNC_DEFERRED_STATUS_FAILED;

	async_awaitable_trigger_continuation(&defer->continuation, &defer->result, 0);
}

ZEND_METHOD(Deferred, value)
{
	async_deferred *defer;
	async_deferred_awaitable *awaitable;

	zval *val;
	zval obj;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	if (val != NULL && Z_TYPE_P(val) == IS_OBJECT) {
		if (instanceof_function_ex(Z_OBJCE_P(val), async_awaitable_ce, 1) != 0) {
			zend_throw_error(NULL, "Deferred must not be resolved with an object implementing Awaitable");
			return;
		}
	}

	defer = emalloc(sizeof(async_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(async_deferred));

	defer->status = ASYNC_DEFERRED_STATUS_RESOLVED;

	if (val == NULL) {
		ZVAL_NULL(&defer->result);
	} else {
		ZVAL_COPY(&defer->result, val);
	}

	zend_object_std_init(&defer->std, async_deferred_ce);
	defer->std.handlers = &async_deferred_handlers;

	awaitable = async_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	OBJ_RELEASE(&defer->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Deferred, error)
{
	async_deferred *defer;
	async_deferred_awaitable *awaitable;

	zval *error;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	defer = emalloc(sizeof(async_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(async_deferred));

	defer->status = ASYNC_DEFERRED_STATUS_FAILED;

	ZVAL_COPY(&defer->result, error);

	zend_object_std_init(&defer->std, async_deferred_ce);
	defer->std.handlers = &async_deferred_handlers;

	awaitable = async_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	OBJ_RELEASE(&defer->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Deferred, combine)
{
	async_deferred *defer;
	async_deferred_awaitable *awaitable;
	async_deferred_combine *combined;
	async_task *task;
	async_deferred_awaitable *inner;

	zend_class_entry *ce;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t count;

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

	count = zend_array_count(Z_ARRVAL_P(args));

	if (count == 0) {
		zend_throw_error(zend_ce_argument_count_error, "At least one awaitable is required");
		return;
	}

	fci.no_separation = 1;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(args), entry) {
		ce = (Z_TYPE_P(entry) == IS_OBJECT) ? Z_OBJCE_P(entry) : NULL;

		if (ce != async_task_ce && ce != async_deferred_awaitable_ce) {
			zend_throw_error(zend_ce_type_error, "All input elements must be awaitable");
			return;
		}
	} ZEND_HASH_FOREACH_END();

	defer = emalloc(sizeof(async_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(async_deferred));

	zend_object_std_init(&defer->std, async_deferred_ce);
	defer->std.handlers = &async_deferred_handlers;

	defer->status = ASYNC_DEFERRED_STATUS_PENDING;

	awaitable = async_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	combined = emalloc(sizeof(async_deferred_combine));
	combined->defer = defer;
	combined->counter = count;
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

		if (ce == async_task_ce) {
			task = (async_task *) Z_OBJ_P(entry);

			if (task->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
				combine_continuation(combined, &key, &task->result, 1);
			} else if (task->fiber.status == ASYNC_FIBER_STATUS_FAILED) {
				combine_continuation(combined, &key, &task->result, 0);
			} else {
				async_awaitable_register_continuation(&task->continuation, combined, &key, combine_continuation);
			}
		} else {
			inner = (async_deferred_awaitable *) Z_OBJ_P(entry);

			if (inner->defer->status == ASYNC_DEFERRED_STATUS_RESOLVED) {
				combine_continuation(combined, &key, &inner->defer->result, 1);
			} else if (inner->defer->status == ASYNC_DEFERRED_STATUS_FAILED) {
				combine_continuation(combined, &key, &inner->defer->result, 0);
			} else {
				async_awaitable_register_continuation(&inner->defer->continuation, combined, &key, combine_continuation);
			}
		}

		zval_ptr_dtor(&key);
	} ZEND_HASH_FOREACH_END();

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Deferred, transform)
{
	async_deferred *defer;
	async_deferred_awaitable *awaitable;
	async_deferred_transform *trans;
	async_task *task;
	async_deferred_awaitable *inner;

	zend_class_entry *ce;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	zval *val;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 2)
		Z_PARAM_ZVAL(val)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	defer = emalloc(sizeof(async_deferred));
	ZEND_SECURE_ZERO(defer, sizeof(async_deferred));

	zend_object_std_init(&defer->std, async_deferred_ce);
	defer->std.handlers = &async_deferred_handlers;

	defer->status = ASYNC_DEFERRED_STATUS_PENDING;

	awaitable = async_deferred_awaitable_object_create(defer);

	ZVAL_OBJ(&obj, &awaitable->std);

	trans = emalloc(sizeof(async_deferred_transform));
	trans->defer = defer;
	trans->fci = fci;
	trans->fcc = fcc;

	Z_TRY_ADDREF_P(&trans->fci.function_name);

	ce = Z_OBJCE_P(val);

	if (ce == async_task_ce) {
		task = (async_task *) Z_OBJ_P(val);

		if (task->fiber.status == ASYNC_FIBER_STATUS_FINISHED) {
			transform_continuation(trans, NULL, &task->result, 1);
		} else if (task->fiber.status == ASYNC_FIBER_STATUS_FAILED) {
			transform_continuation(trans, NULL, &task->result, 0);
		} else {
			async_awaitable_register_continuation(&task->continuation, trans, NULL, transform_continuation);
		}
	} else {
		inner = (async_deferred_awaitable *) Z_OBJ_P(val);

		if (inner->defer->status == ASYNC_DEFERRED_STATUS_RESOLVED) {
			transform_continuation(trans, NULL, &inner->defer->result, 1);
		} else if (inner->defer->status == ASYNC_DEFERRED_STATUS_FAILED) {
			transform_continuation(trans, NULL, &inner->defer->result, 0);
		} else {
			async_awaitable_register_continuation(&inner->defer->continuation, trans, NULL, transform_continuation);
		}
	}

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO(arginfo_deferred_debug_info, 0)
ZEND_END_ARG_INFO()

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
	ZEND_ARG_ARRAY_INFO(0, awaitables, 0)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_deferred_transform, 0, 2, Concurrent\\Awaitable, 0)
	ZEND_ARG_OBJ_INFO(0, awaitable, Concurrent\\Awaitable, 0)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry deferred_functions[] = {
	ZEND_ME(Deferred, __debugInfo, arginfo_deferred_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, awaitable, arginfo_deferred_awaitable, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, resolve, arginfo_deferred_resolve, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, fail, arginfo_deferred_fail, ZEND_ACC_PUBLIC)
	ZEND_ME(Deferred, value, arginfo_deferred_value, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Deferred, error, arginfo_deferred_error, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Deferred, combine, arginfo_deferred_combine, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Deferred, transform, arginfo_deferred_transform, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


void async_deferred_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Deferred", deferred_functions);
	async_deferred_ce = zend_register_internal_class(&ce);
	async_deferred_ce->ce_flags |= ZEND_ACC_FINAL;
	async_deferred_ce->create_object = async_deferred_object_create;
	async_deferred_ce->serialize = zend_class_serialize_deny;
	async_deferred_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_deferred_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_deferred_handlers.free_obj = async_deferred_object_destroy;
	async_deferred_handlers.clone_obj = NULL;

	INIT_CLASS_ENTRY(ce, "Concurrent\\DeferredAwaitable", deferred_awaitable_functions);
	async_deferred_awaitable_ce = zend_register_internal_class(&ce);
	async_deferred_awaitable_ce->ce_flags |= ZEND_ACC_FINAL;
	async_deferred_awaitable_ce->serialize = zend_class_serialize_deny;
	async_deferred_awaitable_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_deferred_awaitable_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_deferred_awaitable_handlers.free_obj = async_deferred_awaitable_object_destroy;
	async_deferred_awaitable_handlers.clone_obj = NULL;

	zend_class_implements(async_deferred_awaitable_ce, 1, async_awaitable_ce);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
