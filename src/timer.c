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
#include "zend_inheritance.h"

ASYNC_API zend_class_entry *async_timeout_exception_ce;
ASYNC_API zend_class_entry *async_timer_ce;

static zend_object_handlers async_timer_handlers;

static zend_function *orig_sleep;
static zif_handler orig_sleep_handler;

typedef struct {
	/* PHP object handle. */
	zend_object std;

	/* Error being set as the watcher was closed (undef by default). */
	zval error;

	/* Timer interval in milliseconds. */
	uint64_t delay;

	/* UV timer handle. */
	uv_timer_t handle;

	/* Queued timeout continuations. */
	async_op_queue timeouts;

	/* Number of pending referenced timeout subscriptions. */
	zend_uchar ref_count;

	async_task_scheduler *scheduler;

	async_cancel_cb cancel;
} async_timer;

typedef struct {
	async_deferred_custom_awaitable base;
	uv_timer_t timer;
} async_timeout_awaitable;

static void trigger_timer(uv_timer_t *handle)
{
	async_timer *timer;
	async_op *op;
	async_op *last;
	zend_bool cont;

	timer = (async_timer *) handle->data;

	ZEND_ASSERT(timer != NULL);
	
	if (Z_TYPE_P(&timer->error) == IS_UNDEF) {	
		last = timer->timeouts.last;
		cont = 1;
	
		while (cont && timer->timeouts.first != NULL) {
			ASYNC_DEQUEUE_OP(&timer->timeouts, op);
			
			cont = (op != last);
			
			ASYNC_FINISH_OP(op);
		}
	} else {
		while (timer->timeouts.first != NULL) {
			ASYNC_DEQUEUE_OP(&timer->timeouts, op);
			ASYNC_FAIL_OP(op, &timer->error);
		}
	}
	
	if (timer->timeouts.first == NULL) {
		if (!uv_is_closing((uv_handle_t *) handle)) {
			uv_timer_stop(handle);
		}
	}
}

static void close_timer(uv_handle_t *handle)
{
	async_timer *timer;

	timer = (async_timer *) handle->data;

	ZEND_ASSERT(timer != NULL);

	ASYNC_DELREF(&timer->std);
}

static void shutdown_timer(void *obj, zval *error)
{
	async_timer *timer;
	async_op *op;

	timer = (async_timer *) obj;

	ZEND_ASSERT(timer != NULL);

	timer->cancel.func = NULL;

	if (error != NULL && Z_TYPE_P(&timer->error) == IS_UNDEF) {
		ZVAL_COPY(&timer->error, error);
	}

	if (!uv_is_closing((uv_handle_t *) &timer->handle)) {
		ASYNC_ADDREF(&timer->std);

		uv_close((uv_handle_t *) &timer->handle, close_timer);
	}
	
	if (error != NULL) {
		while (timer->timeouts.first != NULL) {
			ASYNC_DEQUEUE_OP(&timer->timeouts, op);
			ASYNC_FAIL_OP(op, &timer->error);
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

	ZVAL_UNDEF(&timer->error);
	
	timer->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&timer->scheduler->std);

	uv_timer_init(&timer->scheduler->loop, &timer->handle);
	uv_unref((uv_handle_t *) &timer->handle);

	timer->handle.data = timer;

	timer->cancel.object = timer;
	timer->cancel.func = shutdown_timer;

	ASYNC_Q_ENQUEUE(&timer->scheduler->shutdown, &timer->cancel);

	return &timer->std;
}

static void async_timer_object_dtor(zend_object *object)
{
	async_timer *timer;

	timer = (async_timer *) object;

	if (timer->cancel.func != NULL) {
		ASYNC_Q_DETACH(&timer->scheduler->shutdown, &timer->cancel);
		
		timer->cancel.func(timer, NULL);
	}
}

static void async_timer_object_destroy(zend_object *object)
{
	async_timer *timer;

	timer = (async_timer *) object;

	zval_ptr_dtor(&timer->error);
	
	ASYNC_DELREF(&timer->scheduler->std);
	
	zend_object_std_dtor(&timer->std);
}

ZEND_METHOD(Timer, __construct)
{
	async_timer *timer;
	uint64_t delay;

	zval *a;

	timer = (async_timer *) Z_OBJ_P(getThis());

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

	zval error;
	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	timer = (async_timer *) Z_OBJ_P(getThis());

	if (timer->cancel.func == NULL) {
		return;
	}
	
	ASYNC_PREPARE_ERROR(&error, "Timer has been closed");
	
	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}
	
	ASYNC_Q_DETACH(&timer->scheduler->shutdown, &timer->cancel);
	
	timer->cancel.func(timer, &error);
	
	zval_ptr_dtor(&error);
}

ZEND_METHOD(Timer, awaitTimeout)
{
	async_timer *timer;
	async_op *op;
	async_context *context;

	ZEND_PARSE_PARAMETERS_NONE();

	timer = (async_timer *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&timer->error) != IS_UNDEF) {
		Z_ADDREF_P(&timer->error);

		execute_data->opline--;
		zend_throw_exception_internal(&timer->error);
		execute_data->opline++;

		return;
	}
	
	if (timer->timeouts.first == NULL && !uv_is_active((uv_handle_t *) &timer->handle)) {
		uv_timer_start(&timer->handle, trigger_timer, timer->delay, timer->delay);
	}
	
	context = async_context_get();
	
	ASYNC_ALLOC_OP(op);
	ASYNC_ENQUEUE_OP(&timer->timeouts, op);

	ASYNC_UNREF_ENTER(context, timer);

	if (async_await_op(op) == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
	}

	ASYNC_UNREF_EXIT(context, timer);
	ASYNC_FREE_OP(op);
}

