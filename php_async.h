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

#undef HAVE_ASYNC_SSL
#define HAVE_ASYNC_SSL 1

#endif

#ifdef HAVE_ASYNC_SSL
#include <openssl/opensslv.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

#include "uv.h"
#include "php.h"

extern zend_module_entry async_module_entry;
#define phpext_async_ptr &async_module_entry

#define PHP_ASYNC_VERSION "0.3.0"

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

#ifdef __GNUC__
#define ASYNC_VA_ARGS(...) , ##__VA_ARGS__
#else
#define ASYNC_VA_ARGS(...) , __VA_ARGS__
#endif

#include "zend.h"
#include "zend_API.h"
#include "zend_vm.h"
#include "zend_interfaces.h"
#include "zend_exceptions.h"
#include "zend_closures.h"

#ifdef PHP_WIN32
#include "win32/winutil.h"
#endif

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

#define ASYNC_OP_PENDING 0
#define ASYNC_OP_RESOLVED 64
#define ASYNC_OP_FAILED 65
#define ASYNC_OP_CANCELLED 66

ASYNC_API extern zend_bool async_cli;
ASYNC_API extern char async_ssl_config_file[MAXPATHLEN];

ASYNC_API extern zend_class_entry *async_awaitable_ce;
ASYNC_API extern zend_class_entry *async_channel_ce;
ASYNC_API extern zend_class_entry *async_channel_closed_exception_ce;
ASYNC_API extern zend_class_entry *async_channel_group_ce;
ASYNC_API extern zend_class_entry *async_channel_iterator_ce;
ASYNC_API extern zend_class_entry *async_context_ce;
ASYNC_API extern zend_class_entry *async_context_var_ce;
ASYNC_API extern zend_class_entry *async_deferred_ce;
ASYNC_API extern zend_class_entry *async_deferred_awaitable_ce;
ASYNC_API extern zend_class_entry *async_duplex_stream_ce;
ASYNC_API extern zend_class_entry *async_fiber_ce;
ASYNC_API extern zend_class_entry *async_pending_read_exception_ce;
ASYNC_API extern zend_class_entry *async_process_builder_ce;
ASYNC_API extern zend_class_entry *async_process_ce;
ASYNC_API extern zend_class_entry *async_readable_pipe_ce;
ASYNC_API extern zend_class_entry *async_readable_stream_ce;
ASYNC_API extern zend_class_entry *async_server_ce;
ASYNC_API extern zend_class_entry *async_socket_ce;
ASYNC_API extern zend_class_entry *async_socket_exception_ce;
ASYNC_API extern zend_class_entry *async_socket_stream_ce;
ASYNC_API extern zend_class_entry *async_stream_closed_exception_ce;
ASYNC_API extern zend_class_entry *async_stream_exception_ce;
ASYNC_API extern zend_class_entry *async_signal_watcher_ce;
ASYNC_API extern zend_class_entry *async_stream_watcher_ce;
ASYNC_API extern zend_class_entry *async_task_ce;
ASYNC_API extern zend_class_entry *async_task_scheduler_ce;
ASYNC_API extern zend_class_entry *async_tcp_server_ce;
ASYNC_API extern zend_class_entry *async_tcp_socket_ce;
ASYNC_API extern zend_class_entry *async_tls_client_encryption_ce;
ASYNC_API extern zend_class_entry *async_tls_server_encryption_ce;
ASYNC_API extern zend_class_entry *async_timer_ce;
ASYNC_API extern zend_class_entry *async_writable_pipe_ce;
ASYNC_API extern zend_class_entry *async_writable_stream_ce;


void async_awaitable_ce_register();
void async_channel_ce_register();
void async_context_ce_register();
void async_deferred_ce_register();
void async_dns_ce_register();
void async_fiber_ce_register();
void async_process_ce_register();
void async_signal_watcher_ce_register();
void async_socket_ce_register();
void async_ssl_ce_register();
void async_stream_ce_register();
void async_stream_watcher_ce_register();
void async_task_ce_register();
void async_task_scheduler_ce_register();
void async_tcp_ce_register();
void async_timer_ce_register();
void async_udp_socket_ce_register();

void async_fiber_ce_unregister();

void async_init();
void async_shutdown();

void async_dns_init();
void async_filesystem_init();
void async_tcp_socket_init();
void async_timer_init();
void async_udp_socket_init();

