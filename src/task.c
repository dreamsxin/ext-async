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

#include "async_fiber.h"
#include "async_task.h"

zend_class_entry *async_task_ce;

static zend_object_handlers async_task_handlers;


#define ASYNC_OP_CHECK_ERROR(op, expr, message, ...) do { \
    if (UNEXPECTED(expr)) { \
    	(op)->status = ASYNC_STATUS_FAILED; \
    	ASYNC_PREPARE_ERROR(&(op)->result, message ASYNC_VA_ARGS(__VA_ARGS__)); \
    	return FAILURE; \
    } \
} while (0)

#define ASYNC_TASK_DELEGATE_RESULT(status, result) do { \
	if (status == ASYNC_OP_RESOLVED) { \
		RETURN_ZVAL(result, 1, 0); \
	} else if (status == ASYNC_OP_FAILED) { \
		Z_ADDREF_P(result); \
		execute_data->opline--; \
		zend_throw_exception_internal(result); \
		execute_data->opline++; \
		return; \
	} \
} while (0)

#define ASYNC_TASK_DELEGATE_OP_RESULT(op) do { \
	if ((op)->status == ASYNC_STATUS_RESOLVED) { \
		RETURN_ZVAL(&(op)->result, 1, 1); \
	} \
	if (Z_TYPE_P(&(op)->result) != IS_UNDEF) { \
		EG(current_execute_data)->opline--; \
		zend_throw_exception_internal(&(op)->result); \
		EG(current_execute_data)->opline++; \
		return; \
	} \
	zval_ptr_dtor(&(op)->result); \
	if (UNEXPECTED(EG(exception) == NULL)) { \
		zend_throw_error(NULL, "Awaitable has not been resolved"); \
	} \
	return; \
} while (0)


zval *async_task_get_debug_info(async_task *task, zval *retval)
{
	array_init(retval);
	
	add_assoc_string(retval, "status", async_status_label(task->fiber.status));
	add_assoc_bool(retval, "suspended", task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED);
	add_assoc_str(retval, "file", zend_string_copy(task->fiber.file));
	add_assoc_long(retval, "line", task->fiber.line);

	if (Z_TYPE_P(&task->result) != IS_UNDEF) {
		Z_TRY_ADDREF_P(&task->result);
		
		add_assoc_zval(retval, "result", &task->result);
	}
	
	return retval;
}

static inline void trigger_ops(async_task *task)
{
	async_op *op;
	
	while (task->operations.first != NULL) {
		ASYNC_DEQUEUE_OP(&task->operations, op);
		
		if (task->fiber.status == ASYNC_OP_RESOLVED) {
			ASYNC_RESOLVE_OP(op, &task->result);
		} else {
			ASYNC_FAIL_OP(op, &task->result);
		}
	}
}

static void async_task_fiber_func(async_fiber *fiber)
{
	async_task *task;

	zval retval;
	zval tmp;

	ZEND_ASSERT(fiber->type == ASYNC_FIBER_TYPE_TASK);

	task = (async_task *) fiber;

	fiber->fci.retval = &retval;

	zend_call_function(&fiber->fci, &fiber->fcc);
	zval_ptr_dtor(&fiber->fci.function_name);

	if (EXPECTED(EG(exception) == NULL) && Z_TYPE_P(&retval) == IS_OBJECT) {
		if (instanceof_function(Z_OBJCE_P(&retval), async_awaitable_ce) != 0) {
			tmp = retval;
			
			zend_call_method_with_1_params(NULL, async_task_ce, NULL, "await", &retval, &tmp);
			
			zval_ptr_dtor(&tmp);
		}
	}

	if (EG(exception)) {
		fiber->status = ASYNC_FIBER_STATUS_FAILED;

		ZVAL_OBJ(&task->result, EG(exception));
		EG(exception) = NULL;
	} else {
		fiber->status = ASYNC_FIBER_STATUS_FINISHED;

		ZVAL_COPY(&task->result, &retval);
	}
	
	trigger_ops(task);

	zval_ptr_dtor(&retval);

	zend_clear_exception();
}

