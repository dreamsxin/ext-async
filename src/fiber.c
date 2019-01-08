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

zend_class_entry *async_fiber_ce;

static zend_object_handlers async_fiber_handlers;

static zend_op_array fiber_run_func;
static zend_try_catch_element fiber_terminate_try_catch_array = { 0, 1, 0, 0 };
static zend_op fiber_run_op[2];

#define ASYNC_FIBER_CONST(const_name, value) \
	zend_declare_class_constant_long(async_fiber_ce, const_name, sizeof(const_name)-1, (zend_long)value);


void async_fiber_init_metadata(async_fiber *fiber, zend_execute_data *call)
{
	if (call != NULL && call->func && ZEND_USER_CODE(call->func->common.type)) {
		if (call->func->op_array.filename != NULL) {
			fiber->file = zend_string_copy(call->func->op_array.filename);
		}

		fiber->line = call->opline->lineno;
	}
}

async_fiber_context async_fiber_context_get()
{
	async_fiber_context *context;
	async_fiber *fiber;

	fiber = ASYNC_G(current_fiber);

	if (fiber != NULL) {
		return fiber->context;
	}

	context = ASYNC_G(active_context);

	if (context != NULL) {
		return context;
	}

	context = ASYNC_G(root);

	if (context == NULL) {
		context = async_fiber_create_root_context();

		ASYNC_CHECK_FATAL(context == NULL, "Failed to create root fiber context");

		ASYNC_G(root) = context;
	}

	return context;
}

void async_fiber_context_start(async_fiber *to, async_context *context, zend_bool yieldable)
{
	async_fiber_context *from;
	async_fiber *fiber;
	async_context *prev;
	async_vm_state state;

	from = async_fiber_context_get();

	fiber = ASYNC_G(current_fiber);
	prev = ASYNC_G(current_context);

	if (fiber == NULL) {
		ASYNC_FIBER_BACKUP_VM_STATE(&state);
	} else {
		ASYNC_FIBER_BACKUP_VM_STATE(&fiber->state);
	}

	ASYNC_G(active_context) = to->context;
	ASYNC_G(current_fiber) = to;
	ASYNC_G(current_context) = context;

	ASYNC_CHECK_FATAL(!async_fiber_switch_context(from, to->context, yieldable), "Failed to switch fiber");

	ASYNC_G(active_context) = from;
	ASYNC_G(current_fiber) = fiber;
	ASYNC_G(current_context) = prev;

	if (fiber == NULL) {
		ASYNC_FIBER_RESTORE_VM_STATE(&state);
	} else {
		ASYNC_FIBER_RESTORE_VM_STATE(&fiber->state);
	}
}

void async_fiber_context_switch(async_fiber_context *to, zend_bool yieldable)
{
	async_fiber_context *current;
	async_fiber *fiber;
	async_context *context;
	async_vm_state state;

	current = async_fiber_context_get();

	fiber = ASYNC_G(current_fiber);
	context = ASYNC_G(current_context);

	if (fiber == NULL) {
		ASYNC_FIBER_BACKUP_VM_STATE(&state);
	} else {
		ASYNC_FIBER_BACKUP_VM_STATE(&fiber->state);
	}

	ASYNC_G(active_context) = to;
	ASYNC_G(current_fiber) = NULL;
	ASYNC_G(current_context) = NULL;

	ASYNC_CHECK_FATAL(!async_fiber_switch_context(current, to, yieldable), "Failed to switch fiber");

	ASYNC_G(active_context) = current;
	ASYNC_G(current_fiber) = fiber;
	ASYNC_G(current_context) = context;

	if (fiber == NULL) {
		ASYNC_FIBER_RESTORE_VM_STATE(&state);
	} else {
		ASYNC_FIBER_RESTORE_VM_STATE(&fiber->state);
	}
}

void async_fiber_context_yield()
{
	async_fiber_context *current;
	async_fiber *fiber;
	async_context *context;

	fiber = ASYNC_G(current_fiber);

	ZEND_ASSERT(fiber != NULL);

	current = async_fiber_context_get();
	context = ASYNC_G(current_context);

	ASYNC_FIBER_BACKUP_VM_STATE(&fiber->state);
	ASYNC_CHECK_FATAL(!async_fiber_yield(current), "Failed to yield from fiber");
	ASYNC_FIBER_RESTORE_VM_STATE(&fiber->state);

	ASYNC_G(active_context) = current;
	ASYNC_G(current_fiber) = fiber;
	ASYNC_G(current_context) = context;
}