void async_context_shutdown();
void async_dns_shutdown();
void async_fiber_shutdown();
void async_filesystem_shutdown();
void async_tcp_socket_shutdown();
void async_timer_shutdown();
void async_udp_socket_shutdown();

void async_task_scheduler_run();
void async_task_scheduler_shutdown();


typedef struct _async_cancel_cb                     async_cancel_cb;
typedef struct _async_cancellation_handler          async_cancellation_handler;
typedef struct _async_cancellation_token            async_cancellation_token;
typedef struct _async_channel                       async_channel;
typedef struct _async_channel_buffer                async_channel_buffer;
typedef struct _async_channel_group                 async_channel_group;
typedef struct _async_channel_iterator              async_channel_iterator;
typedef struct _async_context                       async_context;
typedef struct _async_context_var                   async_context_var;
typedef struct _async_deferred                      async_deferred;
typedef struct _async_deferred_awaitable            async_deferred_awaitable;
typedef struct _async_deferred_state                async_deferred_state;
typedef struct _async_fiber                         async_fiber;
typedef struct _async_op                            async_op;
typedef struct _async_task                          async_task;
typedef struct _async_task_scheduler                async_task_scheduler;

typedef void *async_fiber_context;
typedef void (* async_fiber_func)();

typedef struct {
	async_cancel_cb *first;
	async_cancel_cb *last;
} async_cancel_queue;

typedef struct {
	async_channel_buffer *first;
	async_channel_buffer *last;
} async_channel_buffer_queue;

typedef struct {
	async_op *first;
	async_op *last;
} async_op_queue;

typedef struct {
	async_task *first;
	async_task *last;
} async_task_queue;

typedef struct {
	zend_execute_data *exec;
	zend_vm_stack stack;
	size_t stack_page_size;
} async_vm_state;

#define ASYNC_DEFERRED_STATUS_PENDING 0
#define ASYNC_DEFERRED_STATUS_RESOLVED ASYNC_OP_RESOLVED
#define ASYNC_DEFERRED_STATUS_FAILED ASYNC_OP_FAILED

#define ASYNC_FIBER_TYPE_DEFAULT 0
#define ASYNC_FIBER_TYPE_TASK 1

#define ASYNC_FIBER_STATUS_INIT 0
#define ASYNC_FIBER_STATUS_SUSPENDED 1
#define ASYNC_FIBER_STATUS_RUNNING 2
#define ASYNC_FIBER_STATUS_FINISHED ASYNC_OP_RESOLVED
#define ASYNC_FIBER_STATUS_FAILED ASYNC_OP_FAILED

#define ASYNC_PROCESS_STDIN 0
#define ASYNC_PROCESS_STDOUT 1
#define ASYNC_PROCESS_STDERR 2

#define ASYNC_PROCESS_STDIO_IGNORE 16
#define ASYNC_PROCESS_STDIO_INHERIT 17
#define ASYNC_PROCESS_STDIO_PIPE 18

#define ASYNC_SIGNAL_SIGHUP 1
#define ASYNC_SIGNAL_SIGINT 2

#ifdef PHP_WIN32
#define ASYNC_SIGNAL_SIGQUIT -1
#define ASYNC_SIGNAL_SIGKILL -1
#define ASYNC_SIGNAL_SIGTERM -1
#else
#define ASYNC_SIGNAL_SIGQUIT 3
#define ASYNC_SIGNAL_SIGKILL 9
#define ASYNC_SIGNAL_SIGTERM 15
#endif

#ifdef SIGUSR1
#define ASYNC_SIGNAL_SIGUSR1 SIGUSR1
#else
#define ASYNC_SIGNAL_SIGUSR1 -1
#endif

#ifdef SIGUSR2
#define ASYNC_SIGNAL_SIGUSR2 SIGUSR2
#else
#define ASYNC_SIGNAL_SIGUSR2 -1
#endif

#define ASYNC_TASK_OPERATION_NONE 0
#define ASYNC_TASK_OPERATION_START 1
#define ASYNC_TASK_OPERATION_RESUME 2

struct _async_cancel_cb {
	/* Struct being passed to callback as first arg. */
	void *object;
	
	/* Pointer to cancel function. */
	void (* func)(void *obj, zval *error);
	
	/* Queue pointers. */
	async_cancel_cb *prev;
	async_cancel_cb *next;
};

#define ASYNC_OP_FLAG_CANCELLED 1
#define ASYNC_OP_FLAG_DEFER 2