void async_task_start(async_task *task)
{
	task->operation = ASYNC_TASK_OPERATION_NONE;
	task->fiber.context = async_fiber_create_context();

	ASYNC_CHECK_FATAL(task->fiber.context == NULL, "Failed to create native fiber context");
	ASYNC_CHECK_FATAL(!async_fiber_create(task->fiber.context, async_fiber_run, task->fiber.state.stack_page_size), "Failed to create native fiber");
	
	task->fiber.state.stack = (zend_vm_stack) emalloc(ASYNC_FIBER_VM_STACK_SIZE);
	task->fiber.state.stack->top = ZEND_VM_STACK_ELEMENTS(task->fiber.state.stack) + 1;
	task->fiber.state.stack->end = (zval *) ((char *) task->fiber.state.stack + ASYNC_FIBER_VM_STACK_SIZE);
	task->fiber.state.stack->prev = NULL;

	task->fiber.status = ASYNC_FIBER_STATUS_RUNNING;
	task->fiber.func = async_task_fiber_func;

	async_fiber_context_start(&task->fiber, task->context, 1);

	zend_fcall_info_args_clear(&task->fiber.fci, 1);
}

void async_task_continue(async_task *task)
{
	task->operation = ASYNC_TASK_OPERATION_NONE;
	task->fiber.status = ASYNC_FIBER_STATUS_RUNNING;

	async_fiber_context_switch(task->fiber.context, 1);
}

static inline void async_task_execute_inline(async_task *task, async_task *inner)
{
	async_context *context;

	inner->operation = ASYNC_TASK_OPERATION_NONE;

	async_task_scheduler_dequeue(inner);

	// Mark inner fiber as suspended to avoid duplicate inlining attempts.
	inner->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;

	ASYNC_DELREF(&inner->fiber.std);

	context = ASYNC_G(current_context);
	ASYNC_G(current_context) = inner->context;

	inner->fiber.fci.retval = &inner->result;

	zend_call_function(&inner->fiber.fci, &inner->fiber.fcc);

	zval_ptr_dtor(&inner->fiber.fci.function_name);
	zend_fcall_info_args_clear(&inner->fiber.fci, 1);

	ASYNC_G(current_context) = context;

	if (UNEXPECTED(EG(exception))) {
		inner->fiber.status = ASYNC_FIBER_STATUS_FAILED;

		ZVAL_OBJ(&inner->result, EG(exception));
		EG(exception) = NULL;
	} else {
		inner->fiber.status = ASYNC_FIBER_STATUS_FINISHED;
	}
	
	trigger_ops(inner);
}

/* Continue fiber-based task execution. */
static void continue_op_task(async_op *op)
{
	async_task *task;
	zend_bool flag;
	
	task = (async_task *) op->arg;
	
	if (op->flags & ASYNC_OP_FLAG_CANCELLED) {
		return;
	}

	if (op->flags & ASYNC_OP_FLAG_DEFER) {
		async_task_scheduler_enqueue(task);
	} else {
		flag = (task->scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_NOWAIT) ? 1 : 0;

		if (flag) {
			task->scheduler->flags &= ~ASYNC_TASK_SCHEDULER_FLAG_NOWAIT;
		}

		async_task_continue(task);
		
		if (flag) {
			task->scheduler->flags |= ASYNC_TASK_SCHEDULER_FLAG_NOWAIT;
		}

		if (task->fiber.status == ASYNC_OP_RESOLVED || task->fiber.status == ASYNC_OP_FAILED) {
			async_task_dispose(task);
			
			ASYNC_DELREF(&task->fiber.std);
		}
	}
}

/* Continue root task execution. */
static void continue_op_root(async_op *op)
{
	async_task_scheduler *scheduler;
	zend_bool flag;
	
	scheduler = (async_task_scheduler *) op->arg;
	
	if (op->flags & ASYNC_OP_FLAG_CANCELLED) {
		return;
	}
	
	scheduler->current = ASYNC_G(active_context);

	flag = (scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_NOWAIT) ? 1 : 0;

	if (flag) {
		scheduler->flags &= ~ASYNC_TASK_SCHEDULER_FLAG_NOWAIT;
	}

	async_fiber_context_switch(scheduler->caller, 0);

	if (flag) {
		scheduler->flags |= ASYNC_TASK_SCHEDULER_FLAG_NOWAIT;
	}

	scheduler->current = NULL;
}

