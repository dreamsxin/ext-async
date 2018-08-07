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

#ifndef PHP_ASYNC_H
#define PHP_ASYNC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef PHP_WIN32
#include <Winsock2.h>
#include <Mswsock.h>
#include <psapi.h>
#include <Iphlpapi.h>
#endif

#include "php.h"

extern zend_module_entry async_module_entry;
#define phpext_async_ptr &async_module_entry

#define PHP_ASYNC_VERSION "0.2.0"

#ifdef PHP_WIN32
# define ASYNC_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
# define ASYNC_API __attribute__ ((visibility("default")))
#else
# define ASYNC_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

#include "uv.h"

#include "zend.h"
#include "zend_API.h"
#include "zend_vm.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "zend_closures.h"

#include "php_network.h"
#include "php_streams.h"

#if !defined(PHP_WIN32) || (defined(HAVE_SOCKETS) && !defined(COMPILE_DL_SOCKETS))
#define ASYNC_SOCKETS 1
#else
#define ASYNC_SOCKETS 0
#endif

#if defined(HAVE_SOCKETS) && !defined(COMPILE_DL_SOCKETS)
#include "ext/sockets/php_sockets.h"
#elif !defined(PHP_WIN32)
typedef struct {
	int bsd_socket;
} php_socket;
#endif

#define ASYNC_FIBER_VM_STACK_SIZE 4096

#define ASYNC_OP_RESOLVED 64
#define ASYNC_OP_FAILED 65

ASYNC_API extern zend_bool async_cli;

ASYNC_API extern zend_class_entry *async_awaitable_ce;
ASYNC_API extern zend_class_entry *async_context_ce;
ASYNC_API extern zend_class_entry *async_context_var_ce;
ASYNC_API extern zend_class_entry *async_deferred_ce;
ASYNC_API extern zend_class_entry *async_deferred_awaitable_ce;
ASYNC_API extern zend_class_entry *async_fiber_ce;
ASYNC_API extern zend_class_entry *async_signal_watcher_ce;
ASYNC_API extern zend_class_entry *async_stream_watcher_ce;
ASYNC_API extern zend_class_entry *async_task_ce;
ASYNC_API extern zend_class_entry *async_task_scheduler_ce;
ASYNC_API extern zend_class_entry *async_timer_ce;


void async_awaitable_ce_register();
void async_context_ce_register();
void async_deferred_ce_register();
void async_fiber_ce_register();
void async_signal_watcher_ce_register();
void async_stream_watcher_ce_register();
void async_task_ce_register();
void async_task_scheduler_ce_register();
void async_timer_ce_register();

void async_fiber_ce_unregister();

void async_context_shutdown();
void async_fiber_shutdown();
void async_task_scheduler_shutdown();


typedef struct _async_awaitable_cb                  async_awaitable_cb;
typedef struct _async_awaitable_queue               async_awaitable_queue;
typedef struct _async_cancel_cb                     async_cancel_cb;
typedef struct _async_cancel_queue                  async_cancel_queue;
typedef struct _async_cancellation_handler          async_cancellation_handler;
typedef struct _async_cancellation_handler          async_cancellation_handler;
typedef struct _async_context                       async_context;
typedef struct _async_context_var                   async_context_var;
typedef struct _async_deferred                      async_deferred;
typedef struct _async_deferred_awaitable            async_deferred_awaitable;
typedef struct _async_deferred_combine              async_deferred_combine;
typedef struct _async_deferred_transform            async_deferred_transform;
typedef struct _async_enable_cb                     async_enable_cb;
typedef struct _async_enable_queue                  async_enable_queue;
typedef struct _async_fiber                         async_fiber;
typedef struct _async_signal_watcher                async_signal_watcher;
typedef struct _async_stream_watcher                async_stream_watcher;
typedef struct _async_task                          async_task;
typedef struct _async_task_suspended                async_task_suspended;
typedef struct _async_task_scheduler                async_task_scheduler;
typedef struct _async_task_scheduler_stack          async_task_scheduler_stack;
typedef struct _async_task_scheduler_stack_entry    async_task_scheduler_stack_entry;
typedef struct _async_task_queue                    async_task_queue;
typedef struct _async_timer                         async_timer;

typedef void* async_fiber_context;

typedef void (* async_awaitable_func)(void *obj, zval *data, zval *result, zend_bool success);
typedef void (* async_cancel_func)(void *obj, zval *error, async_cancel_cb *cb);