typedef enum {
	ASYNC_STATUS_PENDING,
	ASYNC_STATUS_RUNNING,
	ASYNC_STATUS_RESOLVED,
	ASYNC_STATUS_FAILED
} async_status;

struct _async_op {
	/* One of the ASYNC_STATUS_ constants. */
	async_status status;
	
	/* Callback being used to continue the suspended execution. */
	void (* callback)(async_op *op);
	
	/* Callback being used to mark the operation as cancelled and continue the suspended execution. */
	async_cancel_cb cancel;
	
	/* Opaque pointer that can be used to pass data to the continuation callback. */
	void *arg;
	
	/* Result variable, will hold an error object if an operation fails or is cancelled. */
	zval result;
	
	/* Combined ASYNC_OP flags. */
	uint8_t flags;
	
	/* Refers to an operation queue if the operation is queued for execution. */
	async_op_queue *q;
	async_op *next;
	async_op *prev;
};

typedef struct {
	/* Async operation structure, must be first element to allow for casting to async_op. */
	async_op base;
	
	/* Result status code provided by libuv. */
	int code;
} async_uv_op;

struct _async_cancellation_handler {
	/* PHP object handle. */
	zend_object std;

	/* Cancellable context instance. */
	async_context *context;

	/* Task scheduler instance (only != NULL if timeout is active). */
	async_task_scheduler *scheduler;

	/* Timeout instance (watcher is never referenced within libuv). */
	uv_timer_t timer;

	/* Error that caused cancellation, UNDEF by default. */
	zval error;

	/* Chain handler that connects the cancel handler to the parent handler. */
	async_cancel_cb chain;

	/* Linked list of cancellation callbacks. */
	async_cancel_queue callbacks;
};

struct _async_cancellation_token {
	/* PHP object handle. */
	zend_object std;

	/* The context being observed for cancellation. */
	async_context *context;
};

#define ASYNC_CHANNEL_FLAG_CLOSED 1

struct _async_channel {
	/* PHP object handle. */
	zend_object std;
	
	/* Channel flags. */
	uint8_t flags;
	
	/* reference to the task scheduler. */
	async_task_scheduler *scheduler;
	
	/* Error object that has been passed as close reason. */
	zval error;
	
	/* Shutdown callback registered with the scheduler. */
	async_cancel_cb cancel;
	
	/* Pending send operations. */
	async_op_queue senders;
	
	/* Pending receive operations. */
	async_op_queue receivers;
	
	/* Maximum channel buffer size. */
	uint32_t size;
	
	/* Current channel buffer size. */
	uint32_t buffered;
	
	/* Queue of buffered messages. */
	async_channel_buffer_queue buffer;
};

struct _async_channel_buffer {
	/* The value to be sent. */
	zval value;
	
	/* References to previous and next buffer value. */
	async_channel_buffer *prev;
	async_channel_buffer *next;
};

typedef struct {
	/* Base async op data. */
	async_op base;
	
	/* Wrapped channel iterator. */
	async_channel_iterator *it;
	
	/* Key being used to register the iterator with the channel group. */
	zval key;
} async_channel_select_entry;

typedef struct {
	/* base async op data. */
	async_op base;
	
	/* Number of pending select operations, needed to stop select if all channels are closed. */
	uint32_t pending;
	
	/* refers to the registration of the iterator that completed the select. */
	async_channel_select_entry *entry;
} async_channel_select_op;

#define ASYNC_CHANNEL_GROUP_FLAG_SHUFFLE 1

struct _async_channel_group {
	/* PHP object handle. */
	zend_object std;
	
	/* griup flags. */
	uint8_t flags;
	
	/* Reference to the task scheudler. */
	async_task_scheduler *scheduler;
	
	/* Number of (supposedly) unclosed channel iterators. */
	uint32_t count;
	
	/* Array of registered channel iterators (closed channels will be removed without leaving gaps). */
	async_channel_select_entry *entries;
	
	/* Basic select operation being used to suspend the calling task. */
	async_channel_select_op select;
	
	/* Timeout paramter, -1 when a blocking call is requested. */
	zend_long timeout;
	
	/* Timer being used to stop select, only initialized if timeout > 0. */
	uv_timer_t timer;
};

#define ASYNC_CHANNEL_ITERATOR_FLAG_FETCHING 1

struct _async_channel_iterator {
	/* PHP object handle. */
	zend_object std;
	
	/* Iterator flags. */
	uint8_t flags;
	