/* Continue fiber-based task due to cancellation of an async operation. */
static void cancel_op(void *obj, zval *error)
{
	async_op *op;
	async_task *task;
	
	op = (async_op *) obj;
	task = (async_task *) op->arg;
	
	if (op->status != ASYNC_STATUS_RUNNING) {
		return;
	}
	
	ZEND_ASSERT(task != NULL);
	
	ZVAL_COPY(&op->result, error);
	
	op->status = ASYNC_STATUS_FAILED;
	op->flags |= ASYNC_OP_FLAG_CANCELLED;
	op->cancel.object = NULL;
	op->cancel.func = NULL;

	if (op->flags & ASYNC_OP_FLAG_DEFER) {
		async_task_scheduler_enqueue(task);
	} else {
		async_task_continue(task);
		
		if (task->fiber.status == ASYNC_OP_RESOLVED || task->fiber.status == ASYNC_OP_FAILED) {
			async_task_dispose(task);
			
			ASYNC_DELREF(&task->fiber.std);
		}
	}
}

/* Suspend the current execution until the async operation has been resolved or cancelled. */
int async_await_op(async_op *op)
{
	async_fiber *fiber;
	async_task *task;
	async_task_scheduler *scheduler;
	async_context *context;

	ZEND_ASSERT(op->status == ASYNC_OP_PENDING);

	fiber = ASYNC_G(current_fiber);
	
	ZVAL_NULL(&op->result);
	
	if (fiber == NULL) {
		scheduler = async_task_scheduler_get();

		ASYNC_OP_CHECK_ERROR(op, scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED, "Cannot await after the task scheduler has been disposed");
		ASYNC_OP_CHECK_ERROR(op, scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_RUNNING, "Cannot await in the fiber that is running the task scheduler loop");
		
		op->status = ASYNC_STATUS_RUNNING;
		op->callback = continue_op_root;
		op->arg = scheduler;
		
		if (op->q == NULL) {
			ASYNC_ENQUEUE_OP(&scheduler->operations, op);
		}
		
		context = ASYNC_G(current_context);
		
		async_task_scheduler_run_loop(scheduler);
		
		ASYNC_G(current_context) = context;
	} else {
		ASYNC_OP_CHECK_ERROR(op, fiber->type != ASYNC_FIBER_TYPE_TASK, "Await must be called from within a running task");
		ASYNC_OP_CHECK_ERROR(op, fiber->status != ASYNC_FIBER_STATUS_RUNNING, "Cannot await in a task that is not running");
		ASYNC_OP_CHECK_ERROR(op, fiber->disposed, "Task has been destroyed");
	
		task = (async_task *) fiber;
	
		ASYNC_OP_CHECK_ERROR(op, task->scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED, "Cannot await after the task scheduler has been disposed");
		
		context = ASYNC_G(current_context);
		
		if (context->cancel != NULL) {
			if (Z_TYPE_P(&context->cancel->error) != IS_UNDEF) {
				op->status = ASYNC_STATUS_FAILED;
				
				ZVAL_COPY(&op->result, &context->cancel->error);
				
				return FAILURE;
			}
			
			op->cancel.object = op;
			op->cancel.func = cancel_op;
		
			ASYNC_Q_ENQUEUE(&context->cancel->callbacks, &op->cancel);
			ASYNC_ADDREF(&context->std);
		}
		
		op->status = ASYNC_STATUS_RUNNING;
		op->callback = continue_op_task;
		op->arg = task;
		
		if (op->q == NULL) {
			ASYNC_ENQUEUE_OP(&task->scheduler->operations, op);
		}
		
		task->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;
	
		async_fiber_context_yield();
		
		if (context->cancel != NULL) {
			ASYNC_DELREF(&context->std);
		}

		if (op->cancel.object != NULL) {
			ASYNC_Q_DETACH(&context->cancel->callbacks, &op->cancel);
			ASYNC_DELREF(&context->std);
			
			op->cancel.object = NULL;
			op->cancel.func = NULL;
		}
	}
	
	if (op->status == ASYNC_STATUS_PENDING) {
		if (op->q != NULL) {
			ASYNC_Q_DETACH(op->q, op);
		}
		
		ASYNC_PREPARE_ERROR(&op->result, "Awaitable has not been resolved");
		
		return FAILURE;
	}
	
	return (op->status == ASYNC_STATUS_RESOLVED) ? SUCCESS : FAILURE;
}

