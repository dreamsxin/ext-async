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

#include "php_async.h"

ZEND_DECLARE_MODULE_GLOBALS(async)

zend_class_entry *async_fiber_ce;

const zend_uchar ASYNC_FIBER_TYPE_DEFAULT = 0;

const zend_uchar ASYNC_FIBER_STATUS_INIT = 0;
const zend_uchar ASYNC_FIBER_STATUS_SUSPENDED = 1;
const zend_uchar ASYNC_FIBER_STATUS_RUNNING = 2;
const zend_uchar ASYNC_FIBER_STATUS_FINISHED = 3;
const zend_uchar ASYNC_FIBER_STATUS_DEAD = 4;

static zend_object_handlers async_fiber_handlers;

static zend_op_array fiber_run_func;
static zend_try_catch_element fiber_terminate_try_catch_array = { 0, 1, 0, 0 };
static zend_op fiber_run_op[2];

zend_bool async_fiber_switch_to(async_fiber *fiber)
{
	async_fiber_context root;
	async_fiber *prev;
	zend_bool result;
	zend_execute_data *exec;
	zend_vm_stack stack;
	size_t stack_page_size;

	root = ASYNC_G(root);

	if (root == NULL) {
		root = async_fiber_create_root_context();

		if (root == NULL) {
			return 0;
		}

		ASYNC_G(root) = root;
	}

	prev = ASYNC_G(current_fiber);
	ASYNC_G(current_fiber) = fiber;

	ASYNC_FIBER_BACKUP_EG(stack, stack_page_size, exec);
	result = async_fiber_switch_context((prev == NULL) ? root : prev->context, fiber->context);
	ASYNC_FIBER_RESTORE_EG(stack, stack_page_size, exec);

	ASYNC_G(current_fiber) = prev;

	return result;
}


void async_fiber_run()
{
	async_fiber *fiber;

	fiber = ASYNC_G(current_fiber);
	ZEND_ASSERT(fiber != NULL);

	EG(vm_stack) = fiber->stack;
	EG(vm_stack_top) = fiber->stack->top;
	EG(vm_stack_end) = fiber->stack->end;
	EG(vm_stack_page_size) = ASYNC_FIBER_VM_STACK_SIZE;

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

	zval_ptr_dtor(&fiber->fci.function_name);

	zend_vm_stack_destroy();
	fiber->stack = NULL;
	fiber->exec = NULL;

	async_fiber_yield(fiber->context);
}


static int fiber_run_opcode_handler(zend_execute_data *exec)
{
	async_fiber *fiber;

	zval retval;

	fiber = ASYNC_G(current_fiber);
	ZEND_ASSERT(fiber != NULL);

	fiber->status = ASYNC_FIBER_STATUS_RUNNING;
	fiber->fci.retval = &retval;

	if (zend_call_function(&fiber->fci, &fiber->fcc) == SUCCESS) {
		if (fiber->value != NULL && !EG(exception)) {
			ZVAL_ZVAL(fiber->value, &retval, 0, 1);
		}
	}

	if (EG(exception)) {
		if (fiber->status == ASYNC_FIBER_STATUS_DEAD) {
			zend_clear_exception();
		} else {
			fiber->status = ASYNC_FIBER_STATUS_DEAD;
		}
	} else {
		fiber->status = ASYNC_FIBER_STATUS_FINISHED;
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
		fiber->status = ASYNC_FIBER_STATUS_DEAD;

		async_fiber_switch_to(fiber);
	}

	if (fiber->status == ASYNC_FIBER_STATUS_INIT) {
		zval_ptr_dtor(&fiber->fci.function_name);
	}

	async_fiber_destroy(fiber->context);

	zend_object_std_dtor(&fiber->std);
}


/* {{{ proto Fiber::__construct(callable $callback, int stack_size) */
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

	if (stack_size == 0) {
		stack_size = 4096 * (((sizeof(void *)) < 8) ? 16 : 128);
	}

	fiber->status = ASYNC_FIBER_STATUS_INIT;
	fiber->stack_size = stack_size;

	// Keep a reference to closures or callable objects as long as the fiber lives.
	Z_TRY_ADDREF_P(&fiber->fci.function_name);
}
/* }}} */


/* {{{ proto int Fiber::status() */
ZEND_METHOD(Fiber, status)
{
	ZEND_PARSE_PARAMETERS_NONE();

	async_fiber *fiber = (async_fiber *) Z_OBJ_P(getThis());

	RETURN_LONG(fiber->status);
}
/* }}} */


