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
#include "zend_vm.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "zend_closures.h"

#include "php_task.h"
#include "fiber.h"

ZEND_DECLARE_MODULE_GLOBALS(task)

zend_class_entry *concurrent_fiber_ce;

static zend_object_handlers concurrent_fiber_handlers;

static zend_op_array fiber_run_func;
static zend_try_catch_element fiber_terminate_try_catch_array = { 0, 1, 0, 0 };
static zend_op fiber_run_op[2];

zend_bool concurrent_fiber_switch_to(concurrent_fiber *fiber)
{
	concurrent_fiber_context root;
	concurrent_fiber *prev;
	zend_bool result;
	zend_execute_data *exec;
	zend_vm_stack stack;
	size_t stack_page_size;

	root = TASK_G(root);

	if (root == NULL) {
		root = concurrent_fiber_create_root_context();

		if (root == NULL) {
			return 0;
		}

		TASK_G(root) = root;
	}

	CONCURRENT_FIBER_BACKUP_EG(stack, stack_page_size, exec);

	prev = TASK_G(current_fiber);
	TASK_G(current_fiber) = fiber;

	result = concurrent_fiber_switch_context((prev == NULL) ? root : prev->context, fiber->context);

	TASK_G(current_fiber) = prev;

	CONCURRENT_FIBER_RESTORE_EG(stack, stack_page_size, exec);

	return result;
}


void concurrent_fiber_run()
{
	concurrent_fiber *fiber;

	fiber = TASK_G(current_fiber);
	ZEND_ASSERT(fiber != NULL);

	EG(vm_stack) = fiber->stack;
	EG(vm_stack_top) = fiber->stack->top;
	EG(vm_stack_end) = fiber->stack->end;
	EG(vm_stack_page_size) = CONCURRENT_FIBER_VM_STACK_SIZE;

	fiber->exec = (zend_execute_data *) EG(vm_stack_top);
	EG(vm_stack_top) = (zval *) fiber->exec + ZEND_CALL_FRAME_SLOT;
	zend_vm_init_call_frame(fiber->exec, ZEND_CALL_TOP_FUNCTION, (zend_function *) &fiber_run_func, 0, NULL, NULL);
	fiber->exec->opline = fiber_run_op;
	fiber->exec->call = NULL;
	fiber->exec->return_value = NULL;
	fiber->exec->prev_execute_data = NULL;

	EG(current_execute_data) = fiber->exec;

	execute_ex(fiber->exec);

	fiber->value = NULL;

	if (fiber->fci.object) {
		GC_DELREF(fiber->fci.object);
	}

	zval_ptr_dtor(&fiber->fci.function_name);

	zend_vm_stack_destroy();
	fiber->stack = NULL;
	fiber->exec = NULL;

	concurrent_fiber_yield(fiber->context);

	abort();
}


static int fiber_run_opcode_handler(zend_execute_data *exec)
{
	concurrent_fiber *fiber;
	zval retval;

	fiber = TASK_G(current_fiber);
	ZEND_ASSERT(fiber != NULL);

	fiber->status = CONCURRENT_FIBER_STATUS_RUNNING;
	fiber->fci.retval = &retval;

	if (zend_call_function(&fiber->fci, &fiber->fci_cache) == SUCCESS) {
		if (fiber->value != NULL && !EG(exception)) {
			ZVAL_ZVAL(fiber->value, &retval, 0, 1);
		}
	}

	if (EG(exception)) {
		if (fiber->status == CONCURRENT_FIBER_STATUS_DEAD) {
			zend_clear_exception();
		} else {
			fiber->status = CONCURRENT_FIBER_STATUS_DEAD;
		}
	} else {
		fiber->status = CONCURRENT_FIBER_STATUS_FINISHED;
	}

	return ZEND_USER_OPCODE_RETURN;
}


static zend_object *concurrent_fiber_object_create(zend_class_entry *ce)
{
	concurrent_fiber *fiber;

	fiber = emalloc(sizeof(concurrent_fiber));
	memset(fiber, 0, sizeof(concurrent_fiber));

	zend_object_std_init(&fiber->std, ce);
	fiber->std.handlers = &concurrent_fiber_handlers;

	return &fiber->std;
}