typedef void (* async_fiber_func)();
typedef void (* async_fiber_run_func)(async_fiber *fiber);

typedef void (* async_enable_func)(void *obj);

extern const zend_uchar ASYNC_DEFERRED_STATUS_PENDING;
extern const zend_uchar ASYNC_DEFERRED_STATUS_RESOLVED;
extern const zend_uchar ASYNC_DEFERRED_STATUS_FAILED;

extern const zend_uchar ASYNC_FIBER_TYPE_DEFAULT;
extern const zend_uchar ASYNC_FIBER_TYPE_TASK;

extern const zend_uchar ASYNC_FIBER_STATUS_INIT;
extern const zend_uchar ASYNC_FIBER_STATUS_SUSPENDED;
extern const zend_uchar ASYNC_FIBER_STATUS_RUNNING;
extern const zend_uchar ASYNC_FIBER_STATUS_FINISHED;
extern const zend_uchar ASYNC_FIBER_STATUS_FAILED;

extern const int ASYNC_SIGNAL_SIGINT;
extern const int ASYNC_SIGNAL_SIGHUP;
extern const int ASYNC_SIGNAL_SIGQUIT;
extern const int ASYNC_SIGNAL_SIGKILL;
extern const int ASYNC_SIGNAL_SIGTERM;
extern const int ASYNC_SIGNAL_SIGUSR1;
extern const int ASYNC_SIGNAL_SIGUSR2;

extern const zend_uchar ASYNC_TASK_OPERATION_NONE;
extern const zend_uchar ASYNC_TASK_OPERATION_START;
extern const zend_uchar ASYNC_TASK_OPERATION_RESUME;


struct _async_awaitable_cb {
	/* Object that registered the continuation. */
	void *object;

	/* Arbitrary zval being passed to the continuation. */
	zval data;

	/* Continuation function to be called. */
	async_awaitable_func func;

	/* Link to the next registered continuation (or NULL if no more continuations are available). */
	async_awaitable_cb *next;

	/* Link to the previous registered continuation (or NULL if this is the first one). */
	async_awaitable_cb *prev;
};

struct _async_awaitable_queue {
	async_awaitable_cb *first;
	async_awaitable_cb *last;
};

struct _async_cancel_cb {
	void *object;
	async_cancel_func func;
	async_cancel_cb *prev;
	async_cancel_cb *next;
};

struct _async_cancel_queue {
	async_cancel_cb *first;
	async_cancel_cb *last;
};

struct _async_enable_cb {
	zend_bool active;
	void *object;
	async_enable_func func;
	async_enable_cb *prev;
	async_enable_cb *next;
};

struct _async_enable_queue {
	async_enable_cb *first;
	async_enable_cb *last;
};


struct _async_cancellation_handler {
	/* PHP object handle. */
	zend_object std;

	/* Cancellable context instance. */
	async_context *context;

	/* Error that caused cancellation, UNDEF by default. */
	zval error;

	/* Chain handler that connects the cancel handler to the parent handler. */
	async_cancel_cb chain;

	/* Linked list of cancellation callbacks. */
	async_cancel_queue callbacks;
};

struct _async_context {
	/* PHP object handle. */
	zend_object std;

	/* Refers to the parent context. */
	async_context *parent;

	/* Set if the context is a background context. */
	zend_bool background;

	/* Context var or NULL. */
	async_context_var *var;

	/* Value of the context var, defaults to zval NULL. */
	zval value;

	async_cancellation_handler *cancel;
};

struct _async_context_var {
	/* PHP object handle. */
	zend_object std;
};

struct _async_deferred {
	/* PHP object handle. */
	zend_object std;

	/* Status of the deferred, one of the ASYNC_DEFERRED_STATUS_* constants. */
	zend_uchar status;

	/* Result (or error) value in case of resolved deferred. */
	zval result;

	/* Context instance (only needed if cancellation is available). */
	async_context *context;

	async_cancel_cb *cancel;

	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	/* Linked list of registered continuation callbacks. */
	async_awaitable_queue continuation;
};

struct _async_deferred_awaitable {
	/* PHP object handle. */
	zend_object std;

	/* Refers to the deferred object that created the awaitable. */
	async_deferred *defer;
};

struct _async_deferred_combine {
	async_deferred *defer;
	zend_long counter;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

struct _async_deferred_transform {
	async_deferred *defer;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

struct _async_fiber {
	/* Fiber PHP object handle. */
	zend_object std;

