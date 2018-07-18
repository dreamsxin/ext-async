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

struct _async_task_scheduler {
	/* Number of tasks scheduled to run. */
	size_t scheduled;

	/* Points to the next task to be run. */
	async_task *first;

	/* Points to the last task to be run (needed to insert tasks into the run queue. */
	async_task *last;

	/* Flag indidcating if the scheduler is integrated with an event loop. */
	zend_bool loop;

	/* Is set while an event loop is running. */
	zend_bool running;

	/* Is set while the scheduler is in the process of dispatching tasks. */
	zend_bool dispatching;

	/* Is set when the next task scheduling operation needs to trigger the activate() method. */
	zend_bool activate;

	/* Is set during the call to activate(), needed to prevent early dispatching. */
	zend_bool activating;

	/* Keeps track of all unfinished tasks that have been registered with the scheduler. */
	HashTable *tasks;

	/* Task PHP object handle. */
	zend_object std;
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
