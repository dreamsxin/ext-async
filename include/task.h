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

#ifndef ASYNC_TASK_H
#define ASYNC_TASK_H

#include "php.h"
#include "awaitable.h"
#include "context.h"
#include "fiber.h"

typedef struct _async_task_scheduler async_task_scheduler;

BEGIN_EXTERN_C()

extern zend_class_entry *async_task_ce;

typedef struct _async_task async_task;

struct _async_task {
	/* Embedded fiber. */
	async_fiber fiber;

	/* Task scheduler being used to execute the task. */
	async_task_scheduler *scheduler;

	/* Async execution context provided to the task. */
	async_context *context;

	/* Next task scheduled for execution. */
	async_task *next;

	/* Previous task scheduled for execution. */
	async_task *prev;

	/* Next operation to be performed by the scheduler, one of the ASYNC_TASK_OPERATION_* constants. */
	zend_uchar operation;

	/* Error to be thrown into a task, must be set to UNDEF to resume tasks with a value. */
	zval error;

	/* Return value of the task, may also be an error object, check status for outcome. */
	zval result;

	/* Current suspension point of the task. */
	async_awaitable_cb *suspended;

	/* Linked list of registered continuation callbacks. */
	async_awaitable_cb *continuation;
};

extern const zend_uchar ASYNC_FIBER_TYPE_TASK;

extern const zend_uchar ASYNC_TASK_OPERATION_NONE;
extern const zend_uchar ASYNC_TASK_OPERATION_START;
extern const zend_uchar ASYNC_TASK_OPERATION_RESUME;

void async_task_dispose(async_task *task);
async_task *async_task_object_create();

void async_task_start(async_task *task);
void async_task_continue(async_task *task);

void async_task_ce_register();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