static void concurrent_fiber_object_destroy(zend_object *object)
{
	concurrent_fiber *fiber;

	fiber = (concurrent_fiber *) object;

	if (fiber->status == CONCURRENT_FIBER_STATUS_SUSPENDED) {
		fiber->status = CONCURRENT_FIBER_STATUS_DEAD;

		concurrent_fiber_switch_to(fiber);
	}

	if (fiber->status == CONCURRENT_FIBER_STATUS_INIT) {
		if (fiber->fci.object) {
			GC_DELREF(fiber->fci.object);
		}

		zval_ptr_dtor(&fiber->fci.function_name);
	}

	concurrent_fiber_destroy(fiber->context);

	zend_object_std_dtor(&fiber->std);

	efree(fiber);
}


/* {{{ proto Fiber::__construct(callable $callback, int stack_size) */
ZEND_METHOD(Fiber, __construct)
{
	concurrent_fiber *fiber;
	zend_long stack_size;

	fiber = (concurrent_fiber *) Z_OBJ_P(getThis());
	stack_size = TASK_G(stack_size);

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 2)
		Z_PARAM_FUNC_EX(fiber->fci, fiber->fci_cache, 1, 0)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(stack_size)
	ZEND_PARSE_PARAMETERS_END();

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	fiber->status = CONCURRENT_FIBER_STATUS_INIT;
	fiber->stack_size = stack_size;
	fiber->fci.object = fiber->fci_cache.object;

	// Keep a reference to closures or callable objects as long as the fiber lives.
	Z_TRY_ADDREF_P(&fiber->fci.function_name);

	if (fiber->fci.object) {
		GC_ADDREF(fiber->fci.object);
	}
}
/* }}} */


/* {{{ proto int Fiber::status() */
ZEND_METHOD(Fiber, status)
{
	ZEND_PARSE_PARAMETERS_NONE();

	concurrent_fiber *fiber = (concurrent_fiber *) Z_OBJ_P(getThis());

	RETURN_LONG(fiber->status);
}
/* }}} */


/* {{{ proto mixed Fiber::start($params...) */
ZEND_METHOD(Fiber, start)
{
	concurrent_fiber *fiber;
	zval *params;
	uint32_t param_count;

	ZEND_PARSE_PARAMETERS_START(0, -1)
		Z_PARAM_VARIADIC('+', params, param_count)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (concurrent_fiber *) Z_OBJ_P(getThis());

	if (fiber->status != CONCURRENT_FIBER_STATUS_INIT) {
		zend_throw_error(NULL, "Cannot start Fiber that has already been started");
		return;
	}

	fiber->fci.params = params;
	fiber->fci.param_count = param_count;
	fiber->fci.no_separation = 1;

	fiber->context = concurrent_fiber_create_context();

	if (fiber->context == NULL) {
		zend_throw_error(NULL, "Failed to create native fiber context");
		return;
	}

	if (!concurrent_fiber_create(fiber->context, concurrent_fiber_run, fiber->stack_size)) {
		zend_throw_error(NULL, "Failed to create native fiber");
		return;
	}

	fiber->stack = (zend_vm_stack) emalloc(CONCURRENT_FIBER_VM_STACK_SIZE);
	fiber->stack->top = ZEND_VM_STACK_ELEMENTS(fiber->stack) + 1;
	fiber->stack->end = (zval *) ((char *) fiber->stack + CONCURRENT_FIBER_VM_STACK_SIZE);
	fiber->stack->prev = NULL;

	fiber->value = USED_RET() ? return_value : NULL;

	if (!concurrent_fiber_switch_to(fiber)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}
}
/* }}} */


/* {{{ proto mixed Fiber::resume($value) */
ZEND_METHOD(Fiber, resume)
{
	concurrent_fiber *fiber;
	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (concurrent_fiber *) Z_OBJ_P(getThis());

	if (fiber->status != CONCURRENT_FIBER_STATUS_SUSPENDED) {
		zend_throw_error(NULL, "Non-suspended Fiber cannot be resumed");
		return;
	}

	if (val != NULL && fiber->value != NULL) {
		ZVAL_COPY(fiber->value, val);
		fiber->value = NULL;
	}

	fiber->status = CONCURRENT_FIBER_STATUS_RUNNING;
	fiber->value = USED_RET() ? return_value : NULL;

	if (!concurrent_fiber_switch_to(fiber)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}
}
/* }}} */


/* {{{ proto mixed Fiber::throw(Throwable $error) */
ZEND_METHOD(Fiber, throw)
{
	concurrent_fiber *fiber;
	zval *error;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(error)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (concurrent_fiber *) Z_OBJ_P(getThis());

	if (fiber->status != CONCURRENT_FIBER_STATUS_SUSPENDED) {
		zend_throw_exception_object(error);
		return;
	}

	Z_ADDREF_P(error);

	TASK_G(error) = error;

	fiber->status = CONCURRENT_FIBER_STATUS_RUNNING;
	fiber->value = USED_RET() ? return_value : NULL;

	if (!concurrent_fiber_switch_to(fiber)) {
		zend_throw_error(NULL, "Failed switching to fiber");
	}
}
/* }}} */


