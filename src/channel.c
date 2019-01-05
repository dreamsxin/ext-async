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
#include "zend_inheritance.h"

#include "ext/standard/php_mt_rand.h"

zend_class_entry *async_channel_ce;
zend_class_entry *async_channel_closed_exception_ce;
zend_class_entry *async_channel_group_ce;
zend_class_entry *async_channel_iterator_ce;

static zend_object_handlers async_channel_handlers;
static zend_object_handlers async_channel_group_handlers;
static zend_object_handlers async_channel_iterator_handlers;

static async_channel_iterator *async_channel_iterator_object_create(async_channel *channel);

#define ASYNC_CHANNEL_READABLE_NONBLOCK(channel) ((channel)->receivers.first != NULL || (channel)->buffer.first != NULL)
#define ASYNC_CHANNEL_READABLE(channel) (!((channel)->flags & ASYNC_CHANNEL_FLAG_CLOSED) || ASYNC_CHANNEL_READABLE_NONBLOCK(channel))

typedef struct {
	async_op base;
	zval* value;
} async_channel_send_op;

static inline void forward_error(zval *cause)
{
	zval error;
	
	ASYNC_PREPARE_EXCEPTION(&error, async_channel_closed_exception_ce, "Channel has been closed");

	zend_exception_set_previous(Z_OBJ_P(&error), Z_OBJ_P(cause));
	Z_ADDREF_P(cause);
	
	EG(current_execute_data)->opline--;
	zend_throw_exception_internal(&error);
	EG(current_execute_data)->opline++;
}

static inline int fetch_noblock(async_channel *channel, zval *entry)
{
	async_channel_buffer *buffer;
	async_channel_send_op *send;

	if (channel->buffer.first != NULL) {
		ASYNC_Q_DEQUEUE(&channel->buffer, buffer);
		
		ZVAL_COPY(entry, &buffer->value);
		zval_ptr_dtor(&buffer->value);
		
		efree(buffer);
		
		// Release first pending send operation into the channel's buffer queue.
		if (channel->senders.first != NULL) {
			ASYNC_DEQUEUE_CUSTOM_OP(&channel->senders, send, async_channel_send_op);
			
			buffer = emalloc(sizeof(async_channel_buffer));
			
			ZVAL_COPY(&buffer->value, send->value);

			ASYNC_Q_ENQUEUE(&channel->buffer, buffer);
			
			ASYNC_FINISH_OP(send);
		} else {
			channel->buffered--;
		}
		
		return SUCCESS;
	}
	
	// Grab next message the first pending send operation.
	if (channel->senders.first != NULL) {
		ASYNC_DEQUEUE_CUSTOM_OP(&channel->senders, send, async_channel_send_op);
		
		ZVAL_COPY(entry, send->value);
		
		ASYNC_FINISH_OP(send);
		
		return SUCCESS;
	}
	
	return FAILURE;
}

static void dispose_channel(void *arg, zval *error)
{
	async_channel *channel;
	async_op *op;
	
	channel = (async_channel *) arg;
	
	ZEND_ASSERT(channel != NULL);
	
	channel->cancel.func = NULL;
	channel->flags |= ASYNC_CHANNEL_FLAG_CLOSED;
	
	if (Z_TYPE_P(&channel->error) == IS_UNDEF) {
		if (error != NULL) {
			ZVAL_COPY(&channel->error, error);
		}
	}
	
	while (channel->receivers.first != NULL) {
		ASYNC_DEQUEUE_OP(&channel->receivers, op);
		
		if (Z_TYPE_P(&channel->error) == IS_UNDEF) {
			ASYNC_FINISH_OP(op);
		} else {
			ASYNC_FAIL_OP(op, &channel->error);
		}
	}
	
	while (channel->senders.first != NULL) {
		ASYNC_DEQUEUE_OP(&channel->senders, op);
		
		if (Z_TYPE_P(&channel->error) == IS_UNDEF) {
			ASYNC_FINISH_OP(op);
		} else {
			ASYNC_FAIL_OP(op, &channel->error);
		}
	}
}