static void timeout_close_cb(uv_handle_t *handle)
{
	async_deferred_awaitable *awaitable;
	
	awaitable = (async_deferred_awaitable *) handle->data;
	
	ZEND_ASSERT(awaitable != NULL);
	
	ASYNC_DELREF(&awaitable->std);
}

static void timeout_cb(uv_timer_t *timer)
{
	async_deferred_awaitable *awaitable;
	
	zval error;
	
	awaitable = timer->data;
	
	ZEND_ASSERT(awaitable != NULL);
	
	ASYNC_ADDREF(&awaitable->std);
	
	ASYNC_PREPARE_EXCEPTION(&error, async_timeout_exception_ce, "Operation timed out");
	
	async_fail_awaitable(awaitable, &error);
	zval_ptr_dtor(&error);
	
	uv_close((uv_handle_t *) timer, timeout_close_cb);
}

static void timeout_dispose_cb(async_deferred_awaitable *obj)
{
	async_timeout_awaitable *awaitable;
	
	awaitable = (async_timeout_awaitable *) obj;
	
	if (uv_is_active((uv_handle_t *) &awaitable->timer)) {
		uv_timer_stop(&awaitable->timer);
	}
	
	if (!uv_is_closing((uv_handle_t *) &awaitable->timer)) {
		ASYNC_ADDREF(&awaitable->base.base.std);
		
		uv_close((uv_handle_t *) &awaitable->timer, timeout_close_cb);
	}
}

ZEND_METHOD(Timer, timeout)
{
	async_timeout_awaitable *awaitable;
	async_context *context;
	
	zval *a;
	zval obj;
	
	uint64_t delay;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(a)
	ZEND_PARSE_PARAMETERS_END();
	
	delay = (uint64_t) Z_LVAL_P(a);

	ASYNC_CHECK_ERROR(delay < 0, "Delay must not be shorter than 0 milliseconds");
	
	awaitable = ecalloc(1, sizeof(async_timeout_awaitable));
	context = async_context_get();
	
	async_init_awaitable((async_deferred_custom_awaitable *) awaitable, timeout_dispose_cb, context);
	
	uv_timer_init(&awaitable->base.base.state->scheduler->loop, &awaitable->timer);
	
	awaitable->timer.data = awaitable;
	
	if (context->background) {
		uv_unref((uv_handle_t *) &awaitable->timer);
	}
	
	uv_timer_start(&awaitable->timer, timeout_cb, delay, 0);
	
	ZVAL_OBJ(&obj, &awaitable->base.base.std);
	
	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_timer_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_timer_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_timer_await_timeout, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_timer_timeout, 0, 1, Concurrent\\Awaitable, 0)
	ZEND_ARG_TYPE_INFO(0, milliseconds, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_timer_functions[] = {
	ZEND_ME(Timer, __construct, arginfo_timer_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, close, arginfo_timer_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, awaitTimeout, arginfo_timer_await_timeout, ZEND_ACC_PUBLIC)
	ZEND_ME(Timer, timeout, arginfo_timer_timeout, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


static void sleep_cb(uv_timer_t *timer)
{
	async_op *op;

	op = (async_op *) timer->data;
	
	ZEND_ASSERT(op != NULL);

	ASYNC_FINISH_OP(op);
}

static void sleep_free_cb(uv_handle_t *handle)
{
	async_task_scheduler *scheduler;
	
	scheduler = (async_task_scheduler *) handle->data;
	
	ZEND_ASSERT(scheduler != NULL);
	
	ASYNC_DELREF(&scheduler->std);
	
	efree(handle);
}

static PHP_FUNCTION(asyncsleep)
{
	async_task_scheduler *scheduler;
	async_op *op;
	zend_long num;

	uv_timer_t *timer;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(num)
	ZEND_PARSE_PARAMETERS_END_EX(RETURN_FALSE);

	if (num < 0) {
		php_error_docref(NULL, E_WARNING, "Number of seconds must be greater than or equal to 0");
		RETURN_FALSE;
	}

	scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&scheduler->std);

	ASYNC_ALLOC_CUSTOM_OP(op, sizeof(async_op));
	ASYNC_ENQUEUE_OP(&scheduler->operations, op);

	timer = emalloc(sizeof(uv_timer_t));
	timer->data = op;

	uv_timer_init(&scheduler->loop, timer);
	uv_timer_start(timer, sleep_cb, num * 1000, 0);

	if (async_context_get()->background) {
		uv_unref((uv_handle_t *) timer);
	}
	
	if (async_await_op(op) == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(op);
	}

	ASYNC_FREE_OP(op);
	
	timer->data = scheduler;

	uv_close((uv_handle_t *) timer, sleep_free_cb);

#ifdef PHP_SLEEP_NON_VOID
	if (EXPECTED(EG(exception) == NULL)) {
		RETURN_LONG(num);
	}
#endif
}


static const zend_function_entry empty_funcs[] = {
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
	async_timer_handlers.dtor_obj = async_timer_object_dtor;
	async_timer_handlers.clone_obj = NULL;
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\TimeoutException", empty_funcs);
	async_timeout_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_timeout_exception_ce, zend_ce_exception);
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
