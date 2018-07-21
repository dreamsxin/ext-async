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

#ifndef ASYNC_TASK_SCHEDULER_H
#define ASYNC_TASK_SCHEDULER_H

#include "php.h"
#include "context.h"

typedef struct _async_task async_task;

BEGIN_EXTERN_C()

typedef struct _async_task_scheduler async_task_scheduler;
typedef struct _async_task_scheduler_stack async_task_scheduler_stack;
typedef struct _async_task_scheduler_stack_entry async_task_scheduler_stack_entry;
typedef struct _async_task_queue async_task_queue;

struct _async_task_queue {
	/* Number of queued tasks. */
	uint32_t size;

	/* First task in the queue, used by dequeue(). */
	async_task *first;

	/* Last task in the queue, used by enqueue(). */
	async_task *last;
};

struct _async_task_scheduler {
	/* Flag indidcating if the scheduler is integrated with an event loop. */
	zend_bool loop;

	/* Is set while an event loop is running. */
	zend_bool running;

	/* Has the scheduler been modified since the last call to run. */
	zend_bool modified;

	/* Is set while the scheduler is in the process of dispatching tasks. */
	zend_bool dispatching;

	/* Is set when the next task scheduling operation needs to trigger the activate() method. */
	zend_bool activate;

	/* Is set during the call to activate(), needed to prevent early dispatching. */
	zend_bool activating;

	/* Tasks ready to be started or resumed. */
	async_task_queue ready;

	/* Tasks that are suspended. */
	async_task_queue suspended;

	/* PHP object handle. */
	zend_object std;
};

struct _async_task_scheduler_stack_entry {
	/* Refers to the task scheduler. */
	async_task_scheduler *scheduler;

	/* Points to the previous scheduler, NULL if no stacked scheduler is active. */
	async_task_scheduler_stack_entry *prev;
};

struct _async_task_scheduler_stack {
	/* Number of stacked schedulers. */
	size_t size;

	/* Top-most scheduler on the stack. */
	async_task_scheduler_stack_entry *top;
};

async_task_scheduler *async_task_scheduler_get();

zend_bool async_task_scheduler_enqueue(async_task *task);
void async_task_scheduler_dequeue(async_task *task);

void async_task_scheduler_run_loop(async_task_scheduler *scheduler);
void async_task_scheduler_stop_loop(async_task_scheduler *scheduler);

void async_task_scheduler_ce_register();
void async_task_scheduler_ce_unregister();

void async_task_scheduler_shutdown();

END_EXTERN_C()

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