void async_fiber_run()
{
	async_fiber *fiber;

	fiber = ASYNC_G(current_fiber);
	ZEND_ASSERT(fiber != NULL);

	EG(vm_stack) = fiber->state.stack;
	EG(vm_stack_top) = fiber->state.stack->top;
	EG(vm_stack_end) = fiber->state.stack->end;
	EG(vm_stack_page_size) = ASYNC_FIBER_VM_STACK_SIZE;

	fiber->state.exec = (zend_execute_data *) EG(vm_stack_top);
	EG(vm_stack_top) = (zval *) fiber->state.exec + ZEND_CALL_FRAME_SLOT;
	zend_vm_init_call_frame(fiber->state.exec, ZEND_CALL_TOP_FUNCTION, (zend_function *) &fiber_run_func, 0, NULL, NULL);
	fiber->state.exec->opline = fiber_run_op;
	fiber->state.exec->call = NULL;
	fiber->state.exec->return_value = NULL;
	fiber->state.exec->prev_execute_data = NULL;

	EG(current_execute_data) = fiber->state.exec;

	execute_ex(fiber->state.exec);

	zend_vm_stack_destroy();
	fiber->state.stack = NULL;
	fiber->state.exec = NULL;

	async_fiber_context_yield();
}


static int fiber_run_opcode_handler(zend_execute_data *exec)
{
	async_fiber *fiber;

	zval retval;

	fiber = ASYNC_G(current_fiber);
	ZEND_ASSERT(fiber != NULL);

	fiber->status = ASYNC_FIBER_STATUS_RUNNING;

	if (fiber->func != NULL) {
		fiber->func(fiber);
	} else {
		fiber->fci.retval = &retval;

		if (zend_call_function(&fiber->fci, &fiber->fcc) == SUCCESS) {
			if (fiber->value != NULL && !EG(exception)) {
				ZVAL_ZVAL(fiber->value, &retval, 0, 1);
			}
		}

		if (EG(exception)) {
			if (fiber->disposed) {
				zend_clear_exception();
			}

			fiber->status = ASYNC_FIBER_STATUS_FAILED;
		} else {
			fiber->status = ASYNC_FIBER_STATUS_FINISHED;
		}

		fiber->value = NULL;

		zval_ptr_dtor(&fiber->fci.function_name);
	}

	return ZEND_USER_OPCODE_RETURN;
}


static zend_object *async_fiber_object_create(zend_class_entry *ce)
{
	async_fiber *fiber;

	fiber = emalloc(sizeof(async_fiber));
	ZEND_SECURE_ZERO(fiber, sizeof(async_fiber));

	zend_object_std_init(&fiber->std, ce);
	fiber->std.handlers = &async_fiber_handlers;

	return &fiber->std;
}

static void async_fiber_object_destroy(zend_object *object)
{
	async_fiber *fiber;

	fiber = (async_fiber *) object;

	if (fiber->status == ASYNC_FIBER_STATUS_SUSPENDED) {
		fiber->disposed = 1;
		fiber->status = ASYNC_FIBER_STATUS_RUNNING;

		async_fiber_context_switch(fiber->context, 1);
	}

	if (fiber->status == ASYNC_FIBER_STATUS_INIT && fiber->func == NULL) {
		zval_ptr_dtor(&fiber->fci.function_name);
	}

	async_fiber_destroy(fiber->context);

	if (fiber->file != NULL) {
		zend_string_release(fiber->file);
	}

	zend_object_std_dtor(&fiber->std);
}

static zval *read_fiber_prop(zval *object, zval *member, int type, void **cache_slot, zval *rv)
{
	async_fiber *fiber;
	
	fiber = (async_fiber *) Z_OBJ_P(object);
	
	if (strcmp(Z_STRVAL_P(member), "status") == 0) {
		ZVAL_LONG(rv, fiber->status);
	} else {
		rv = &EG(uninitialized_zval);
	}
	
	return rv;
}

static int has_fiber_prop(zval *object, zval *member, int has_set_exists, void **cache_slot)
{
	async_fiber *fiber;
	
	zval val;
	
	fiber = (async_fiber *) Z_OBJ_P(object);
	
	if (strcmp(Z_STRVAL_P(member), "status") != 0) {
		return 0;
	}
	
	switch (has_set_exists) {
    	case ZEND_PROPERTY_EXISTS:
    	case ZEND_PROPERTY_ISSET:
    		return 1;
    }
    
    ZVAL_LONG(&val, fiber->status);
    
    convert_to_boolean(&val);
    
    return (Z_TYPE_P(&val) == IS_TRUE) ? 1 : 0;
}