static zend_object *async_channel_object_create(zend_class_entry *ce)
{
	async_channel *channel;
	
	channel = emalloc(sizeof(async_channel));
	ZEND_SECURE_ZERO(channel, sizeof(async_channel));
	
	zend_object_std_init(&channel->std, ce);
	channel->std.handlers = &async_channel_handlers;
	
	channel->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&channel->scheduler->std);
	
	channel->cancel.object = channel;
	channel->cancel.func = dispose_channel;
	
	ASYNC_Q_ENQUEUE(&channel->scheduler->shutdown, &channel->cancel);
	
	return &channel->std;
}

static void async_channel_object_dtor(zend_object *object)
{
	async_channel *channel;
	
	channel = (async_channel *) object;
	
	if (channel->cancel.func != NULL) {
		ASYNC_Q_DETACH(&channel->scheduler->shutdown, &channel->cancel);
		
		channel->cancel.func(channel, NULL);
	}
}

static void async_channel_object_destroy(zend_object *object)
{
	async_channel *channel;
	async_channel_buffer *buffer;
	
	channel = (async_channel *) object;
	
	while (channel->buffer.first != NULL) {
		ASYNC_Q_DEQUEUE(&channel->buffer, buffer);
		
		zval_ptr_dtor(&buffer->value);
		
		efree(buffer);
	}
	
	zval_ptr_dtor(&channel->error);
	
	ASYNC_DELREF(&channel->scheduler->std);
	
	zend_object_std_dtor(&channel->std);
}

ZEND_METHOD(Channel, __construct)
{
	async_channel *channel;
	
	zend_long size;
	
	size = 0;
		
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(size)
	ZEND_PARSE_PARAMETERS_END();
	
	ASYNC_CHECK_ERROR(size < 0, "Channel buffer size must not be negative");
	
	channel = (async_channel *) Z_OBJ_P(getThis());
	
	channel->size = (uint32_t) size;
}

ZEND_METHOD(Channel, getIterator)
{
	async_channel *channel;
	async_channel_iterator *it;
	
	zval obj;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	channel = (async_channel *) Z_OBJ_P(getThis());
	
	it = async_channel_iterator_object_create(channel);
	
	ZVAL_OBJ(&obj, &it->std);
	
	RETURN_ZVAL(&obj, 1, 1);
}

ZEND_METHOD(Channel, close)
{
	async_channel *channel;
	
	zval *val;

	val = NULL;

	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	channel = (async_channel *) Z_OBJ_P(getThis());
	
	if (channel->cancel.func != NULL) {
		ASYNC_Q_DETACH(&channel->scheduler->shutdown, &channel->cancel);
		
		channel->cancel.func(channel, (val == NULL || Z_TYPE_P(val) == IS_NULL) ? NULL : val);
	}
}

ZEND_METHOD(Channel, send)
{
	async_channel *channel;
	async_channel_buffer *buffer;
	async_channel_send_op *send;
	async_op *op;
	
	zval *val;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 1)
		Z_PARAM_ZVAL(val)
	ZEND_PARSE_PARAMETERS_END();
	
	channel = (async_channel *) Z_OBJ_P(getThis());
	
	if (Z_TYPE_P(&channel->error) != IS_UNDEF) {
		forward_error(&channel->error);
	
		return;
	}
	
	ASYNC_CHECK_EXCEPTION(channel->flags & ASYNC_CHANNEL_FLAG_CLOSED, async_channel_closed_exception_ce, "Channel has been closed");
	
	// Fast forward message to first waiting receiver.
	if (channel->receivers.first != NULL) {	
		ASYNC_DEQUEUE_OP(&channel->receivers, op);
		ASYNC_RESOLVE_OP(op, val);

		return;
	}
	
	// There is space in the channel's buffer, enqueue value and return.
	if (channel->buffered < channel->size) {
		buffer = emalloc(sizeof(async_channel_buffer));
		
		ZVAL_COPY(&buffer->value, val);
		
		ASYNC_Q_ENQUEUE(&channel->buffer, buffer);
		
		channel->buffered++;
		
		return;
	}
	
	// Allocate async operationd and queue it up.
	ASYNC_ALLOC_CUSTOM_OP(send, sizeof(async_channel_send_op));
	
	send->value = val;
	
	ASYNC_ENQUEUE_OP(&channel->senders, send);
	
	if (async_await_op((async_op *) send) == FAILURE) {
		ASYNC_FORWARD_OP_ERROR(send);
	}
	
	ASYNC_FREE_OP(send);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_channel_ctor, 0, 0, 0)
	ZEND_ARG_TYPE_INFO(0, capacity, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_channel_get_iterator, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_channel_close, 0, 0, IS_VOID, 0)
	ZEND_ARG_OBJ_INFO(0, error, Throwable, 1)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_channel_send, 0, 1, IS_VOID, 0)
	ZEND_ARG_INFO(0, message)