	/* Implementation-specific fiber type. */
	zend_uchar type;

	/* Status of the fiber, one of the ASYNC_FIBER_STATUS_* constants. */
	zend_uchar status;

	/* Flag indicating if the fiber has been disposed yet. */
	zend_bool disposed;

	/* Callback and info / cache to be used when fiber is started. */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	/* Native fiber context of this fiber, will be created during call to start(). */
	async_fiber_context context;

	/* Custom fiber execution function (NULL for default handler). */
	async_fiber_run_func func;

	/* Destination for a PHP value being passed into or returned from the fiber. */
	zval *value;

	/* Current Zend VM execute data being run by the fiber. */
	zend_execute_data *exec;

	/* VM stack being used by the fiber. */
	zend_vm_stack stack;

	/* Max size of the C stack being used by the fiber. */
	size_t stack_size;

	zend_string *file;
	size_t line;
};

struct _async_signal_watcher {
	/* PHP object handle. */
	zend_object std;

	/* Error being set as the watcher was closed (undef by default). */
	zval error;

	int signum;

	uv_signal_t signal;

	async_awaitable_queue observers;

	zend_uchar ref_count;
	zend_uchar unref_count;

	zend_bool running;
	zend_bool new_running;

	async_task_scheduler *scheduler;
	async_enable_cb enable;
};

struct _async_stream_watcher {
	/* PHP object handle. */
	zend_object std;

	/* Error being set as the watcher was closed (undef by default). */
	zval error;

	/* PHP stream or socket being observed. */
	zval resource;

	/** File descriptor being polled by libuv. */
	php_socket_t fd;

	/* Libuv poll instance being used to receive events. */
	uv_poll_t poll;

	/* Queue of tasks wanting to be notified when the stream is readable. */
	async_awaitable_queue reads;

	/* Queue of tasks wanting to be notified when the stream is writable. */
	async_awaitable_queue writes;

	/* Number of pending referenced read / write operations. */
	zend_uchar ref_count;

	/* Number of pending unreferenced read / write operations. */
	zend_uchar unref_count;

	zend_uchar events;
	zend_uchar new_events;

	async_task_scheduler *scheduler;
	async_enable_cb enable;
};

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
	async_awaitable_queue continuation;
};

struct _async_task_suspended {
	/* Active root task scheduler. */
	async_task_scheduler *scheduler;

	/* State of the continuation. */
	zend_uchar state;

	/* Result of the awaited operation. */
	zval result;
};

struct _async_task_queue {
	/* First task in the queue, used by dequeue(). */
	async_task *first;

	/* Last task in the queue, used by enqueue(). */
	async_task *last;
};

struct _async_task_scheduler {
	/* PHP object handle. */
	zend_object std;

	/* Is set while an event loop is running. */
	zend_bool running;

	/* Is set while the scheduler is in the process of dispatching tasks. */
	zend_bool dispatching;

	/* Tasks ready to be started or resumed. */
	async_task_queue ready;

	/* Tasks that are suspended. */
	async_task_queue suspended;

	/* Libuv event loop. */
	uv_loop_t loop;

	/* Idle handler being used to dispatch tasks from within a running event loop. */
	uv_idle_t idle;

	/* Is set to 0 when the idle handler must be activated when something changes. */
	zend_bool changes;

	/* Queue of callbacks that are executed due to handle state change. */
	async_enable_queue enable;
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

struct _async_timer {
	/* PHP object handle. */
	zend_object std;

	/* Error being set as the watcher was closed (undef by default). */
	zval error;

	/* Timer interval in milliseconds. */
	uint64_t delay;

	/* UV timer handle. */
	uv_timer_t timer;

	/* Queued timeout continuations. */
	async_awaitable_queue timeouts;

	/* Number of pending referenced timeout subscriptions. */
	zend_uchar ref_count;

	/* Number of pending unreferenced timeout subscriptions. */
	zend_uchar unref_count;

	zend_bool running;
	zend_bool new_running;

	async_task_scheduler *scheduler;
	async_enable_cb enable;
};


ASYNC_API async_awaitable_cb *async_awaitable_register_continuation(async_awaitable_queue *q, void *obj, zval *data, async_awaitable_func func);
ASYNC_API void async_awaitable_dispose_continuation(async_awaitable_queue *q, async_awaitable_cb *cb);
ASYNC_API void async_awaitable_trigger_continuation(async_awaitable_queue *q, zval *result, zend_bool success);

ASYNC_API async_context *async_context_get();

ASYNC_API void async_task_suspend(async_awaitable_queue *q, zval *return_value, zend_execute_data *execute_data, zend_bool cancellable);

ASYNC_API uv_loop_t *async_task_scheduler_get_loop();
ASYNC_API async_task_scheduler *async_task_scheduler_get();


ZEND_BEGIN_MODULE_GLOBALS(async)
	/* Root fiber context (main thread). */
	async_fiber_context root;