	/* Reference to the channel being iterated. */
	async_channel *channel;
	
	/* Current key (ascending counter). */
	zend_long pos;
	
	/* Current entry. */
	zval entry;
	
	/* Reused receive operation. */
	async_op op;
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

	/* Refers to the contextual cancellation handler. */
	async_cancellation_handler *cancel;
};

struct _async_context_var {
	/* PHP object handle. */
	zend_object std;
};

struct _async_deferred {
	/* PHP object handle. */
	zend_object std;

	/* Refers to the deferred state being shared by deferred and awaitable. */
	async_deferred_state *state;

	/* Inlined cancellation handler called during contextual cancellation. */
	async_cancel_cb cancel;

	/* Function call info & cache of the cancel callback. */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
};

struct _async_deferred_awaitable {
	/* PHP object handle. */
	zend_object std;

	/* Refers to the deferred state being shared by deferred and awaitable. */
	async_deferred_state *state;
};

struct _async_deferred_state {
	/* Deferred status, one of PENDING, RESOLVED or FAILED. */
	zend_uchar status;

	/* Internal refcount being used to control deferred lifecycle. */
	uint32_t refcount;

	/* Holds the result value or error. */
	zval result;
	
	/* Reference to the task scheduler. */
	async_task_scheduler *scheduler;

	/* Associated async context. */
	async_context *context;

	/* Queue of waiting async operations. */
	async_op_queue operations;
	
	/* Cancel callback being called when the task scheduler is disposed. */
	async_cancel_cb cancel;
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
	void (* func)(async_fiber *fiber);

	/* Destination for a PHP value being passed into or returned from the fiber. */
	zval *value;

	/* Current Zend VM state within the fiber. */
	async_vm_state state;

	/* Data to be displayed as debug info. */
	zend_string *file;
	size_t line;
};

typedef struct {
	/* Base pointer being used to allocate and free buffer memory. */
	char *base;
	
	/* Current read position. */
	char *rpos;
	
	/* Current write position. */
	char *wpos;
	
	/* Buffer size. */
	size_t size;
	
	/* Number of buffered bytes. */
	size_t len;
} async_ring_buffer;

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
	
	/* Queued operations waiting for task completion. */
	async_op_queue operations;
	
	/* Current await operation. */
	async_op op;
};

#define ASYNC_TASK_SCHEDULER_FLAG_RUNNING 1
#define ASYNC_TASK_SCHEDULER_FLAG_DISPOSED (1 << 1)
#define ASYNC_TASK_SCHEDULER_FLAG_NOWAIT (1 << 2)

struct _async_task_scheduler {
	/* PHP object handle. */
	zend_object std;
	
	/* Flags being used to control scheduler state. */
	uint16_t flags;

	/* Error object to be used for disposal. */
	zval error;

	/* Tasks ready to be started or resumed. */
	async_task_queue ready;
	
	/* Pending operations that have not completed yet. */
	async_op_queue operations;
	
	/* Root level awaited operation. */
	async_op op;
	
	/** Queue of shutdown callbacks that need to be executed when the scheduler is disposed. */
	async_cancel_queue shutdown;

	/* Libuv event loop. */
	uv_loop_t loop;

	/* Idle handler being used to dispatch tasks from within a running event loop. */
	uv_idle_t idle;
	
	/* Timer being used to keep the loop busy when needed. */
	uv_timer_t busy;
	zend_ulong busy_count;
	
	async_fiber_context fiber;
	async_fiber_context current;
	async_fiber_context caller;
};

char *async_status_label(zend_uchar status);

ASYNC_API async_context *async_context_get();
ASYNC_API async_task_scheduler *async_task_scheduler_get();

ASYNC_API int async_await_op(async_op *op);
ASYNC_API void async_dispose_ops(async_op_queue *q);

ASYNC_API size_t async_ring_buffer_read_len(async_ring_buffer *buffer);
ASYNC_API size_t async_ring_buffer_write_len(async_ring_buffer *buffer);
ASYNC_API size_t async_ring_buffer_read(async_ring_buffer *buffer, char *base, size_t len);
ASYNC_API size_t async_ring_buffer_read_string(async_ring_buffer *buffer, zend_string **str, size_t len);
ASYNC_API void async_ring_buffer_consume(async_ring_buffer *buffer, size_t len);
ASYNC_API void async_ring_buffer_write_move(async_ring_buffer *buffer, size_t offset);

