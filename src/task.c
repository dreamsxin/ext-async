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

ZEND_DECLARE_MODULE_GLOBALS(task)

zend_class_entry *concurrent_task_ce;
zend_class_entry *concurrent_task_continuation_ce;

static zend_object_handlers concurrent_task_handlers;
static zend_object_handlers concurrent_task_continuation_handlers;

static zend_object *concurrent_task_continuation_object_create(concurrent_task *task);


void concurrent_task_start(concurrent_task *task)
{
	task->operation = CONCURRENT_TASK_OPERATION_NONE;
	task->fiber = concurrent_fiber_create_context();

	if (task->fiber == NULL) {
		zend_throw_error(NULL, "Failed to create native fiber context");
		return;
	}

	if (!concurrent_fiber_create(task->fiber, concurrent_fiber_run, task->stack_size)) {
		zend_throw_error(NULL, "Failed to create native fiber");
		return;
	}

	task->stack = (zend_vm_stack) emalloc(CONCURRENT_FIBER_VM_STACK_SIZE);
	task->stack->top = ZEND_VM_STACK_ELEMENTS(task->stack) + 1;
	task->stack->end = (zval *) ((char *) task->stack + CONCURRENT_FIBER_VM_STACK_SIZE);
	task->stack->prev = NULL;

	task->status = CONCURRENT_FIBER_STATUS_RUNNING;

	if (!concurrent_fiber_switch_to((concurrent_fiber *) task)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}

	zend_fcall_info_args_clear(&task->fci, 1);
}

void concurrent_task_continue(concurrent_task *task)
{
	task->operation = CONCURRENT_TASK_OPERATION_NONE;
	task->status = CONCURRENT_FIBER_STATUS_RUNNING;

	if (!concurrent_fiber_switch_to((concurrent_fiber *) task)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}
}

void concurrent_task_notify_success(concurrent_task *task, zval *result)
{
	concurrent_task_continuation_cb *cont;

	zval args[2];
	zval retval;

	ZVAL_COPY(&task->result, result);

	while (task->continuation != NULL) {
		cont = task->continuation;
		task->continuation = cont->next;

		ZVAL_NULL(&args[0]);
		ZVAL_COPY(&args[1], &task->result);

		cont->fci.param_count = 2;
		cont->fci.params = args;
		cont->fci.retval = &retval;

		zend_call_function(&cont->fci, &cont->fcc);

		zval_ptr_dtor(args);
		zval_ptr_dtor(&retval);
		zval_ptr_dtor(&cont->fci.function_name);

		efree(cont);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(task->context);
		}
	}
}

void concurrent_task_notify_failure(concurrent_task *task, zval *error)
{
	concurrent_task_continuation_cb *cont;

	zval args[1];
	zval retval;

	ZVAL_COPY(&task->result, error);

	while (task->continuation != NULL) {
		cont = task->continuation;
		task->continuation = cont->next;

		ZVAL_COPY(&args[0], &task->result);

		cont->fci.param_count = 1;
		cont->fci.params = args;
		cont->fci.retval = &retval;

		zend_call_function(&cont->fci, &cont->fcc);

		zval_ptr_dtor(args);
		zval_ptr_dtor(&retval);
		zval_ptr_dtor(&cont->fci.function_name);

		efree(cont);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(task->context);
		}
	}
}

static void concurrent_task_dispose(concurrent_task *task)
{
	concurrent_task_scheduler *prev;

	zval result;

	prev = TASK_G(scheduler);
	TASK_G(scheduler) = NULL;

	ZVAL_NULL(&result);

	task->status = CONCURRENT_FIBER_STATUS_DEAD;
	task->value = &result;

	concurrent_fiber_switch_to((concurrent_fiber *) task);

	TASK_G(scheduler) = prev;

	if (UNEXPECTED(EG(exception))) {
		zval_ptr_dtor(&result);
		ZVAL_OBJ(&result, EG(exception));

		EG(exception) = NULL;

		concurrent_task_notify_failure(task, &result);

		zval_ptr_dtor(&result);
	} else {
		concurrent_task_notify_success(task, &result);
	}
}


concurrent_task *concurrent_task_object_create()
{
	concurrent_task *task;
	zend_long stack_size;

	task = emalloc(sizeof(concurrent_task));
	ZEND_SECURE_ZERO(task, sizeof(concurrent_task));

	task->status = CONCURRENT_FIBER_STATUS_INIT;

	task->id = TASK_G(counter) + 1;
	TASK_G(counter) = task->id;

	stack_size = TASK_G(stack_size);

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	task->stack_size = stack_size;

	ZVAL_NULL(&task->result);
	ZVAL_UNDEF(&task->error);

	zend_object_std_init(&task->std, concurrent_task_ce);
	task->std.handlers = &concurrent_task_handlers;

	return task;
}