/* {{{ proto mixed Fiber::yield([$value]) */
ZEND_METHOD(Fiber, yield)
{
	concurrent_fiber *fiber;
	zend_execute_data *exec;
	size_t stack_page_size;
	zval *val;
	zval *error;

	fiber = TASK_G(current_fiber);

	if (UNEXPECTED(fiber == NULL)) {
		zend_throw_error(NULL, "Cannot yield from outside a fiber");
		return;
	}

	if (fiber->status != CONCURRENT_FIBER_STATUS_RUNNING) {
		zend_throw_error(NULL, "Cannot yield from a fiber that is not running");
		return;
	}

	val = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();

	if (val != NULL && fiber->value != NULL) {
		ZVAL_COPY(fiber->value, val);
		fiber->value = NULL;
	}

	fiber->status = CONCURRENT_FIBER_STATUS_SUSPENDED;
	fiber->value = USED_RET() ? return_value : NULL;

	CONCURRENT_FIBER_BACKUP_EG(fiber->stack, stack_page_size, fiber->exec);

	concurrent_fiber_yield(fiber->context);

	CONCURRENT_FIBER_RESTORE_EG(fiber->stack, stack_page_size, fiber->exec);

	if (fiber->status == CONCURRENT_FIBER_STATUS_DEAD) {
		zend_throw_error(NULL, "Fiber has been destroyed");
		return;
	}

	error = TASK_G(error);

	if (error != NULL) {
		TASK_G(error) = NULL;
		exec = EG(current_execute_data);

		exec->opline--;
		zend_throw_exception_internal(error);
		exec->opline++;
	}
}
/* }}} */


/* {{{ proto Fiber::__wakeup() */
ZEND_METHOD(Fiber, __wakeup)
{
	/* Just specifying the zend_class_unserialize_deny handler is not enough,
	 * because it is only invoked for C unserialization. For O the error has
	 * to be thrown in __wakeup. */

	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_exception(NULL, "Unserialization of 'Fiber' is not allowed", 0);
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_create, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callable, 0)
	ZEND_ARG_TYPE_INFO(0, stack_size, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_fiber_status, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_start, 0)
	ZEND_ARG_VARIADIC_INFO(0, arguments)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_resume, 0, 0, 1)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_throw, 0)
	 ZEND_ARG_OBJ_INFO(0, error, Throwable, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_void, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_fiber_yield, 0)
	ZEND_ARG_INFO(0, value)
ZEND_END_ARG_INFO()

static const zend_function_entry fiber_functions[] = {
	ZEND_ME(Fiber, __construct, arginfo_fiber_create, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(Fiber, status, arginfo_fiber_status, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, start, arginfo_fiber_start, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, resume, arginfo_fiber_resume, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, throw, arginfo_fiber_throw, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, yield, arginfo_fiber_yield, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Fiber, __wakeup, arginfo_fiber_void, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


void concurrent_fiber_ce_register()
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
	concurrent_fiber_ce = zend_register_internal_class(&ce);
	concurrent_fiber_ce->ce_flags |= ZEND_ACC_FINAL;
	concurrent_fiber_ce->create_object = concurrent_fiber_object_create;
	concurrent_fiber_ce->serialize = zend_class_serialize_deny;
	concurrent_fiber_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&concurrent_fiber_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	concurrent_fiber_handlers.free_obj = concurrent_fiber_object_destroy;
	concurrent_fiber_handlers.clone_obj = NULL;

	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_INIT", CONCURRENT_FIBER_STATUS_INIT);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_SUSPENDED", CONCURRENT_FIBER_STATUS_SUSPENDED);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_RUNNING", CONCURRENT_FIBER_STATUS_RUNNING);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_FINISHED", CONCURRENT_FIBER_STATUS_FINISHED);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_DEAD", CONCURRENT_FIBER_STATUS_DEAD);
}

void concurrent_fiber_ce_unregister()
{
	zend_string_free(fiber_run_func.function_name);
	fiber_run_func.function_name = NULL;
}

void concurrent_fiber_shutdown()
{
	concurrent_fiber_context root;
	root = TASK_G(root);

	TASK_G(root) = NULL;

	concurrent_fiber_destroy(root);
}
