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

#include "async_task.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_timer_ce;

static zend_object_handlers async_timer_handlers;


static void trigger_timer(uv_timer_t *handle)
{
	async_timer *timer;

	zval result;

	timer = (async_timer *) handle->data;

	ZEND_ASSERT(timer != NULL);

	ZVAL_NULL(&result);

	async_awaitable_trigger_continuation(&timer->timeouts, &result, 1);
}

static void enable_timer(void *obj)
{
	async_timer *timer;

	timer = (async_timer *) obj;

	timer->running = timer->new_running;

	if (timer->running) {
		uv_timer_start(&timer->timer, trigger_timer, timer->delay, timer->delay);
	} else {
		uv_timer_stop(&timer->timer);
	}

	OBJ_RELEASE(&timer->std);
}

static inline void suspend(async_timer *timer, zval *return_value, zend_execute_data *execute_data)
{
	async_context *context;

	context = async_context_get();

	if (context->background) {
		timer->unref_count++;

		if (timer->unref_count == 1 && !timer->ref_count) {
			uv_unref((uv_handle_t *) &timer->timer);
		}
	} else {
		timer->ref_count++;

		if (timer->ref_count == 1 && timer->unref_count) {
			uv_ref((uv_handle_t *) &timer->timer);
		}
	}

	async_task_suspend(&timer->timeouts, return_value, execute_data);

	if (context->background) {
		timer->unref_count--;

		if (timer->unref_count == 0 && timer->ref_count) {
			uv_ref((uv_handle_t *) &timer->timer);
		}
	} else {
		timer->ref_count--;

		if (timer->ref_count == 0 && timer->unref_count) {
			uv_unref((uv_handle_t *) &timer->timer);
		}
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

	timer->enable.object = timer;
	timer->enable.func = enable_timer;

	return &timer->std;
}

static void async_timer_object_destroy(zend_object *object)
{
	async_timer *timer;

	timer = (async_timer *) object;

	zend_object_std_dtor(&timer->std);
}

ZEND_METHOD(Timer, __construct)
{
	async_timer *timer;
	uint64_t delay;

	zval *a;

	timer = (async_timer *)Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(a)
	ZEND_PARSE_PARAMETERS_END();

	delay = (uint64_t) Z_LVAL_P(a);

	ASYNC_CHECK_ERROR(delay < 0, "Delay must not be shorter than 0 milliseconds");

	timer->delay = delay;
	timer->scheduler = async_task_scheduler_get();
}

ZEND_METHOD(Timer, stop)
{
	async_timer *timer;

	zval *val;
	zval error;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	timer = (async_timer *)Z_OBJ_P(getThis());

	zend_throw_error(NULL, "Timer has been closed");

	ZVAL_OBJ(&error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&timer->timeouts, &error, 0);
}

ZEND_METHOD(Timer, awaitTimeout)
{
	async_timer *timer;

	ZEND_PARSE_PARAMETERS_NONE();

	timer = (async_timer *)Z_OBJ_P(getThis());

	timer->new_running = 1;

	if (!timer->ref_count && !timer->unref_count) {
		if (timer->running) {
			if (timer->enable.active) {
				ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
				timer->enable.active = 0;

				OBJ_RELEASE(&timer->std);
			}
		} else if (!timer->enable.active) {
			GC_ADDREF(&timer->std);

			async_task_scheduler_enqueue_enable(timer->scheduler, &timer->enable);
		}
	}

	suspend(timer, return_value, execute_data);

	if (!timer->ref_count && !timer->unref_count) {
		timer->new_running = 0;

		if (timer->running) {
			if (!timer->enable.active) {
				GC_ADDREF(&timer->std);

				async_task_scheduler_enqueue_enable(timer->scheduler, &timer->enable);
			}
		} else if (timer->enable.active) {
			ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
			timer->enable.active = 0;

			OBJ_RELEASE(&timer->std);
		}
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_stop, 0, 0, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_timer_await_timeout, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_timer_functions[] = {
	ZEND_ME(Timer, __construct, arginfo_timer_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(Timer, stop, arginfo_timer_stop, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, awaitTimeout, arginfo_timer_await_timeout, ZEND_ACC_PUBLIC)
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