static void concurrent_task_object_destroy(zend_object *object)
{
	concurrent_task *task;
	concurrent_task_scheduler *prev;
	concurrent_task_continuation_cb *cont;

	task = (concurrent_task *) object;

	if (task->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		prev = TASK_G(scheduler);
		TASK_G(scheduler) = NULL;

		task->status = CONCURRENT_FIBER_STATUS_DEAD;

		concurrent_fiber_switch_to((concurrent_fiber *) task);

		TASK_G(scheduler) = prev;
	}

	if (task->status == CONCURRENT_FIBER_STATUS_INIT) {
		zend_fcall_info_args_clear(&task->fci, 1);

		zval_ptr_dtor(&task->fci.function_name);
	}

	zval_ptr_dtor(&task->result);
	zval_ptr_dtor(&task->error);

	while (task->continuation != NULL) {
		cont = task->continuation;
		task->continuation = cont->next;

		zval_ptr_dtor(&cont->fci.function_name);

		efree(cont);
	}

	OBJ_RELEASE(&task->context->std);

	concurrent_fiber_destroy(task->fiber);

	zend_object_std_dtor(&task->std);
}

ZEND_METHOD(Task, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Tasks must not be constructed by userland code");
}

ZEND_METHOD(Task, continueWith)
{
	concurrent_task *task;
	concurrent_task_continuation_cb *cont;
	concurrent_task_continuation_cb *tmp;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval args[2];
	zval result;

	task = (concurrent_task *) Z_OBJ_P(getThis());

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
	ZEND_PARSE_PARAMETERS_END();

	fci.no_separation = 1;

	if (task->status == CONCURRENT_FIBER_STATUS_FINISHED) {
		ZVAL_NULL(&args[0]);
		ZVAL_COPY(&args[1], &task->result);

		fci.param_count = 2;
		fci.params = args;
		fci.retval = &result;

		zend_call_function(&fci, &fcc);
		zval_ptr_dtor(args);
		zval_ptr_dtor(&result);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(task->context);
		}

		return;
	}

	if (task->status == CONCURRENT_FIBER_STATUS_DEAD) {
		ZVAL_COPY(&args[0], &task->result);

		fci.param_count = 1;
		fci.params = args;
		fci.retval = &result;

		zend_call_function(&fci, &fcc);
		zval_ptr_dtor(args);
		zval_ptr_dtor(&result);

		if (UNEXPECTED(EG(exception))) {
			concurrent_context_delegate_error(task->context);
		}

		return;
	}

	cont = emalloc(sizeof(concurrent_task_continuation_cb));
	cont->fci = fci;
	cont->fcc = fcc;
	cont->next = NULL;

	if (task->continuation == NULL) {
		task->continuation = cont;
	} else {
		tmp = task->continuation;

		while (tmp->next != NULL) {
			tmp = tmp->next;
		}

		tmp->next = cont;
	}

	Z_TRY_ADDREF_P(&fci.function_name);
}

ZEND_METHOD(Task, isRunning)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(TASK_G(current_fiber) != NULL && TASK_G(scheduler) != NULL);
}