ASYNC_API int async_dns_lookup_ipv4(char *name, struct sockaddr_in *dest, int proto);
ASYNC_API int async_dns_lookup_ipv6(char *name, struct sockaddr_in6 *dest, int proto);


ZEND_BEGIN_MODULE_GLOBALS(async)
	/* Root fiber context (main thread). */
	async_fiber_context root;

	async_fiber_context active_context;

	/* Active fiber, NULL when in main thread. */
	async_fiber *current_fiber;

	/* Root context. */
	async_context *context;

	/* Active task context. */
	async_context *current_context;

	/* Fallback root task scheduler. */
	async_task_scheduler *scheduler;

	/* Running task scheduler. */
	async_task_scheduler *current_scheduler;

	/* Error to be thrown into a fiber (will be populated by throw()). */
	zval *error;

	/* Default fiber C stack size. */
	zend_long stack_size;
	
	/* INI settings. */
	zend_bool dns_enabled;
	zend_bool fs_enabled;
	zend_bool tcp_enabled;
	zend_bool timer_enabled;
	zend_bool udp_enabled;

ZEND_END_MODULE_GLOBALS(async)

ASYNC_API ZEND_EXTERN_MODULE_GLOBALS(async)
#define ASYNC_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(async, v)

#if defined(ZTS) && defined(COMPILE_DL_ASYNC)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#define ASYNC_ALLOC_OP(op) do { \
	op = emalloc(sizeof(async_op)); \
	ZEND_SECURE_ZERO(op, sizeof(async_op)); \
} while (0)

#define ASYNC_ALLOC_CUSTOM_OP(op, size) do { \
	op = emalloc(size); \
	ZEND_SECURE_ZERO(op, size); \
} while (0)

#define ASYNC_FINISH_OP(op) do { \
	async_op *tmp; \
	tmp = (async_op *) op; \
	if (tmp->q != NULL) { \
		ASYNC_Q_DETACH(tmp->q, tmp); \
		tmp->q = NULL; \
	} \
	tmp->status = ASYNC_STATUS_RESOLVED; \
	tmp->callback(tmp); \
} while (0)

#define ASYNC_RESOLVE_OP(op, val) do { \
	async_op *tmp; \
	tmp = (async_op *) op; \
	if (tmp->q != NULL) { \
		ASYNC_Q_DETACH(tmp->q, tmp); \
		tmp->q = NULL; \
	} \
	ZVAL_COPY(&tmp->result, val); \
	tmp->status = ASYNC_STATUS_RESOLVED; \
	tmp->callback(tmp); \
} while (0)

#define ASYNC_FAIL_OP(op, error) do { \
	async_op *tmp; \
	tmp = (async_op *) op; \
	if (tmp->q != NULL) { \
		ASYNC_Q_DETACH(tmp->q, tmp); \
		tmp->q = NULL; \
	} \
	ZVAL_COPY(&tmp->result, error); \
	tmp->status = ASYNC_STATUS_FAILED; \
	tmp->callback(tmp); \
} while (0)

#define ASYNC_ENQUEUE_OP(queue, op) do { \
	async_op *tmp; \
	tmp = (async_op *) op; \
	tmp->q = queue; \
	ASYNC_Q_ENQUEUE(queue, tmp); \
} while (0)

#define ASYNC_DEQUEUE_OP(queue, op) do { \
	ASYNC_Q_DEQUEUE(queue, op); \
	op->q = NULL; \
} while (0)

#define ASYNC_DEQUEUE_CUSTOM_OP(queue, op, type) do { \
	async_op *tmp; \
	ASYNC_Q_DEQUEUE(queue, tmp); \
	tmp->q = NULL; \
	op = (type *) tmp; \
} while (0)

#define ASYNC_FREE_OP(op) do { \
	async_op *tmp; \
	tmp = (async_op *) op; \
	if (tmp->q != NULL) { \
		ASYNC_Q_DETACH(tmp->q, tmp); \
		tmp->q = NULL; \
	} \
	zval_ptr_dtor(&tmp->result); \
	efree(op); \
} while (0)

#define ASYNC_FORWARD_OP_ERROR(op) do { \
	Z_ADDREF_P(&((async_op *) op)->result); \
	EG(current_execute_data)->opline--; \
	zend_throw_exception_internal(&((async_op *) op)->result); \
	EG(current_execute_data)->opline++; \
} while (0)

