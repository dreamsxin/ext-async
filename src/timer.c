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

static zend_function *orig_sleep;
static zif_handler orig_sleep_handler;


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

	ZEND_ASSERT(timer != NULL);

	timer->running = timer->new_running;

	if (timer->running) {
		uv_timer_start(&timer->handle, trigger_timer, timer->delay, timer->delay);
	} else {
		uv_timer_stop(&timer->handle);
	}
	
	ASYNC_DELREF(&timer->std);
}

static void close_timer(uv_handle_t *handle)
{
	async_timer *timer;

	timer = (async_timer *) handle->data;

	ZEND_ASSERT(timer != NULL);

	ASYNC_DELREF(&timer->std);
}


static zend_object *async_timer_object_create(zend_class_entry *ce)
{
	async_timer *timer;

	timer = emalloc(sizeof(async_timer));
	ZEND_SECURE_ZERO(timer, sizeof(async_timer));

	zend_object_std_init(&timer->std, ce);
	timer->std.handlers = &async_timer_handlers;

	ZVAL_UNDEF(&timer->error);
	
	timer->scheduler = async_task_scheduler_get();
	
	async_awaitable_queue_init(&timer->timeouts, timer->scheduler);

	uv_timer_init(&timer->scheduler->loop, &timer->handle);

	timer->handle.data = timer;

	timer->enable.object = timer;
	timer->enable.func = enable_timer;

	return &timer->std;
}

static void async_timer_object_dtor(zend_object *object)
{
	async_timer *timer;

	timer = (async_timer *) object;

	if (timer->enable.active) {
		ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
		timer->enable.active = 0;
	}

	if (!uv_is_closing((uv_handle_t *) &timer->handle)) {
		ASYNC_ADDREF(&timer->std);
		
		uv_close((uv_handle_t *) &timer->handle, close_timer);
	}
}

static void async_timer_object_destroy(zend_object *object)
{
	async_timer *timer;

	timer = (async_timer *) object;

	zval_ptr_dtor(&timer->error);
	
	async_awaitable_queue_destroy(&timer->timeouts);
	
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
}

ZEND_METHOD(Timer, close)
{
	async_timer *timer;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	timer = (async_timer *)Z_OBJ_P(getThis());

	if (Z_TYPE_P(&timer->error) != IS_UNDEF) {
		return;
	}

	if (timer->enable.active) {
		ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
		timer->enable.active = 0;
	} else {
		ASYNC_ADDREF(&timer->std);
	}

	uv_close((uv_handle_t *) &timer->handle, close_timer);

	zend_throw_error(NULL, "Timer has been closed");

	ZVAL_OBJ(&timer->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&timer->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&timer->timeouts, &timer->error, 0);
}

ZEND_METHOD(Timer, awaitTimeout)
{
	async_timer *timer;
	async_context *context;
	
	zend_bool cancelled;

	ZEND_PARSE_PARAMETERS_NONE();

	timer = (async_timer *)Z_OBJ_P(getThis());

	if (Z_TYPE_P(&timer->error) != IS_UNDEF) {
		Z_ADDREF_P(&timer->error);

		execute_data->opline--;
		zend_throw_exception_internal(&timer->error);
		execute_data->opline++;

		return;
	}

	timer->new_running = 1;

	if (!timer->ref_count && !timer->unref_count) {
		if (timer->running) {
			if (timer->enable.active) {
				ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
				timer->enable.active = 0;

				ASYNC_DELREF(&timer->std);
			}
		} else if (!timer->enable.active) {
			ASYNC_ADDREF(&timer->std);

			async_task_scheduler_enqueue_enable(timer->scheduler, &timer->enable);
		}
	}

	context = async_context_get();

	ASYNC_REF_ENTER(context, timer);
	async_task_suspend(&timer->timeouts, NULL, execute_data, &cancelled);
	ASYNC_REF_EXIT(context, timer);
	
	if (timer->scheduler->disposed && Z_TYPE_P(&timer->error) == IS_UNDEF) {
		ZVAL_COPY(&timer->error, &timer->scheduler->error);
	
		if (timer->enable.active) {
			ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
			timer->enable.active = 0;
		} else {
			ASYNC_ADDREF(&timer->std);
		}
		
		uv_close((uv_handle_t *) &timer->handle, close_timer);
	}

	if (!timer->ref_count && !timer->unref_count && Z_TYPE_P(&timer->error) == IS_UNDEF) {
		timer->new_running = 0;

		if (timer->running) {
			if (!timer->enable.active) {
				ASYNC_ADDREF(&timer->std);

				async_task_scheduler_enqueue_enable(timer->scheduler, &timer->enable);
			}
		} else if (timer->enable.active) {
			ASYNC_Q_DETACH(&timer->scheduler->enable, &timer->enable);
			timer->enable.active = 0;

			ASYNC_DELREF(&timer->std);
		}
	}
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_timer_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_timer_await_timeout, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_timer_functions[] = {
	ZEND_ME(Timer, __construct, arginfo_timer_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, close, arginfo_timer_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, awaitTimeout, arginfo_timer_await_timeout, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static void sleep_cb(uv_timer_t *timer)
{
	async_awaitable_queue *q;

	q = (async_awaitable_queue *) timer->data;

	async_awaitable_trigger_continuation(q, NULL, 1);
}

static void sleep_free_cb(uv_handle_t *handle)
{
	efree(handle);
}

static PHP_FUNCTION(asyncsleep)
{
	async_task_scheduler *scheduler;
	async_awaitable_queue *q;
	zend_long num;
	zend_bool cancelled;

	uv_timer_t *timer;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(num)
	ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	if (num < 0) {
		php_error_docref(NULL, E_WARNING, "Number of seconds must be greater than or equal to 0");
		RETURN_FALSE;
	}

	scheduler = async_task_scheduler_get();
	q = async_awaitable_queue_alloc(scheduler);

	timer = emalloc(sizeof(uv_timer_t));
	timer->data = q;

	uv_timer_init(&scheduler->loop, timer);
	uv_timer_start(timer, sleep_cb, num * 1000, 0);

	if (async_context_get()->background) {
		uv_unref((uv_handle_t *) timer);
	}

	async_task_suspend(q, NULL, execute_data, &cancelled);

	async_awaitable_queue_dispose(q);

	uv_close((uv_handle_t *) timer, sleep_free_cb);

#ifdef PHP_SLEEP_NON_VOID
	if (EXPECTED(!cancelled && EG(exception) == NULL)) {
		RETURN_LONG(num);
	}
#endif
}


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
	async_timer_handlers.dtor_obj = async_timer_object_dtor;
	async_timer_handlers.clone_obj = NULL;
}

void async_timer_init()
{
	orig_sleep = (zend_function *) zend_hash_str_find_ptr(EG(function_table), ZEND_STRL("sleep"));
	orig_sleep_handler = orig_sleep->internal_function.handler;

	orig_sleep->internal_function.handler = PHP_FN(asyncsleep);
}

void async_timer_shutdown()
{
	orig_sleep->internal_function.handler = orig_sleep_handler;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
