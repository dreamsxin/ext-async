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

HashTable *async_task_get_debug_info(async_task *task, zend_bool include_result);

async_task *async_task_object_create(zend_execute_data *call, async_task_scheduler *scheduler, async_context *context);
void async_task_start(async_task *task);
void async_task_continue(async_task *task);
void async_task_dispose(async_task *task);

zend_bool async_task_scheduler_enqueue(async_task *task);
void async_task_scheduler_dequeue(async_task *task);
void async_task_scheduler_run_loop(async_task_scheduler *scheduler);
void async_task_scheduler_stop_loop(async_task_scheduler *scheduler);

void async_task_scheduler_enqueue_enable(async_task_scheduler *scheduler, async_enable_cb *cb);

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