#define ASYNC_UNREF_ENTER(ctx, obj) do { \
	if (!(ctx)->background) { \
		if (++(obj)->ref_count == 1) { \
			uv_ref((uv_handle_t *) &(obj)->handle); \
		} \
	} \
} while (0)

#define ASYNC_UNREF_EXIT(ctx, obj) do { \
	if (!(ctx)->background) { \
		if (--(obj)->ref_count == 0) { \
			uv_unref((uv_handle_t *) &(obj)->handle); \
		} \
	} \
} while (0)

#define ASYNC_BUSY_ENTER(scheduler) do { \
	if (++(scheduler)->busy_count == 1) { \
		uv_ref((uv_handle_t *) &(scheduler)->busy); \
	} \
} while(0)

#define ASYNC_BUSY_EXIT(scheduler) do { \
	if (--(scheduler)->busy_count == 0) { \
		uv_unref((uv_handle_t *) &(scheduler)->busy); \
	} \
} while(0)

#define ASYNC_PREPARE_ERROR(error, message, ...) do { \
	zend_execute_data *exec; \
	zend_execute_data dummy; \
	zend_object *prev; \
	prev = EG(exception); \
	exec = EG(current_execute_data); \
	if (exec == NULL) { \
		memset(&dummy, 0, sizeof(zend_execute_data)); \
		EG(current_execute_data) = &dummy; \
	} \
	zend_throw_error(NULL, message ASYNC_VA_ARGS(__VA_ARGS__)); \
	ZVAL_OBJ(error, EG(exception)); \
	EG(current_execute_data) = exec; \
	EG(exception) = prev; \
} while (0)

#define ASYNC_PREPARE_EXCEPTION(error, ce, message, ...) do { \
	zend_execute_data *exec; \
	zend_execute_data dummy; \
	zend_object *prev; \
	prev = EG(exception); \
	exec = EG(current_execute_data); \
	if (exec == NULL) { \
		memset(&dummy, 0, sizeof(zend_execute_data)); \
		EG(current_execute_data) = &dummy; \
	} \
	zend_throw_exception_ex(ce, 0, message ASYNC_VA_ARGS(__VA_ARGS__)); \
	ZVAL_OBJ(error, EG(exception)); \
	EG(current_execute_data) = exec; \
	EG(exception) = prev; \
} while (0)

#define ASYNC_CHECK_ERROR(expr, message, ...) do { \
    if (UNEXPECTED(expr)) { \
    	zend_throw_error(NULL, message ASYNC_VA_ARGS(__VA_ARGS__)); \
    	return; \
    } \
} while (0)

#define ASYNC_CHECK_EXCEPTION(expr, ce, message, ...) do { \
    if (UNEXPECTED(expr)) { \
    	zend_throw_exception_ex(ce, 0, message ASYNC_VA_ARGS(__VA_ARGS__)); \
    	return; \
    } \
} while (0)

#define ASYNC_CHECK_FATAL(expr, message, ...) do { \
	if (UNEXPECTED(expr)) { \
		php_printf(message ASYNC_VA_ARGS(__VA_ARGS__)); \
		zend_error_noreturn(E_CORE_ERROR, message ASYNC_VA_ARGS(__VA_ARGS__)); \
	} \
} while (0)

#define ASYNC_RETURN_ON_ERROR() do { \
	if (UNEXPECTED(EG(exception))) { \
		return; \
	} \
} while (0)

#define ASYNC_ADDREF(obj) do { \
	/* php_printf("REF [%s]: %d -> %d / %s:%d\n", ZSTR_VAL((obj)->ce->name), (int) GC_REFCOUNT(obj), 1 + (int) GC_REFCOUNT(obj), __FILE__, __LINE__); */ \
	GC_ADDREF(obj); \
} while (0)

#define ASYNC_DELREF(obj) do { \
	/* php_printf("UNREF [%s]: %d -> %d / %s:%d\n", ZSTR_VAL((obj)->ce->name), (int) GC_REFCOUNT(obj), ((int) GC_REFCOUNT(obj)) - 1, __FILE__, __LINE__); */ \
	OBJ_RELEASE(obj); \
} while (0)

#define ASYNC_DEBUG_LOG(message, ...) do { \
	/* php_printf(message ASYNC_VA_ARGS(__VA_ARGS__)); */ \
} while (0)

/*
 * Queue macros require a "q" pointer with fields "first" and "last" of same pointer type as "v".
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