ZEND_METHOD(Fiber, __construct)
{
	async_fiber *fiber;
	zend_long stack_size;

	fiber = (async_fiber *) Z_OBJ_P(getThis());
	stack_size = ASYNC_G(stack_size);

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(fiber->fci, fiber->fcc, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(stack_size)
	ZEND_PARSE_PARAMETERS_END();

	async_fiber_init_metadata(fiber, EX(prev_execute_data));

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	fiber->status = ASYNC_FIBER_STATUS_INIT;
	fiber->state.stack_page_size = stack_size;

	// Keep a reference to closures or callable objects as long as the fiber lives.
	Z_TRY_ADDREF_P(&fiber->fci.function_name);
}

ZEND_METHOD(Fiber, __debugInfo)
{
	async_fiber *fiber;

	ZEND_PARSE_PARAMETERS_NONE();

	fiber = (async_fiber *) Z_OBJ_P(getThis());

	if (USED_RET()) {
		array_init(return_value);

		add_assoc_string(return_value, "status", async_status_label(fiber->status));
		add_assoc_bool(return_value, "suspended", fiber->status == ASYNC_FIBER_STATUS_SUSPENDED);
		add_assoc_str(return_value, "file", zend_string_copy(fiber->file));
		add_assoc_long(return_value, "line", fiber->line);
	}
}

ZEND_METHOD(Fiber, start)
{
	async_fiber *fiber;
	uint32_t param_count;

	zval *params;

	ZEND_PARSE_PARAMETERS_START(0, -1)
		Z_PARAM_VARIADIC('+', params, param_count)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (async_fiber *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(fiber->status != ASYNC_FIBER_STATUS_INIT, "Cannot start Fiber that has already been started");

	fiber->fci.params = params;
	fiber->fci.param_count = param_count;
	fiber->fci.no_separation = 1;

	fiber->context = async_fiber_create_context();

	ASYNC_CHECK_ERROR(fiber->context == NULL, "Failed to create native fiber context");
	ASYNC_CHECK_ERROR(!async_fiber_create(fiber->context, async_fiber_run, fiber->state.stack_page_size), "Failed to create native fiber");

	fiber->state.stack = (zend_vm_stack) emalloc(ASYNC_FIBER_VM_STACK_SIZE);
	fiber->state.stack->top = ZEND_VM_STACK_ELEMENTS(fiber->state.stack) + 1;
	fiber->state.stack->end = (zval *) ((char *) fiber->state.stack + ASYNC_FIBER_VM_STACK_SIZE);
	fiber->state.stack->prev = NULL;

	fiber->value = USED_RET() ? return_value : NULL;

	async_fiber_context_start(fiber, ASYNC_G(current_context), 1);
}

ZEND_METHOD(Fiber, resume)
{
	async_fiber *fiber;

	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (async_fiber *) Z_OBJ_P(getThis());

	ASYNC_CHECK_ERROR(fiber->status != ASYNC_FIBER_STATUS_SUSPENDED, "Non-suspended Fiber cannot be resumed");

	if (val != NULL && fiber->value != NULL) {
		ZVAL_COPY(fiber->value, val);
		fiber->value = NULL;
	}

	fiber->status = ASYNC_FIBER_STATUS_RUNNING;
	fiber->value = USED_RET() ? return_value : NULL;

	async_fiber_context_switch(fiber->context, 1);
}

ZEND_METHOD(Fiber, throw)
{
	async_fiber *fiber;

	zval *error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (async_fiber *) Z_OBJ_P(getThis());

	if (fiber->status != ASYNC_FIBER_STATUS_SUSPENDED) {
		zend_throw_exception_object(error);
		return;
	}

	Z_ADDREF_P(error);

	ASYNC_G(error) = error;

	fiber->status = ASYNC_FIBER_STATUS_RUNNING;
	fiber->value = USED_RET() ? return_value : NULL;

	async_fiber_context_switch(fiber->context, 1);
}

ZEND_METHOD(Fiber, isRunning)
{
	async_fiber *fiber;

	ZEND_PARSE_PARAMETERS_NONE();

	fiber = ASYNC_G(current_fiber);

	RETURN_BOOL(fiber != NULL && fiber->type == ASYNC_FIBER_TYPE_DEFAULT);
}

ZEND_METHOD(Fiber, backend)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_STRING(async_fiber_backend_info());
}

ZEND_METHOD(Fiber, yield)
{
	async_fiber *fiber;

	zval *val;
	zval *error;

	fiber = ASYNC_G(current_fiber);

	ASYNC_CHECK_ERROR(fiber == NULL, "Cannot yield from outside a fiber");
	ASYNC_CHECK_ERROR(fiber->type != ASYNC_FIBER_TYPE_DEFAULT, "Cannot yield from an async task");
	ASYNC_CHECK_ERROR(fiber->status != ASYNC_FIBER_STATUS_RUNNING, "Cannot yield from a fiber that is not running");
	ASYNC_CHECK_ERROR(fiber->disposed, "Fiber has been destroyed");

	val = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	if (val != NULL && fiber->value != NULL) {
		ZVAL_COPY(fiber->value, val);
		fiber->value = NULL;
	}

	fiber->status = ASYNC_FIBER_STATUS_SUSPENDED;
	fiber->value = USED_RET() ? return_value : NULL;

	async_fiber_context_yield();

	ASYNC_CHECK_ERROR(fiber->disposed, "Fiber has been destroyed");

	error = ASYNC_G(error);

	if (error != NULL) {
		ASYNC_G(error) = NULL;

		fiber->state.exec->opline--;
		zend_throw_exception_internal(error);
		fiber->state.exec->opline++;
	}
}

ZEND_METHOD(Fiber, __wakeup)
{
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a fiber is not allowed");
}


ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_ctor, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_TYPE_INFO(0, stack_size, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_debug_info, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_start, 0, 0, 1)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_resume, 0, 0, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_throw, 0)
	 ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_void, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_fiber_is_running, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_fiber_backend, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_yield, 0, 0, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

