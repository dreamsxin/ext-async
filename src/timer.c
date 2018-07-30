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

zend_class_entry *async_timer_ce;

static zend_object_handlers async_timer_handlers;


static void trigger_timer(uv_timer_t *handle)
{
	async_timer *timer;

	zval args[1];
	zval retval;

	timer = (async_timer *) handle->data;

	ZEND_ASSERT(timer != NULL);

	ZVAL_OBJ(&args[0], &timer->std);
	GC_ADDREF(&timer->std);

	timer->fci.param_count = 1;
	timer->fci.params = args;
	timer->fci.retval = &retval;
	timer->fci.no_separation = 1;

	zend_call_function(&timer->fci, &timer->fcc);

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&retval);

	if (uv_timer_get_repeat(handle) == 0) {
		OBJ_RELEASE(&timer->std);
	}

	if (UNEXPECTED(EG(exception))) {
		ASYNC_CHECK_FATAL(1, "Timmer callback failed!");
	}
}


static zend_object *async_timer_object_create(zend_class_entry *ce)
{
	async_timer *timer;

	timer = emalloc(sizeof(async_timer));
	ZEND_SECURE_ZERO(timer, sizeof(async_timer));

	zend_object_std_init(&timer->std, ce);
	timer->std.handlers = &async_timer_handlers;

	uv_timer_init(async_task_scheduler_get_loop(), &timer->timer);

	timer->timer.data = timer;

	return &timer->std;
}

static void async_timer_object_destroy(zend_object *object)
{
	async_timer *timer;

	timer = (async_timer *) object;

	zval_ptr_dtor(&timer->fci.function_name);

	zend_object_std_dtor(&timer->std);
}

ZEND_METHOD(Timer, __construct)
{
	async_timer *timer;

	timer = (async_timer *)Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(timer->fci, timer->fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	Z_TRY_ADDREF_P(&timer->fci.function_name);
}

ZEND_METHOD(Timer, start)
{
	async_timer *timer;
	uint64_t delay;
	uint64_t repeat;

	zval *a;
	zval *b;

	b = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_ZVAL(a)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(b)
	ZEND_PARSE_PARAMETERS_END();

	timer = (async_timer *)Z_OBJ_P(getThis());

	delay = (uint64_t) Z_LVAL_P(a);
	repeat = (b != NULL && Z_LVAL_P(b) && delay > 0) ? delay : 0;

	ASYNC_CHECK_ERROR(delay < 0, "Delay must not be shorter than 0 milliseconds");

	if (!uv_is_active((uv_handle_t *) &timer->timer)) {
		GC_ADDREF(&timer->std);
	}

	uv_timer_start(&timer->timer, trigger_timer, delay, repeat);
}

ZEND_METHOD(Timer, stop)
{
	async_timer *timer;

	ZEND_PARSE_PARAMETERS_NONE();

	timer = (async_timer *)Z_OBJ_P(getThis());

	if (uv_is_active((uv_handle_t *) &timer->timer)) {
		uv_timer_stop(&timer->timer);

		OBJ_RELEASE(&timer->std);
	}
}

ZEND_METHOD(Timer, tick)
{
	async_timer *timer;

	zval obj;

	timer = (async_timer *) async_timer_object_create(async_timer_ce);

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(timer->fci, timer->fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	Z_TRY_ADDREF_P(&timer->fci.function_name);

	GC_ADDREF(&timer->std);

	uv_timer_start(&timer->timer, trigger_timer, 0, 0);

	ZVAL_OBJ(&obj, &timer->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_ctor, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_start, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
	ZEND_ARG_TYPE_INFO(0, repeat, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_timer_stop, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_tick, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_timer_functions[] = {
	ZEND_ME(Timer, __construct, arginfo_timer_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(Timer, start, arginfo_timer_start, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, stop, arginfo_timer_stop, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, tick, arginfo_timer_tick, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


void async_timer_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Timer", async_timer_functions);
	async_timer_ce = zend_register_internal_class(&ce);
	async_timer_ce->ce_flags |= ZEND_ACC_FINAL;
	async_timer_ce->create_object = async_timer_object_create;
	async_timer_ce->serialize = zend_class_serialize_deny;
	async_timer_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_timer_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_timer_handlers.free_obj = async_timer_object_destroy;
	async_timer_handlers.clone_obj = NULL;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
