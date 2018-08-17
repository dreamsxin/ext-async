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

zend_class_entry *async_signal_watcher_ce;

static zend_object_handlers async_signal_watcher_handlers;

#define ASYNC_SIGNAL_WATCHER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_signal_watcher_ce, const_name, sizeof(const_name)-1, (zend_long)value);


static void trigger_signal(uv_signal_t *handle, int signum)
{
	async_signal_watcher *watcher;

	zval result;

	watcher = (async_signal_watcher *) handle->data;

	ZEND_ASSERT(watcher != NULL);

	ZVAL_NULL(&result);

	async_awaitable_trigger_continuation(&watcher->observers, &result, 1);
}

static void enable_signal(void *obj)
{
	async_signal_watcher *watcher;

	watcher = (async_signal_watcher *) obj;

	ZEND_ASSERT(watcher != NULL);

	watcher->running = watcher->new_running;

	if (watcher->running) {
		uv_signal_start(&watcher->signal, trigger_signal, watcher->signum);
	} else {
		uv_signal_stop(&watcher->signal);
	}

	OBJ_RELEASE(&watcher->std);
}

static void close_signal(uv_handle_t *handle)
{
	async_signal_watcher *watcher;

	watcher = (async_signal_watcher *) handle->data;

	ZEND_ASSERT(watcher != NULL);

	OBJ_RELEASE(&watcher->scheduler->std);
	OBJ_RELEASE(&watcher->std);
}

static inline void suspend(async_signal_watcher *watcher, zval *return_value, zend_execute_data *execute_data)
{
	async_context *context;
	zend_bool cancelled;

	context = async_context_get();

	if (context->background) {
		watcher->unref_count++;

		if (watcher->unref_count == 1 && !watcher->ref_count) {
			uv_unref((uv_handle_t *) &watcher->signal);
		}
	} else {
		watcher->ref_count++;

		if (watcher->ref_count == 1 && watcher->unref_count) {
			uv_ref((uv_handle_t *) &watcher->signal);
		}
	}

	async_task_suspend(&watcher->observers, NULL, execute_data, &cancelled);

	if (context->background) {
		watcher->unref_count--;

		if (watcher->unref_count == 0 && watcher->ref_count) {
			uv_ref((uv_handle_t *) &watcher->signal);
		}
	} else {
		watcher->ref_count--;

		if (watcher->ref_count == 0 && watcher->unref_count) {
			uv_unref((uv_handle_t *) &watcher->signal);
		}
	}
}


static zend_object *async_signal_watcher_object_create(zend_class_entry *ce)
{
	async_signal_watcher *watcher;

	watcher = emalloc(sizeof(async_signal_watcher));
	ZEND_SECURE_ZERO(watcher, sizeof(async_signal_watcher));

	zend_object_std_init(&watcher->std, ce);
	watcher->std.handlers = &async_signal_watcher_handlers;

	ZVAL_UNDEF(&watcher->error);

	watcher->enable.object = watcher;
	watcher->enable.func = enable_signal;

	return &watcher->std;
}

static void async_signal_watcher_object_dtor(zend_object *object)
{
	async_signal_watcher *watcher;

	watcher = (async_signal_watcher *) object;

	if (watcher->enable.active) {
		ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
		watcher->enable.active = 0;
	}

	if (!uv_is_closing((uv_handle_t *) &watcher->signal)) {
		GC_ADDREF(&watcher->std);

		uv_close((uv_handle_t *) &watcher->signal, close_signal);
	}
}

static void async_signal_watcher_object_destroy(zend_object *object)
{
	async_signal_watcher *watcher;

	watcher = (async_signal_watcher *) object;

	zval_ptr_dtor(&watcher->error);

	zend_object_std_dtor(&watcher->std);
}

ZEND_METHOD(SignalWatcher, __construct)
{
	async_signal_watcher *watcher;
	zend_long signum;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
	    Z_PARAM_LONG(signum)
	ZEND_PARSE_PARAMETERS_END();

	ASYNC_CHECK_ERROR(!async_cli, "Signal watchers require PHP running in CLI mode");

	watcher = (async_signal_watcher *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(signum < 1, "Invalid signal number: %d", (int) signum);

	watcher->signum = (int) signum;
	watcher->scheduler = async_task_scheduler_get();

	GC_ADDREF(&watcher->scheduler->std);

	uv_signal_init(&watcher->scheduler->loop, &watcher->signal);

	watcher->signal.data = watcher;
}

ZEND_METHOD(SignalWatcher, close)
{
	async_signal_watcher *watcher;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	watcher = (async_signal_watcher *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&watcher->error) != IS_UNDEF) {
		return;
	}

	if (watcher->enable.active) {
		ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
		watcher->enable.active = 0;
	} else {
		GC_ADDREF(&watcher->std);
	}

	if (!uv_is_closing((uv_handle_t *) &watcher->signal)) {
		uv_close((uv_handle_t *) &watcher->signal, close_signal);
	}

	zend_throw_error(NULL, "Signal watcher has been closed");

	ZVAL_OBJ(&watcher->error, EG(exception));
	EG(exception) = NULL;

	if (val != NULL && Z_TYPE_P(val) != IS_NULL) {
		zend_exception_set_previous(Z_OBJ_P(&watcher->error), Z_OBJ_P(val));
		GC_ADDREF(Z_OBJ_P(val));
	}

	async_awaitable_trigger_continuation(&watcher->observers, &watcher->error, 0);
}