static const zend_function_entry fiber_functions[] = {
	ZEND_ME(Fiber, __construct, arginfo_fiber_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, __debugInfo, arginfo_fiber_debug_info, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, start, arginfo_fiber_start, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, resume, arginfo_fiber_resume, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, throw, arginfo_fiber_throw, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, yield, arginfo_fiber_yield, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Fiber, isRunning, arginfo_fiber_is_running, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Fiber, backend, arginfo_fiber_backend, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Fiber, __wakeup, arginfo_fiber_void, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void async_fiber_ce_register()
{
	zend_class_entry ce;
	zend_uchar opcode = ZEND_VM_LAST_OPCODE + 1;

	/* Create a new user opcode to run fiber. */
	while (1) {
		if (opcode == 255) {
			return;
		} else if (zend_get_user_opcode_handler(opcode) == NULL) {
			break;
		}
		opcode++;
	}

	zend_set_user_opcode_handler(opcode, fiber_run_opcode_handler);

	ZEND_SECURE_ZERO(fiber_run_op, sizeof(fiber_run_op));
	fiber_run_op[0].opcode = opcode;
	zend_vm_set_opcode_handler_ex(fiber_run_op, 0, 0, 0);
	fiber_run_op[1].opcode = opcode;
	zend_vm_set_opcode_handler_ex(fiber_run_op + 1, 0, 0, 0);

	ZEND_SECURE_ZERO(&fiber_run_func, sizeof(fiber_run_func));
	fiber_run_func.type = ZEND_USER_FUNCTION;
	fiber_run_func.function_name = zend_string_init("Concurrent\\Fiber::run", sizeof("Concurrent\\Fiber::run") - 1, 1);
	fiber_run_func.filename = ZSTR_EMPTY_ALLOC();
	fiber_run_func.opcodes = fiber_run_op;
	fiber_run_func.last_try_catch = 1;
	fiber_run_func.try_catch_array = &fiber_terminate_try_catch_array;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Fiber", fiber_functions);
	async_fiber_ce = zend_register_internal_class(&ce);
	async_fiber_ce->ce_flags |= ZEND_ACC_FINAL;
	async_fiber_ce->create_object = async_fiber_object_create;
	async_fiber_ce->serialize = zend_class_serialize_deny;
	async_fiber_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_fiber_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_fiber_handlers.free_obj = async_fiber_object_destroy;
	async_fiber_handlers.clone_obj = NULL;
	async_fiber_handlers.has_property = has_fiber_prop;
	async_fiber_handlers.read_property = read_fiber_prop;

	ASYNC_FIBER_CONST("STATUS_INIT", ASYNC_FIBER_STATUS_INIT);
	ASYNC_FIBER_CONST("STATUS_SUSPENDED", ASYNC_FIBER_STATUS_SUSPENDED);
	ASYNC_FIBER_CONST("STATUS_RUNNING", ASYNC_FIBER_STATUS_RUNNING);
	ASYNC_FIBER_CONST("STATUS_FINISHED", ASYNC_FIBER_STATUS_FINISHED);
	ASYNC_FIBER_CONST("STATUS_FAILED", ASYNC_FIBER_STATUS_FAILED);
}

void async_fiber_ce_unregister()
{
	zend_string_free(fiber_run_func.function_name);
	fiber_run_func.function_name = NULL;
}

void async_fiber_shutdown()
{
	async_fiber_context root;
	root = ASYNC_G(root);

	ASYNC_G(root) = NULL;

	async_fiber_destroy(root);
}