/* {{{ proto mixed Fiber::start($params...) */
ZEND_METHOD(Fiber, start)
{
	async_fiber *fiber;
	uint32_t param_count;

	zval *params;

	ZEND_PARSE_PARAMETERS_START(0, -1)
		Z_PARAM_VARIADIC('+', params, param_count)
	ZEND_PARSE_PARAMETERS_END();

	fiber = (async_fiber *) Z_OBJ_P(getThis());

	if (fiber->status != ASYNC_FIBER_STATUS_INIT) {
		zend_throw_error(NULL, "Cannot start Fiber that has already been started");
		return;
	}

	fiber->fci.params = params;
	fiber->fci.param_count = param_count;
	fiber->fci.no_separation = 1;

	fiber->context = async_fiber_create_context();

	ASYNC_CHECK_ERROR(fiber->context == NULL, "Failed to create native fiber context");
	ASYNC_CHECK_ERROR(!async_fiber_create(fiber->context, async_fiber_run, fiber->stack_size), "Failed to create native fiber");

	fiber->stack = (zend_vm_stack) emalloc(ASYNC_FIBER_VM_STACK_SIZE);
	fiber->stack->top = ZEND_VM_STACK_ELEMENTS(fiber->stack) + 1;
	fiber->stack->end = (zval *) ((char *) fiber->stack + ASYNC_FIBER_VM_STACK_SIZE);
	fiber->stack->prev = NULL;

	fiber->value = USED_RET() ? return_value : NULL;

	ASYNC_CHECK_ERROR(!async_fiber_switch_to(fiber), "Failed switching to fiber");
}
/* }}} */


/* {{{ proto mixed Fiber::resume($value) */
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

	if (fiber->status != ASYNC_FIBER_STATUS_SUSPENDED) {
		zend_throw_error(NULL, "Non-suspended Fiber cannot be resumed");
		return;
	}

	if (val != NULL && fiber->value != NULL) {
		ZVAL_COPY(fiber->value, val);
		fiber->value = NULL;
	}

	fiber->status = ASYNC_FIBER_STATUS_RUNNING;
	fiber->value = USED_RET() ? return_value : NULL;

	ASYNC_CHECK_ERROR(!async_fiber_switch_to(fiber), "Failed switching to fiber");
}
/* }}} */


/* {{{ proto mixed Fiber::throw(Throwable $error) */
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

	ASYNC_CHECK_ERROR(!async_fiber_switch_to(fiber), "Failed switching to fiber");
}
/* }}} */


/* {{{ proto bool Fiber::isRunning() */
ZEND_METHOD(Fiber, isRunning)
{
	async_fiber *fiber;

	ZEND_PARSE_PARAMETERS_NONE();

	fiber = ASYNC_G(current_fiber);

	RETURN_BOOL(fiber != NULL && fiber->type == ASYNC_FIBER_TYPE_DEFAULT);
}
/* }}} */

/* {{{ proto bool Fiber::isRunning() */
ZEND_METHOD(Fiber, backend)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_STRING(async_fiber_backend_info());
}
/* }}} */

/* {{{ proto mixed Fiber::yield([$value]) */
ZEND_METHOD(Fiber, yield)
{
	async_fiber *fiber;
	zend_execute_data *exec;
	size_t stack_page_size;

	zval *val;
	zval *error;

	fiber = ASYNC_G(current_fiber);

	if (UNEXPECTED(fiber == NULL)) {
		zend_throw_error(NULL, "Cannot yield from outside a fiber");
		return;
	}

	if (fiber->type != ASYNC_FIBER_TYPE_DEFAULT) {
		zend_throw_error(NULL, "Cannot yield from an async task");
		return;
	}

	if (fiber->status != ASYNC_FIBER_STATUS_RUNNING) {
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

	fiber->status = ASYNC_FIBER_STATUS_SUSPENDED;
	fiber->value = USED_RET() ? return_value : NULL;

	ASYNC_FIBER_BACKUP_EG(fiber->stack, stack_page_size, fiber->exec);
	async_fiber_yield(fiber->context);
	ASYNC_FIBER_RESTORE_EG(fiber->stack, stack_page_size, fiber->exec);

	if (fiber->status == ASYNC_FIBER_STATUS_DEAD) {
		zend_throw_error(NULL, "Fiber has been destroyed");
		return;
	}

	error = ASYNC_G(error);

	if (error != NULL) {
		ASYNC_G(error) = NULL;
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
	ZEND_PARSE_PARAMETERS_NONE();

	zend_throw_error(NULL, "Unserialization of a fiber is not allowed");
}
/* }}} */


ZEND_BEGIN_ARG_INFO_EX(arginfo_fiber_create, 0, 0, 1)
	ZEND_ARG_CALLABLE_INFO(0, callback, 0)
	ZEND_ARG_TYPE_INFO(0, stack_size, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_fiber_status, 0, 0, IS_LONG, 0)
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
	ZEND_ME(Fiber, __construct, arginfo_fiber_create, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
	ZEND_ME(Fiber, status, arginfo_fiber_status, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, start, arginfo_fiber_start, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, resume, arginfo_fiber_resume, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, throw, arginfo_fiber_throw, ZEND_ACC_PUBLIC)
	ZEND_ME(Fiber, isRunning, arginfo_fiber_is_running, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Fiber, backend, arginfo_fiber_backend, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
	ZEND_ME(Fiber, yield, arginfo_fiber_yield, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
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

	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_INIT", ASYNC_FIBER_STATUS_INIT);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_SUSPENDED", ASYNC_FIBER_STATUS_SUSPENDED);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_RUNNING", ASYNC_FIBER_STATUS_RUNNING);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_FINISHED", ASYNC_FIBER_STATUS_FINISHED);
	REGISTER_FIBER_CLASS_CONST_LONG("STATUS_DEAD", ASYNC_FIBER_STATUS_DEAD);
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