async_task *async_task_object_create(zend_execute_data *call, async_task_scheduler *scheduler, async_context *context)
{
	async_task *task;
	zend_long stack_size;

	ZEND_ASSERT(scheduler != NULL);
	ZEND_ASSERT(context != NULL);

	task = emalloc(sizeof(async_task));
	ZEND_SECURE_ZERO(task, sizeof(async_task));

	task->fiber.type = ASYNC_FIBER_TYPE_TASK;
	task->fiber.status = ASYNC_FIBER_STATUS_INIT;

	stack_size = ASYNC_G(stack_size);

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	task->fiber.state.stack_page_size = stack_size;

	ZVAL_NULL(&task->result);
	ZVAL_UNDEF(&task->error);

	task->scheduler = scheduler;
	task->context = context;

	ASYNC_ADDREF(&context->std);

	zend_object_std_init(&task->fiber.std, async_task_ce);
	task->fiber.std.handlers = &async_task_handlers;

	async_fiber_init_metadata(&task->fiber, call);

	return task;
}

void async_task_dispose(async_task *task)
{
	task->operation = ASYNC_TASK_OPERATION_NONE;

	if (task->fiber.status == ASYNC_FIBER_STATUS_SUSPENDED) {
		task->fiber.disposed = 1;

		async_task_continue(task);
	}

	if (task->fiber.status == ASYNC_FIBER_STATUS_INIT) {
		zend_fcall_info_args_clear(&task->fiber.fci, 1);
		zval_ptr_dtor(&task->fiber.fci.function_name);
		
		trigger_ops(task);
	}
}

static void async_task_object_destroy(zend_object *object)
{
	async_task *task;

	task = (async_task *) object;

	async_fiber_destroy(task->fiber.context);

	if (task->fiber.file != NULL) {
		zend_string_release(task->fiber.file);
	}

	zval_ptr_dtor(&task->result);
	zval_ptr_dtor(&task->error);

	ASYNC_DELREF(&task->context->std);

	zend_object_std_dtor(&task->fiber.std);
}

ZEND_METHOD(Task, __construct)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Tasks must not be constructed by userland code");
}

ZEND_METHOD(Task, __debugInfo)
{
	ZEND_PARSE_PARAMETERS_NONE();
	
	if (USED_RET()) {
		async_task_get_debug_info((async_task *) Z_OBJ_P(getThis()), return_value);
	}
}

ZEND_METHOD(Task, isRunning)
{
	async_fiber *fiber;

	ZEND_PARSE_PARAMETERS_NONE();

	fiber = ASYNC_G(current_fiber);

	RETURN_BOOL(fiber != NULL && fiber->type == ASYNC_FIBER_TYPE_TASK);
}