	/* Active fiber, NULL when in main thread. */
	async_fiber *current_fiber;

	/* Root context. */
	async_context *context;

	/* Active task context. */
	async_context *current_context;

	/* Fallback root task scheduler. */
	async_task_scheduler *scheduler;

	/* Stack of registered default schedulers. */
	async_task_scheduler_stack *scheduler_stack;

	/* Running task scheduler. */
	async_task_scheduler *current_scheduler;

	/* Default fiber C stack size. */
	zend_long stack_size;

	/* Error to be thrown into a fiber (will be populated by throw()). */
	zval *error;

ZEND_END_MODULE_GLOBALS(async)

ASYNC_API ZEND_EXTERN_MODULE_GLOBALS(async)
#define ASYNC_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(async, v)

#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#define ASYNC_FIBER_BACKUP_EG(stack, stack_page_size, exec) do { \
	stack = EG(vm_stack); \
	stack->top = EG(vm_stack_top); \
	stack->end = EG(vm_stack_end); \
	stack_page_size = EG(vm_stack_page_size); \
	exec = EG(current_execute_data); \
} while (0)

#define ASYNC_FIBER_RESTORE_EG(stack, stack_page_size, exec) do { \
	EG(vm_stack) = stack; \
	EG(vm_stack_top) = stack->top; \
	EG(vm_stack_end) = stack->end; \
	EG(vm_stack_page_size) = stack_page_size; \
	EG(current_execute_data) = exec; \
} while (0)

#ifdef __GNUC__

#define ASYNC_CHECK_ERROR(expr, message, ...) do { \
    if (UNEXPECTED(expr)) { \
    	zend_throw_error(NULL, message, ##__VA_ARGS__); \
    	return; \
    } \
} while (0)

#define ASYNC_CHECK_FATAL(expr, message, ...) do { \
	if (UNEXPECTED(expr)) { \
		zend_error_noreturn(E_CORE_ERROR, message, ##__VA_ARGS__); \
	} \
} while (0)

#else

#define ASYNC_CHECK_ERROR(expr, message, ...) do { \
    if (UNEXPECTED(expr)) { \
    	zend_throw_error(NULL, message, __VA_ARGS__); \
    	return; \
    } \
} while (0)

#define ASYNC_CHECK_FATAL(expr, message, ...) do { \
	if (UNEXPECTED(expr)) { \
		zend_error_noreturn(E_CORE_ERROR, message, __VA_ARGS__); \
	} \
} while (0)

#endif

/*
 * Queue macros require a "q" pointer with fields "first" and "last" of same ponter type as "v".
 * The "v" pointer must have fields "prev" and "next" of the same pointer type as "v".
 */

#define ASYNC_Q_ENQUEUE(q, v) do { \
	(v)->next = NULL; \
	if ((q)->last == NULL) { \
		(v)->prev = NULL; \
		(q)->first = v; \
		(q)->last = v; \
	} else { \
		(v)->prev = (q)->last; \
		(q)->last->next = v; \
		(q)->last = v; \
	} \
} while (0)

#define ASYNC_Q_DEQUEUE(q, v) do { \
	if ((q)->first == NULL) { \
		v = NULL; \
	} else { \
		v = (q)->first; \
		(q)->first = (v)->next; \
		if ((q)->first != NULL) { \
			(q)->first->prev = NULL; \
		} \
		if ((q)->last == v) { \
			(q)->last = NULL; \
		} \
		(v)->next = NULL; \
		(v)->prev = NULL; \
	} \
} while (0)

#define ASYNC_Q_DETACH(q, v) do { \
	if ((v)->prev != NULL) { \
		(v)->prev->next = (v)->next; \
	} \
	if ((v)->next != NULL) { \
		(v)->next->prev = (v)->prev; \
	} \
	if ((q)->first == v) { \
		(q)->first = (v)->next; \
	} \
	if ((q)->last == v) { \
		(q)->last = (v)->prev; \
	} \
	(v)->next = NULL; \
	(v)->prev = NULL; \
} while (0)

#endif

/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