ZEND_METHOD(Task, async)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task * task;

	zval *params;
	zval obj;

	scheduler = TASK_G(scheduler);

	if (UNEXPECTED(scheduler == NULL)) {
		zend_throw_error(NULL, "Cannot create an async task while no task scheduler is running");
		return;
	}

	task = concurrent_task_object_create();
	task->scheduler = scheduler;

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(task->fci, task->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->fci.no_separation = 1;

	if (params == NULL) {
		task->fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fci, params);
	}

	Z_TRY_ADDREF_P(&task->fci.function_name);

	task->context = scheduler->context;
	GC_ADDREF(&task->context->std);

	concurrent_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, asyncWithContext)
{
	concurrent_task_scheduler *scheduler;
	concurrent_task * task;

	zval *ctx;
	zval *params;
	zval obj;

	scheduler = TASK_G(scheduler);

	if (UNEXPECTED(scheduler == NULL)) {
		zend_throw_error(NULL, "Cannot create an async task while no task scheduler is running");
		return;
	}

	task = concurrent_task_object_create();
	task->scheduler = scheduler;

	params = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, 3)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(task->fci, task->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(params)
	ZEND_PARSE_PARAMETERS_END();

	task->fci.no_separation = 1;

	if (params == NULL) {
		task->fci.param_count = 0;
	} else {
		zend_fcall_info_args(&task->fci, params);
	}

	Z_TRY_ADDREF_P(&task->fci.function_name);

	task->context = (concurrent_context *) Z_OBJ_P(ctx);
	GC_ADDREF(&task->context->std);

	concurrent_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, await)
{
	zend_class_entry *ce;
	concurrent_task *task;
	concurrent_task *inner;
	concurrent_context *context;
	zend_execute_data *exec;
	size_t stack_page_size;

	zval *val;
	zval retval;
	zval cont;
	zval error;
	zval *value;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val);
	ZEND_PARSE_PARAMETERS_END();

	task = (concurrent_task *) TASK_G(current_fiber);

	if (UNEXPECTED(task == NULL)) {
		zend_throw_error(NULL, "Await must be called from within a running task");
		return;
	}

	if (UNEXPECTED(task->status != CONCURRENT_FIBER_STATUS_RUNNING)) {
		zend_throw_error(NULL, "Cannot await in a task that is not running");
		return;
	}

	ZVAL_NULL(&retval);
	ZVAL_UNDEF(&error);

	if (Z_TYPE_P(val) != IS_OBJECT) {
		RETURN_ZVAL(val, 1, 0);
	}

	ce = Z_OBJCE_P(val);

	// Optimized handling of awaited tasks.
	if (ce == concurrent_task_ce) {
		inner = (concurrent_task *) Z_OBJ_P(val);

		// Task-inlining optimization, avoids allocating a native fiber and stack for the awaited task.
		if (inner->status == CONCURRENT_FIBER_STATUS_INIT && inner->stack_size == task->stack_size) {
			if (inner->scheduler == task->scheduler) {
				inner->operation = CONCURRENT_TASK_OPERATION_NONE;

				context = task->context;
				task->context = inner->context;

				inner->fci.retval = &retval;

				zend_call_function(&inner->fci, &inner->fci_cache);

				zval_ptr_dtor(&inner->fci.function_name);
				zend_fcall_info_args_clear(&inner->fci, 1);

				task->context = context;

				if (UNEXPECTED(EG(exception))) {
					inner->status = CONCURRENT_FIBER_STATUS_DEAD;

					ZVAL_OBJ(&error, EG(exception));
					EG(exception) = NULL;

					concurrent_task_notify_failure(inner, &error);

					zval_ptr_dtor(&error);
				} else {
					inner->status = CONCURRENT_FIBER_STATUS_FINISHED;

					concurrent_task_notify_success(inner, &retval);
				}

				zval_ptr_dtor(&retval);
			}
		}

		if (inner->status == CONCURRENT_FIBER_STATUS_FINISHED) {
			RETURN_ZVAL(&inner->result, 1, 0);
		}

		if (inner->status == CONCURRENT_FIBER_STATUS_DEAD) {
			exec = EG(current_execute_data);

			exec->opline--;
			zend_throw_exception_internal(&inner->result);
			exec->opline++;

			return;
		}
	}

	// Attempt to adapt non-awaitable objects to awaitables.
	if (instanceof_function_ex(ce, concurrent_awaitable_ce, 1) != 1) {
		if (task->scheduler->adapter) {
			task->scheduler->adapter_fci.param_count = 1;
			task->scheduler->adapter_fci.params = val;
			task->scheduler->adapter_fci.retval = &retval;

			zend_call_function(&task->scheduler->adapter_fci, &task->scheduler->adapter_fcc);
			zval_ptr_dtor(val);

			ZVAL_COPY(val, &retval);
			zval_ptr_dtor(&retval);

			if (Z_TYPE_P(val) != IS_OBJECT) {
				RETURN_ZVAL(val, 1, 0);
			}

			ce = Z_OBJCE_P(val);
		}

		if (instanceof_function_ex(ce, concurrent_awaitable_ce, 1) != 1) {
			RETURN_ZVAL(val, 1, 0);
		}
	}

	value = task->value;
	task->value = USED_RET() ? return_value : NULL;

	ZVAL_OBJ(&cont, concurrent_task_continuation_object_create(task));
	zend_call_method_with_1_params(val, NULL, NULL, "continueWith", NULL, &cont);
	zval_ptr_dtor(&cont);

	// Resume without fiber context switch if the awaitable is already resolved.
	if (task->operation == CONCURRENT_TASK_OPERATION_RESUME) {
		task->operation = CONCURRENT_TASK_OPERATION_NONE;
		task->value = value;

		return;
	}

	task->status = CONCURRENT_FIBER_STATUS_SUSPENDED;

	CONCURRENT_FIBER_BACKUP_EG(task->stack, stack_page_size, task->exec);
	concurrent_fiber_yield(task->fiber);
	CONCURRENT_FIBER_RESTORE_EG(task->stack, stack_page_size, task->exec);

	if (task->status == CONCURRENT_FIBER_STATUS_DEAD) {
		zend_throw_error(NULL, "Task has been destroyed");
		return;
	}

	task->value = value;

	if (Z_TYPE_P(&task->error) != IS_UNDEF) {
		error = task->error;
		ZVAL_UNDEF(&task->error);

		exec = EG(current_execute_data);

		exec->opline--;
		zend_throw_exception_internal(&error);
		exec->opline++;
	}
}