ZEND_END_ARG_INFO()

static const zend_function_entry channel_functions[] = {
	ZEND_ME(Channel, __construct, arginfo_channel_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(Channel, getIterator, arginfo_channel_get_iterator, ZEND_ACC_PUBLIC)
	ZEND_ME(Channel, close, arginfo_channel_close, ZEND_ACC_PUBLIC)
	ZEND_ME(Channel, send, arginfo_channel_send, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static zend_object *async_channel_group_object_create(zend_class_entry *ce)
{
	async_channel_group *group;
	
	group = emalloc(sizeof(async_channel_group));
	ZEND_SECURE_ZERO(group, sizeof(async_channel_group));
	
	zend_object_std_init(&group->std, ce);
	group->std.handlers = &async_channel_group_handlers;
	
	group->scheduler = async_task_scheduler_get();
	
	ASYNC_ADDREF(&group->scheduler->std);
	
	return &group->std;
}

static void dispose_group_timer(uv_handle_t *handle)
{
	async_channel_group *group;
	
	group = (async_channel_group *) handle->data;
	
	ASYNC_DELREF(&group->std);
}

static void async_channel_group_object_dtor(zend_object *object)
{
	async_channel_group *group;
	
	group = (async_channel_group *) object;
	
	if (group->timeout > 0 && !uv_is_closing((uv_handle_t *) &group->timer)) {
		ASYNC_ADDREF(&group->std);
		
		uv_close((uv_handle_t *) &group->timer, dispose_group_timer);
	}
}

static void async_channel_group_object_destroy(zend_object *object)
{
	async_channel_group *group;
	
	int i;
	
	group = (async_channel_group *) object;
	
	if (group->entries != NULL) {
		for (i = 0; i < group->count; i++) {
			ASYNC_DELREF(&group->entries[i].it->std);
			zval_ptr_dtor(&group->entries[i].key);
		}
	
		efree(group->entries);
	}
	
	ASYNC_DELREF(&group->scheduler->std);
	
	zend_object_std_dtor(&group->std);
}

ZEND_METHOD(ChannelGroup, __construct)
{
	async_channel_group *group;
	
	HashTable *map;
	zval *t;
	zval *entry;
	zval tmp;
	
	zend_long timeout;
	zend_long shuffle;
	zend_long h;
	zend_string *k;
	
	t = NULL;
	shuffle = 0;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 1, 3)
		Z_PARAM_ARRAY_HT(map)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(t)
		Z_PARAM_LONG(shuffle)
	ZEND_PARSE_PARAMETERS_END();
	
	group = (async_channel_group *) Z_OBJ_P(getThis());
	
	if (t == NULL || Z_TYPE_P(t) == IS_NULL) {
		timeout = -1;
	} else {
		timeout = Z_LVAL_P(t);
		
		ASYNC_CHECK_ERROR(timeout < 0, "Timeout must not be negative, use NULL to disable timeout");
		
		if (timeout > 0) {
			uv_timer_init(&group->scheduler->loop, &group->timer);
			uv_unref((uv_handle_t *) &group->timer);
			
			group->timer.data = group;
		}
	}
	
	if (shuffle) {
		group->flags |= ASYNC_CHANNEL_GROUP_FLAG_SHUFFLE;
	}
	
	group->timeout = timeout;
	group->entries = ecalloc(zend_array_count(map), sizeof(async_channel_select_entry));
	
	ZEND_HASH_FOREACH_KEY_VAL(map, h, k, entry) {
		if (Z_TYPE_P(entry) != IS_OBJECT) {
			zend_throw_error(NULL, "Select requires all inputs to be objects");
			return;
		}
		
		if (!instanceof_function(Z_OBJCE_P(entry), async_channel_iterator_ce)) {
			if (!instanceof_function(Z_OBJCE_P(entry), zend_ce_aggregate)) {
				zend_throw_error(NULL, "Select requires all inputs to be channel iterators or provide such an iterator via IteratorAggregate");
				return;
			}
			
			zend_call_method_with_0_params(entry, Z_OBJCE_P(entry), NULL, "getiterator", &tmp);
			
			if (!instanceof_function(Z_OBJCE_P(&tmp), async_channel_iterator_ce)) {
				zval_ptr_dtor(&tmp);
				
				zend_throw_error(NULL, "Aggregated iterator is not a channel iterator");
				return;
			}
			
			group->entries[group->count].it = (async_channel_iterator *) Z_OBJ_P(&tmp);
		} else {
			Z_ADDREF_P(entry);
			
			group->entries[group->count].it = (async_channel_iterator *) Z_OBJ_P(entry);
		}
		
		if (k == NULL) {
			ZVAL_LONG(&group->entries[group->count].key, h);
		} else {
			ZVAL_STR_COPY(&group->entries[group->count].key, k);
		}
		
		group->count++;
	} ZEND_HASH_FOREACH_END();
}

ZEND_METHOD(ChannelGroup, count)
{
	async_channel_group *group;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	group = (async_channel_group *) Z_OBJ_P(getThis());
	
	RETURN_LONG(group->count);
}

static void continue_select(async_op *op)
{
	async_channel_group *group;
	async_channel_select_entry *entry;
	
	group = (async_channel_group *) op->arg;
	entry = (async_channel_select_entry *) op;
	
	group->select.pending--;
	
	if (group->select.base.status != ASYNC_STATUS_RUNNING) {
		return;
	}
	
	if (op->status == ASYNC_STATUS_FAILED) {
		group->select.entry = entry;
		
		ASYNC_FAIL_OP(&group->select, &op->result);
		
		return;
	}
	
	if (entry->it->channel->flags & ASYNC_CHANNEL_FLAG_CLOSED) {
		if (group->select.pending == 0) {
			ASYNC_FINISH_OP(&group->select);
		}
		
		return;
	}
	
	group->select.entry = entry;
	
	ASYNC_RESOLVE_OP(&group->select, &op->result);
}

static void timeout_select(uv_timer_t *timer)
{
	async_channel_group *group;
	
	group = (async_channel_group *) timer->data;
	
	ASYNC_FINISH_OP(&group->select);
}

ZEND_METHOD(ChannelGroup, select)
{
	async_channel_group *group;
	async_channel_select_entry buf;
	async_channel_select_entry *entry;
	async_channel_select_entry *first;
	async_channel *channel;
	
	zval *val;
	zval tmp;
	
	int i;
	int j;
	
	val = NULL;
	
	ZEND_PARSE_PARAMETERS_START_EX(ZEND_PARSE_PARAMS_THROW, 0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL_DEREF(val);
	ZEND_PARSE_PARAMETERS_END();
	
	if (val != NULL) {
		zval_ptr_dtor(val);
		ZVAL_NULL(val);
	}
	
	group = (async_channel_group *) Z_OBJ_P(getThis());
	
	// Perform a Fisher–Yates shuffle to randomize the channel entries array.
	if (group->flags & ASYNC_CHANNEL_GROUP_FLAG_SHUFFLE) {
		for (i = group->count - 1; i > 0; i--) {
			j = php_mt_rand_common(0, i);
			buf = group->entries[i];
			
			group->entries[i] = group->entries[j];
			group->entries[j] = buf;
		}
	}
	
	for (first = NULL, i = 0; i < group->count; i++) {
		entry = &group->entries[i];
		channel = entry->it->channel;
		
		if (ASYNC_CHANNEL_READABLE_NONBLOCK(channel)) {
			if (first == NULL) {
				first = entry;
			}
			
			continue;
		}
		
		// Perform error forwarding and inline compaction of the group.
		if (channel->flags & ASYNC_CHANNEL_FLAG_CLOSED) {
			ASYNC_DELREF(&entry->it->std);
			zval_ptr_dtor(&entry->key);
		
			for (j = i + 1; j < group->count; j++) {
				group->entries[j - 1] = group->entries[j];
			}
			
			group->count--;
			i--;
			
			if (Z_TYPE_P(&channel->error) != IS_UNDEF) {
				forward_error(&channel->error);
				return;
			}
		}
	}
	
	// Perform a non-blocking select if a channel is ready.
	if (first != NULL && fetch_noblock(first->it->channel, &tmp) == SUCCESS) {
		if (val != NULL) {
			ZVAL_COPY(val, &tmp);
		}
		
		zval_ptr_dtor(&tmp);
		
		RETURN_ZVAL(&first->key, 1, 0);
	}
	
	// No more channels left or non-blocking select early return.
	if (group->count == 0 || group->timeout == 0) {
		return;
	}
	
	// Register select operations with input channels and start the race.
	group->select.base.status = ASYNC_STATUS_PENDING;
	group->select.base.flags = 0;
	
	group->select.pending = group->count;
	group->select.entry = NULL;
	
	for (i = 0; i < group->count; i++) {
		entry = &group->entries[i];
	
		entry->base.status = ASYNC_STATUS_RUNNING;
		entry->base.flags = 0;
		entry->base.callback = continue_select;
		entry->base.arg = group;
	
		ASYNC_ENQUEUE_OP(&entry->it->channel->receivers, entry);
	}
	
	if (group->timeout > 0) {
		uv_timer_start(&group->timer, timeout_select, group->timeout, 0);
	}
	
	if (async_await_op((async_op *) &group->select) == FAILURE) {
		forward_error(&group->select.base.result);
	}
	
	if (group->timeout > 0) {
		uv_timer_stop(&group->timer);
	}
	
	// Populate return values.
	if (EXPECTED(EG(exception) == NULL) && group->select.entry != NULL) {
		if (val != NULL) {
			ZVAL_COPY(val, &group->select.base.result);
		}
		
		if (USED_RET()) {
			ZVAL_COPY(return_value, &group->select.entry->key);
		}
	}
	
	// Cleanup pending operations.
	for (i = 0; i < group->count; i++) {
		entry = &group->entries[i];
		channel = entry->it->channel;
	
		zval_ptr_dtor(&entry->base.result);
		
		ASYNC_Q_DETACH(&channel->receivers, (async_op *) entry);
		
		if (channel->flags & ASYNC_CHANNEL_FLAG_CLOSED) {
			// Do not remove channels that were closed with an error unless the error is being forwarded.
			if (Z_TYPE_P(&channel->error) == IS_UNDEF || entry == group->select.entry) {
				ASYNC_DELREF(&entry->it->std);
				zval_ptr_dtor(&entry->key);
			
				for (j = i + 1; j < group->count; j++) {
					group->entries[j - 1] = group->entries[j];
				}
				
				group->count--;
				i--;
			}
		}
	}
	
	zval_ptr_dtor(&group->select.base.result);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_channel_group_ctor, 0, 0, 1)
	ZEND_ARG_TYPE_INFO(0, channels, IS_ARRAY, 0)
	ZEND_ARG_TYPE_INFO(0, timeout, IS_LONG, 1)
	ZEND_ARG_TYPE_INFO(0, shuffle, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_channel_group_count, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_channel_group_select, 0, 0, 0)
	ZEND_ARG_INFO(1, value)
ZEND_END_ARG_INFO()

static const zend_function_entry channel_group_functions[] = {
	ZEND_ME(ChannelGroup, __construct, arginfo_channel_group_ctor, ZEND_ACC_PUBLIC)
	ZEND_ME(ChannelGroup, count, arginfo_channel_group_count, ZEND_ACC_PUBLIC)
	ZEND_ME(ChannelGroup, select, arginfo_channel_group_select, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static void fetch_next_entry(async_channel_iterator *it)
{
	async_channel *channel;
	
	ASYNC_CHECK_ERROR(it->flags & ASYNC_CHANNEL_ITERATOR_FLAG_FETCHING, "Cannot advance iterator while already awaiting next channel value");
	
	channel = it->channel;
	
	if (fetch_noblock(channel, &it->entry) == SUCCESS) {
		it->pos++;
	
		return;
	}
	
	// Queue up receiver and mark the iterator as fetching next value.
	it->flags |= ASYNC_CHANNEL_ITERATOR_FLAG_FETCHING;
	
	it->op.status = ASYNC_STATUS_PENDING;
	it->op.flags = 0;
	
	ASYNC_ENQUEUE_OP(&channel->receivers, &it->op);
	
	if (async_await_op(&it->op) == FAILURE) {
		forward_error(&it->op.result);
	} else if (!(channel->flags & ASYNC_CHANNEL_FLAG_CLOSED)) {
		it->pos++;
		
		ZVAL_COPY(&it->entry, &it->op.result);
	}
	
	zval_ptr_dtor(&it->op.result);
	
	it->flags &= ~ASYNC_CHANNEL_ITERATOR_FLAG_FETCHING;
}

static async_channel_iterator *async_channel_iterator_object_create(async_channel *channel)
{
	async_channel_iterator *it;
	
	it = emalloc(sizeof(async_channel));
	ZEND_SECURE_ZERO(it, sizeof(async_channel));
	
	zend_object_std_init(&it->std, async_channel_iterator_ce);
	it->std.handlers = &async_channel_iterator_handlers;
	
	it->pos = -1;
	it->channel = channel;
	
	ASYNC_ADDREF(&channel->std);
	
	return it;
}

static void async_channel_iterator_object_destroy(zend_object *object)
{
	async_channel_iterator *it;
	
	it = (async_channel_iterator *) object;
	
	zval_ptr_dtor(&it->entry);
	
	ASYNC_DELREF(&it->channel->std);
	
	zend_object_std_dtor(&it->std);
}

ZEND_METHOD(ChannelIterator, rewind)
{
	async_channel_iterator *it;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	it = (async_channel_iterator *) Z_OBJ_P(getThis());
	
	zval_ptr_dtor(&it->entry);
	ZVAL_UNDEF(&it->entry);
	
	if (it->pos < 0 && ASYNC_CHANNEL_READABLE(it->channel)) {
		fetch_next_entry(it);
	} else if (Z_TYPE_P(&it->channel->error) != IS_UNDEF) {
		forward_error(&it->channel->error);
	}
}

ZEND_METHOD(ChannelIterator, valid)
{
	async_channel_iterator *it;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	it = (async_channel_iterator *) Z_OBJ_P(getThis());
	
	RETURN_BOOL(it->pos >= 0 && Z_TYPE_P(&it->entry) != IS_UNDEF);
}

ZEND_METHOD(ChannelIterator, current)
{
	async_channel_iterator *it;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	it = (async_channel_iterator *) Z_OBJ_P(getThis());
	
	if (it->pos < 0 && ASYNC_CHANNEL_READABLE(it->channel)) {
		fetch_next_entry(it);
	} else if (Z_TYPE_P(&it->channel->error) != IS_UNDEF) {
		forward_error(&it->channel->error);
	}
	
	if (Z_TYPE_P(&it->entry) != IS_UNDEF) {
		RETURN_ZVAL(&it->entry, 1, 0);
	}
}

ZEND_METHOD(ChannelIterator, key)
{
	async_channel_iterator *it;
	
	ZEND_PARSE_PARAMETERS_NONE();
	
	it = (async_channel_iterator *) Z_OBJ_P(getThis());
	
	if (it->pos < 0 && ASYNC_CHANNEL_READABLE(it->channel)) {
		fetch_next_entry(it);
	} else if (Z_TYPE_P(&it->channel->error) != IS_UNDEF) {
		forward_error(&it->channel->error);
	}
	
	if (it->pos >= 0 && Z_TYPE_P(&it->entry) != IS_UNDEF) {
		RETURN_LONG(it->pos);
	}
}

ZEND_METHOD(ChannelIterator, next)
{
	async_channel_iterator *it;

	ZEND_PARSE_PARAMETERS_NONE();
	
	it = (async_channel_iterator *) Z_OBJ_P(getThis());
	
	zval_ptr_dtor(&it->entry);
	ZVAL_UNDEF(&it->entry);
	
	if (ASYNC_CHANNEL_READABLE(it->channel)) {
		fetch_next_entry(it);
	} else if (Z_TYPE_P(&it->channel->error) != IS_UNDEF) {
		forward_error(&it->channel->error);
	}
}

ZEND_BEGIN_ARG_INFO(arginfo_channel_iterator_void, 0)
ZEND_END_ARG_INFO()

static const zend_function_entry channel_iterator_functions[] = {
	ZEND_ME(ChannelIterator, rewind, arginfo_channel_iterator_void, ZEND_ACC_PUBLIC)
	ZEND_ME(ChannelIterator, valid, arginfo_channel_iterator_void, ZEND_ACC_PUBLIC)
	ZEND_ME(ChannelIterator, current, arginfo_channel_iterator_void, ZEND_ACC_PUBLIC)
	ZEND_ME(ChannelIterator, key, arginfo_channel_iterator_void, ZEND_ACC_PUBLIC)
	ZEND_ME(ChannelIterator, next, arginfo_channel_iterator_void, ZEND_ACC_PUBLIC)
	ZEND_FE_END
};


static const zend_function_entry empty_funcs[] = {
	ZEND_FE_END
};


void async_channel_ce_register()
{
	zend_class_entry ce;

	INIT_CLASS_ENTRY(ce, "Concurrent\\Channel", channel_functions);
	async_channel_ce = zend_register_internal_class(&ce);
	async_channel_ce->ce_flags |= ZEND_ACC_FINAL;
	async_channel_ce->create_object = async_channel_object_create;
	async_channel_ce->serialize = zend_class_serialize_deny;
	async_channel_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_channel_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_channel_handlers.free_obj = async_channel_object_destroy;
	async_channel_handlers.dtor_obj = async_channel_object_dtor;
	async_channel_handlers.clone_obj = NULL;
	
	zend_class_implements(async_channel_ce, 1, zend_ce_aggregate);
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\ChannelGroup", channel_group_functions);
	async_channel_group_ce = zend_register_internal_class(&ce);
	async_channel_group_ce->ce_flags |= ZEND_ACC_FINAL;
	async_channel_group_ce->create_object = async_channel_group_object_create;
	async_channel_group_ce->serialize = zend_class_serialize_deny;
	async_channel_group_ce->unserialize = zend_class_unserialize_deny;
	
	memcpy(&async_channel_group_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_channel_group_handlers.free_obj = async_channel_group_object_destroy;
	async_channel_group_handlers.dtor_obj = async_channel_group_object_dtor;
	async_channel_group_handlers.clone_obj = NULL;
	
	zend_class_implements(async_channel_group_ce, 1, zend_ce_countable);
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\ChannelIterator", channel_iterator_functions);
	async_channel_iterator_ce = zend_register_internal_class(&ce);
	async_channel_iterator_ce->ce_flags |= ZEND_ACC_FINAL;
	async_channel_iterator_ce->serialize = zend_class_serialize_deny;
	async_channel_iterator_ce->unserialize = zend_class_unserialize_deny;

	memcpy(&async_channel_iterator_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	async_channel_iterator_handlers.free_obj = async_channel_iterator_object_destroy;
	async_channel_iterator_handlers.clone_obj = NULL;
	
	zend_class_implements(async_channel_iterator_ce, 1, zend_ce_iterator);
	
	INIT_CLASS_ENTRY(ce, "Concurrent\\ChannelClosedException", empty_funcs);
	async_channel_closed_exception_ce = zend_register_internal_class(&ce);

	zend_do_inheritance(async_channel_closed_exception_ce, zend_ce_exception);
}


/*
 * vim: sw=4 ts=4
 * vim600: fdm=marker
 */