ZEND_METHOD(SignalWatcher, awaitSignal)
{
	async_signal_watcher *watcher;

	ZEND_PARSE_PARAMETERS_NONE();

	watcher = (async_signal_watcher *) Z_OBJ_P(getThis());

	if (Z_TYPE_P(&watcher->error) != IS_UNDEF) {
		Z_ADDREF_P(&watcher->error);

		execute_data->opline--;
		zend_throw_exception_internal(&watcher->error);
		execute_data->opline++;

		return;
	}

	watcher->new_running = 1;

	if (!watcher->ref_count && !watcher->unref_count) {
		if (watcher->running) {
			if (watcher->enable.active) {
				ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
				watcher->enable.active = 0;

				OBJ_RELEASE(&watcher->std);
			}
		} else if (!watcher->enable.active) {
			GC_ADDREF(&watcher->std);

			async_task_scheduler_enqueue_enable(watcher->scheduler, &watcher->enable);
		}
	}

	suspend(watcher, return_value, execute_data);

	if (!watcher->ref_count && !watcher->unref_count && Z_TYPE_P(&watcher->error) == IS_UNDEF) {
		watcher->new_running = 0;

		if (watcher->running) {
			if (!watcher->enable.active) {
				GC_ADDREF(&watcher->std);

				async_task_scheduler_enqueue_enable(watcher->scheduler, &watcher->enable);
			}
		} else if (watcher->enable.active) {
			ASYNC_Q_DETACH(&watcher->scheduler->enable, &watcher->enable);
			watcher->enable.active = 0;

			OBJ_RELEASE(&watcher->std);
		}
	}
}

ZEND_METHOD(SignalWatcher, isSupported)
{
	zend_long tmp;
	int signum;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_LONG(tmp)
	ZEND_PARSE_PARAMETERS_END();

	if (!async_cli) {
		RETURN_FALSE;
	}

	signum = (int) tmp;

	if (signum < 1) {
		RETURN_FALSE;
	}

	if (signum == ASYNC_SIGNAL_SIGHUP) {
		RETURN_TRUE;
	}

	if (signum == ASYNC_SIGNAL_SIGINT) {
		RETURN_TRUE;
	}

	if (signum == ASYNC_SIGNAL_SIGQUIT) {
		RETURN_TRUE;
	}

	if (signum == ASYNC_SIGNAL_SIGKILL) {
		RETURN_TRUE;
	}

	if (signum == ASYNC_SIGNAL_SIGTERM) {
		RETURN_TRUE;
	}

	if (signum == ASYNC_SIGNAL_SIGUSR1) {
		RETURN_TRUE;
	}
	if (signum == ASYNC_SIGNAL_SIGUSR2) {
		RETURN_TRUE;
	}

	RETURN_FALSE;
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_signal_watcher_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, signum, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_signal_watcher_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_signal_watcher_await_signal, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_signal_watcher_is_supported, 0, 1, _IS_BOOL, 0)
	ZEND_ARG_TYPE_INFO(0, signum, IS_LONG, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry async_signal_watcher_functions[] = {
	ZEND_ME(SignalWatcher, __construct, arginfo_signal_watcher_ctor, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(SignalWatcher, close, arginfo_signal_watcher_close, ZEND_ACC_PUBLIC)
	ZEND_ME(SignalWatcher, awaitSignal, arginfo_signal_watcher_await_signal, ZEND_ACC_PUBLIC)
	ZEND_ME(SignalWatcher, isSupported, arginfo_signal_watcher_is_supported, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_FE_END
};


void async_signal_watcher_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\SignalWatcher", async_signal_watcher_functions);
	async_signal_watcher_ce = zend_register_internal_class(&ce);
	async_signal_watcher_ce->ce_flags |= ZEND_ACC_FINAL;
	async_signal_watcher_ce->create_object = async_signal_watcher_object_create;
	async_signal_watcher_ce->serialize = zend_class_serialize_deny;
	async_signal_watcher_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_signal_watcher_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_signal_watcher_handlers.free_obj = async_signal_watcher_object_destroy;
	async_signal_watcher_handlers.dtor_obj = async_signal_watcher_object_dtor;
	async_signal_watcher_handlers.clone_obj = NULL;

	ASYNC_SIGNAL_WATCHER_CONST("SIGHUP", ASYNC_SIGNAL_SIGHUP);
	ASYNC_SIGNAL_WATCHER_CONST("SIGINT", ASYNC_SIGNAL_SIGINT);
	ASYNC_SIGNAL_WATCHER_CONST("SIGQUIT", ASYNC_SIGNAL_SIGQUIT);
	ASYNC_SIGNAL_WATCHER_CONST("SIGKILL", ASYNC_SIGNAL_SIGKILL);
	ASYNC_SIGNAL_WATCHER_CONST("SIGTERM", ASYNC_SIGNAL_SIGTERM);
	ASYNC_SIGNAL_WATCHER_CONST("SIGUSR1", ASYNC_SIGNAL_SIGUSR1);
	ASYNC_SIGNAL_WATCHER_CONST("SIGUSR2", ASYNC_SIGNAL_SIGUSR2);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