ZEND_METHOD(Task, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_continue_with, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, continuation, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_task_is_running, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_async, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, arguments, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_async_with_context, 0, 0, 2)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_ARRAY_INFO(0, arguments, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_await, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_functions[] = {
	ZEND_ME(Task, __construct, arginfo_task_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(Task, continueWith, arginfo_task_continue_with, ZEND_ACC_PUBLIC)
	ZEND_ME(Task, isRunning, arginfo_task_is_running, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, async, arginfo_task_async, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, asyncWithContext, arginfo_task_async_with_context, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, await, arginfo_task_await, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, __wakeup, arginfo_task_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *concurrent_task_continuation_object_create(concurrent_task *task)
{
	concurrent_task_continuation *cont;

	cont = emalloc(sizeof(concurrent_task_continuation));
	ZEND_SECURE_ZERO(cont, sizeof(concurrent_task_continuation));

	cont->task = task;

	GC_ADDREF(&task->std);

	zend_object_std_init(&cont->std, concurrent_task_continuation_ce);
	cont->std.handlers = &concurrent_task_continuation_handlers;

	return &cont->std;
}

static void concurrent_task_continuation_object_destroy(zend_object *object)
{
	concurrent_task_continuation *cont;

	cont = (concurrent_task_continuation *) object;

	if (cont->task != NULL) {
		if (cont->task->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
			concurrent_task_dispose(cont->task);
		}

		OBJ_RELEASE(&cont->task->std);
	}

	zend_object_std_dtor(&cont->std);
}

ZEND_METHOD(TaskContinuation, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Task continuations must not be constructed from userland code");
}

ZEND_METHOD(TaskContinuation, __invoke)
{
	concurrent_task_continuation *cont;
	concurrent_task *task;

	zval *error;
	zval *val;

	cont = (concurrent_task_continuation *) Z_OBJ_P(getThis());

	if (cont->task == NULL) {
		return;
	}

	error = NULL;
	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_ZVAL(error)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	task = cont->task;
	cont->task = NULL;

	if (error == NULL || Z_TYPE_P(error) == IS_NULL) {
		if (task->value != NULL) {
			ZVAL_COPY(task->value, val);
		}
	} else {
		ZVAL_COPY(&task->error, error);
	}

	task->value = NULL;

	if (task->status != CONCURRENT_FIBER_STATUS_RUNNING) {
		concurrent_task_scheduler_enqueue(task);
	} else {
		task->operation = CONCURRENT_TASK_OPERATION_RESUME;
	}

	OBJ_RELEASE(&task->std);
}

ZEND_METHOD(TaskContinuation, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task continuation is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_continuation_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_continuation_invoke, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_continuation_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_continuation_functions[] = {
	ZEND_ME(TaskContinuation, __construct, arginfo_task_continuation_ctor, ZEND_ACC_PRIVATE | ZEND_ACC_CTOR)
	ZEND_ME(TaskContinuation, __invoke, arginfo_task_continuation_invoke, ZEND_ACC_PUBLIC)
	ZEND_ME(TaskContinuation, __wakeup, arginfo_task_continuation_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void concurrent_task_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Task", task_functions);
	concurrent_task_ce = zend_register_internal_class(&ce);
	concurrent_task_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_ce->serialize = zend_class_serialize_deny;
	concurrent_task_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_task_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_handlers.free_obj = concurrent_task_object_destroy;
	concurrent_task_handlers.clone_obj = NULL;

	zend_class_implements(concurrent_task_ce, 1, concurrent_awaitable_ce);

	INIT_CLASS_ENTRY(ce, "Concurrent\\TaskContinuation", task_continuation_functions);
	concurrent_task_continuation_ce = zend_register_internal_class(&ce);
	concurrent_task_continuation_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_task_continuation_ce->serialize = zend_class_serialize_deny;
	concurrent_task_continuation_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_task_continuation_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_task_continuation_handlers.free_obj = concurrent_task_continuation_object_destroy;
	concurrent_task_continuation_handlers.clone_obj = NULL;
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