ZEND_METHOD(Task, async)
{
	async_task *task;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t count;
	uint32_t i;

	zval *params;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, -1)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	for (i = 1; i <= count; i++) {
		ASYNC_CHECK_ERROR(ARG_SHOULD_BE_SENT_BY_REF(fcc.function_handler, i), "Cannot pass async call argument %d by reference", (int) i);
	}

	fci.no_separation = 1;

	if (count == 0) {
		fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&fci, count, params);
	}

	Z_TRY_ADDREF_P(&fci.function_name);

	task = async_task_object_create(EX(prev_execute_data), async_task_scheduler_get(), async_context_get());
	task->fiber.fci = fci;
	task->fiber.fcc = fcc;

	async_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->fiber.std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, asyncWithContext)
{
	async_task *task;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	uint32_t count;
	uint32_t i;

	zval *ctx;
	zval *params;
	zval obj;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 2, -1)
		Z_PARAM_ZVAL(ctx)
		Z_PARAM_FUNC_EX(fci, fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_VARIADIC('+', params, count)
	ZEND_PARSE_PARAMETERS_END();

	for (i = 1; i <= count; i++) {
		ASYNC_CHECK_ERROR(ARG_SHOULD_BE_SENT_BY_REF(fcc.function_handler, i), "Cannot pass async call argument %d by reference", (int) i);
	}

	fci.no_separation = 1;

	if (count == 0) {
		fci.param_count = 0;
	} else {
		zend_fcall_info_argp(&fci, count, params);
	}

	Z_TRY_ADDREF_P(&fci.function_name);

	task = async_task_object_create(EX(prev_execute_data), async_task_scheduler_get(), (async_context *) Z_OBJ_P(ctx));
	task->fiber.fci = fci;
	task->fiber.fcc = fcc;

	async_task_scheduler_enqueue(task);

	ZVAL_OBJ(&obj, &task->fiber.std);

	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Task, await)
{
	zend_class_entry *ce;
	async_fiber *fiber;
	async_task *task;
	async_task *inner;
	async_task_scheduler *scheduler;
	async_deferred_state *state;
	async_context *context;

	zend_bool busy;
	zval *val;
	zval error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	fiber = ASYNC_G(current_fiber);
	
	ce = (Z_TYPE_P(val) == IS_OBJECT) ? Z_OBJCE_P(val) : NULL;
	busy = 0;

	// Check for root level await.
	if (fiber == NULL) {
		scheduler = async_task_scheduler_get();
		context = async_context_get();

		ASYNC_CHECK_ERROR(scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED, "Cannot await after the task scheduler has been disposed");
		ASYNC_CHECK_ERROR(scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_RUNNING, "Cannot await in the fiber that is running the task scheduler loop");
		ASYNC_CHECK_ERROR(scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_NOWAIT, "Cannot await within the current execution");

		if (ce == async_deferred_awaitable_ce) {
			state = ((async_deferred_awaitable *) Z_OBJ_P(val))->state;

			ASYNC_TASK_DELEGATE_RESULT(state->status, &state->result);
			
			scheduler->op.status = ASYNC_STATUS_RUNNING;
			scheduler->op.flags = 0;
			scheduler->op.callback = continue_op_root;
			scheduler->op.arg = scheduler;
			
			ASYNC_ENQUEUE_OP(&state->operations, &scheduler->op);
			
			if (!context->background && state->context->background) {
				ASYNC_BUSY_ENTER(scheduler);
				busy = 1;
			}
		} else {
			inner = (async_task *) Z_OBJ_P(val);

			ASYNC_CHECK_ERROR(inner->scheduler != scheduler, "Cannot await a task that runs on a different task scheduler");

			ASYNC_TASK_DELEGATE_RESULT(inner->fiber.status, &inner->result);
			
			scheduler->op.status = ASYNC_STATUS_RUNNING;
			scheduler->op.flags = 0;
			scheduler->op.callback = continue_op_root;
			scheduler->op.arg = scheduler;
			
			ASYNC_ENQUEUE_OP(&inner->operations, &scheduler->op);
			
			if (!context->background && inner->context->background) {
				ASYNC_BUSY_ENTER(scheduler);
				busy = 1;
			}
		}
		
		async_task_scheduler_run_loop(scheduler);
		
		if (busy) {
			ASYNC_BUSY_EXIT(scheduler);
		}
		
		ASYNC_TASK_DELEGATE_OP_RESULT(&scheduler->op);
	}

	ASYNC_CHECK_ERROR(fiber->type != ASYNC_FIBER_TYPE_TASK, "Await must be called from within a running task");
	ASYNC_CHECK_ERROR(fiber->status != ASYNC_FIBER_STATUS_RUNNING, "Cannot await in a task that is not running");
	ASYNC_CHECK_ERROR(fiber->disposed, "Task has been destroyed");

	task = (async_task *) fiber;

	ASYNC_CHECK_ERROR(task->scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_DISPOSED, "Cannot await after the task scheduler has been disposed");
	ASYNC_CHECK_ERROR(task->scheduler->flags & ASYNC_TASK_SCHEDULER_FLAG_NOWAIT, "Cannot await within the current execution");

	if (ce == async_task_ce) {
		inner = (async_task *) Z_OBJ_P(val);

		ASYNC_CHECK_ERROR(inner->scheduler != task->scheduler, "Cannot await a task that runs on a different task scheduler");

		// Perform task-inlining optimization where applicable.
		if (inner->fiber.status == ASYNC_FIBER_STATUS_INIT && inner->fiber.state.stack_page_size <= task->fiber.state.stack_page_size) {
			async_task_execute_inline(task, inner);
		}

		ASYNC_TASK_DELEGATE_RESULT(inner->fiber.status, &inner->result);
		
		task->op.status = ASYNC_STATUS_RUNNING;
		task->op.flags = 0;
		task->op.callback = continue_op_task;
		task->op.arg = task;
		
		ASYNC_ENQUEUE_OP(&inner->operations, &task->op);
		
		if (!task->context->background && inner->context->background) {
			ASYNC_BUSY_ENTER(task->scheduler);
			busy = 1;
		}
	} else {
		state = ((async_deferred_awaitable *) Z_OBJ_P(val))->state;

		ASYNC_TASK_DELEGATE_RESULT(state->status, &state->result);
					
		task->op.status = ASYNC_STATUS_RUNNING;
		task->op.flags = 0;
		task->op.callback = continue_op_task;
		task->op.arg = task;

		ASYNC_ENQUEUE_OP(&state->operations, &task->op);
		
		if (!task->context->background && state->context->background) {
			ASYNC_BUSY_ENTER(task->scheduler);
			busy = 1;
		}
	}

	task->fiber.value = USED_RET() ? return_value : NULL;
	task->fiber.status = ASYNC_FIBER_STATUS_SUSPENDED;
	
	async_fiber_context_yield();
	
	if (busy) {
		ASYNC_BUSY_EXIT(task->scheduler);
	}
	
	// Re-throw error provided by task scheduler.
	if (Z_TYPE_P(&task->error) != IS_UNDEF) {
		zval_ptr_dtor(&task->op.result);
	
		error = task->error;
		ZVAL_UNDEF(&task->error);

		execute_data->opline--;
		zend_throw_exception_internal(&error);
		execute_data->opline++;

		return;
	}
	
	ASYNC_TASK_DELEGATE_OP_RESULT(&task->op);
}

ZEND_METHOD(Task, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a task is not allowed");
}

ZEND_BEGIN_ARG_INFO(arginfo_task_ctor, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_debug_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_task_is_running, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_task_async, 0, 1, Concurrent\\Task, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_task_async_with_context, 0, 2, Concurrent\\Task, 0)
	ZEND_ARG_OBJ_INFO(0, context, Concurrent\\Context, 0)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_task_await, 0, 0, 1)
	ZEND_ARG_OBJ_INFO(0, awaitable, Concurrent\\Awaitable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_task_wakeup, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry task_functions[] = {
	ZEND_ME(Task, __construct, arginfo_task_ctor, ZEND_ACC_PRIVATE)
	ZEND_ME(Task, __debugInfo, arginfo_task_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(Task, isRunning, arginfo_task_is_running, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, async, arginfo_task_async, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, asyncWithContext, arginfo_task_async_with_context, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, await, arginfo_task_await, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Task, __wakeup, arginfo_task_wakeup, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_task_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Task", task_functions);
	async_task_ce = zend_register_internal_class(&ce);
	async_task_ce->ce_flags |= ZEND_ACC_FINAL;
	async_task_ce->serialize = zend_class_serialize_deny;
	async_task_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_task_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_task_handlers.free_obj = async_task_object_destroy;
	async_task_handlers.clone_obj = NULL;

	zend_class_implements(async_task_ce, 1, async_awaitable_ce);
}

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
