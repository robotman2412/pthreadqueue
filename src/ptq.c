
#define PTQ_REVEAL_PRIVATE
#include "ptq.h"
#include <errno.h>
#include <stdio.h>

// Creates a queue with a length limit.
ptq_queue_t ptq_create_max(size_t entry_size, size_t max_length) {
	if (!max_length) max_length = 1;
	
	// Get memory for struct.
	ptq_queue_t queue = malloc(sizeof(struct ptq_queue));
	if (!queue) return NULL;
	
	// Fill out struct with empty queue.
	*queue = (struct ptq_queue) {
		.mutex        = PTHREAD_MUTEX_INITIALIZER,
		
		.head         = NULL,
		.tail         = NULL,
		
		.length       = 0,
		.max_length   = max_length,
		.entry_size   = entry_size,
	};
	
	// Initialise pthread mutex.
	int res = pthread_mutex_init(&queue->mutex, NULL);
	if (res) {
		free(queue);
		return NULL;
	}
	
	// Initialise semaphores: read semaphore.
	res = sem_init(&queue->read_sem, false, 0);
	if (res) {
		pthread_mutex_destroy(&queue->mutex);
		free(queue);
		return NULL;
	}
	
	// Initialise semaphores: write semaphore.
	res = sem_init(&queue->write_sem, false, 1);
	if (res) {
		pthread_mutex_destroy(&queue->mutex);
		sem_destroy(&queue->read_sem);
		free(queue);
		return NULL;
	}
	
	// Initialise semaphores: empty semaphore.
	res = sem_init(&queue->empty_sem, false, 1);
	if (res) {
		pthread_mutex_destroy(&queue->mutex);
		sem_destroy(&queue->read_sem);
		sem_destroy(&queue->write_sem);
		free(queue);
		return NULL;
	}
	
	return queue;
}

// Creates a queue without a length limit.
ptq_queue_t ptq_create(size_t entry_size) {
	// You can't really fill the address space completely ;)
	return ptq_create_max(entry_size, (size_t) -1);
}

// Destroys a queue.
// Thread-safe to concurrent read and/or write.
// Not thread-safe to use-after-free, which is up to the user.
void ptq_destroy(ptq_queue_t queue) {
	// Acquire the mutex.
	pthread_mutex_lock(&queue->mutex);
	
	// Clear the linked list.
	ptq_link_t *head = queue->head;
	while (head) {
		ptq_link_t *next = head->next;
		free(head->memory);
		free(head);
		head = next;
	}
	
	// Destroy mutex.
	pthread_mutex_unlock(&queue->mutex);
	pthread_mutex_destroy(&queue->mutex);
	
	// Destroy semaphores.
	sem_destroy(&queue->read_sem);
	sem_destroy(&queue->write_sem);
	sem_destroy(&queue->empty_sem);
	
	// Free memory.
	free(queue);
}

// Nonblocking write to the queue.
// Returns true if a write was performed, false otherwise.
bool ptq_send_nonblock(ptq_queue_t queue, void *object, pthread_mutex_t *mutex_to_lock) {
	
	// Await write space.
	int res = sem_trywait(&queue->write_sem);
	if (res) return false;
	
	// Acquire the mutex.
	res = pthread_mutex_lock(&queue->mutex);
	if (res) return false;
	
	// Add a new head link.
	ptq_link_t *next = malloc(sizeof(ptq_link_t));
	if (!next) {
		res = pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	*next = (ptq_link_t) {
		.prev   = NULL,
		.next   = queue->head,
		.memory = malloc(queue->entry_size),
	};
	
	// Copy the object.
	if (!next->memory) {
		free(next);
		res = pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	memcpy(next->memory, object, queue->entry_size);
	
	// Enlink it.
	if (queue->head) {
		queue->head->prev = next;
		queue->head = next;
	} else {
		queue->head = next;
		queue->tail = next;
	}
	queue->length ++;
	
	// Update semaphores.
	if (queue->length < queue->max_length) {
		// If length is not max, re-enable writes.
		sem_post(&queue->write_sem);
	}
	if (queue->length == 1) {
		// If length just became one, enable reads and disable empty.
		sem_trywait(&queue->empty_sem);
		sem_post(&queue->read_sem);
	}
	
	// Acquire the other mutex.
	if (mutex_to_lock) pthread_mutex_lock(mutex_to_lock);
	
	// Release the mutex.
	res = pthread_mutex_unlock(&queue->mutex);
	
	return true;
}

// Blocking write to the queue.
// Waits until the queue has space before returning.
bool ptq_send_block(ptq_queue_t queue, void *object, pthread_mutex_t *mutex_to_lock) {
	
	// Await write space.
	int res = sem_wait(&queue->write_sem);
	if (res) return false;
	
	// Acquire the mutex.
	res = pthread_mutex_lock(&queue->mutex);
	if (res) return false;
	
	// Add a new head link.
	ptq_link_t *next = malloc(sizeof(ptq_link_t));
	if (!next) {
		res = pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	*next = (ptq_link_t) {
		.prev   = NULL,
		.next   = queue->head,
		.memory = malloc(queue->entry_size),
	};
	
	// Copy the object.
	if (!next->memory) {
		free(next);
		res = pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	memcpy(next->memory, object, queue->entry_size);
	
	// Enlink it.
	if (queue->head) {
		queue->head->prev = next;
		queue->head = next;
	} else {
		queue->head = next;
		queue->tail = next;
	}
	queue->length ++;
	
	// Update semaphores.
	if (queue->length < queue->max_length) {
		// If length is not max, re-enable writes.
		sem_post(&queue->write_sem);
	}
	if (queue->length == 1) {
		// If length just became one, enable reads and disable empty.
		sem_trywait(&queue->empty_sem);
		sem_post(&queue->read_sem);
	}
	
	// Acquire the other mutex.
	if (mutex_to_lock) pthread_mutex_lock(mutex_to_lock);
	
	// Release the mutex.
	res = pthread_mutex_unlock(&queue->mutex);
	
	return true;
}

// Nonblocking read from the queue.
// Returns true if a read was performed, false otherwise.
bool ptq_receive_nonblock(ptq_queue_t queue, void *object, pthread_mutex_t *mutex_to_lock) {
	
	// Await read data.
	int res = sem_trywait(&queue->read_sem);
	if (res) return false;
	
	// Acquire the mutex.
	res = pthread_mutex_lock(&queue->mutex);
	if (res) return false;
	
	// Unlink the tail.
	ptq_link_t *tail = queue->tail;
	if (!tail) {
		res = pthread_mutex_unlock(&queue->mutex);
		return false;
	}
	if (tail->prev) {
		queue->tail = tail->prev;
		queue->tail->next  = NULL;
	} else {
		queue->head = NULL;
		queue->tail = NULL;
	}
	
	// Copy memory.
	memcpy(object, tail->memory, queue->entry_size);
	
	// Release memory.
	free(tail->memory);
	free(tail);
	queue->length --;
	
	// Update semaphores.
	if (queue->length) {
		// If length did not hit zero, re-enable reads.
		sem_post(&queue->read_sem);
	} else {
		// If length hit zero, enable empty semaphore.
		sem_post(&queue->empty_sem);
	}
	if (queue->length == queue->max_length - 1) {
		// If the queue just became small enough, enable writes.
		sem_post(&queue->write_sem);
	}
	
	// Acquire the other mutex.
	if (mutex_to_lock) pthread_mutex_lock(mutex_to_lock);
	
	// Release the mutex.
	res = pthread_mutex_unlock(&queue->mutex);
	
	return true;
}

// Nonblocking read from the queue.
// Waits until the queue has space before returning.
bool ptq_receive_block(ptq_queue_t queue, void *object, pthread_mutex_t *mutex_to_lock) {
	
	// Await read data.
	int res = sem_wait(&queue->read_sem);
	if (res) return false;
	
	// Acquire the read/write mutex.
	res = pthread_mutex_lock(&queue->mutex);
	if (res) return false;
	
	// Await the tail to APPEAR.
	ptq_link_t *tail = queue->tail;
	if (tail->prev) {
		queue->tail = tail->prev;
		queue->tail->next  = NULL;
	} else {
		queue->head = NULL;
		queue->tail = NULL;
	}
	
	// Copy memory.
	if (object) {
		memcpy(object, tail->memory, queue->entry_size);
	}
	
	// Release memory.
	free(tail->memory);
	free(tail);
	queue->length --;
	
	// Update semaphores.
	if (queue->length) {
		// If length did not hit zero, re-enable reads.
		sem_post(&queue->read_sem);
	} else {
		// If length hit zero, enable empty semaphore.
		sem_post(&queue->empty_sem);
	}
	if (queue->length == queue->max_length - 1) {
		// If the queue just became small enough, enable writes.
		sem_post(&queue->write_sem);
	}
	
	// Acquire the other mutex.
	if (mutex_to_lock) pthread_mutex_lock(mutex_to_lock);
	
	// Release the mutex.
	res = pthread_mutex_unlock(&queue->mutex);
	
	return true;
}

// Blocking wait for empty.
// Waits until the queue is completely empty before returning.
// Does not gaurantee that the queue remains empty; only waits for it to happen.
bool ptq_join(ptq_queue_t queue, pthread_mutex_t *mutex_to_lock) {
	
	// Await empty semaphore.
	int res = sem_wait(&queue->empty_sem);
	if (res) return false;
	
	// Acquire the read/write mutex.
	res = pthread_mutex_lock(&queue->mutex);
	if (res) return false;
	
	// Acquire the other mutex.
	if (mutex_to_lock) pthread_mutex_lock(mutex_to_lock);
	
	// If still empty, re-enable empty semaphore.
	if (!queue->length) {
		sem_post(&queue->empty_sem);
	}
	
	// Release the mutex.
	res = pthread_mutex_unlock(&queue->mutex);
	
	return true;
}

// Gets the length of the queue.
size_t ptq_get_length(ptq_queue_t queue, pthread_mutex_t *mutex_to_lock) {
	
	// Acquire the read/write mutex.
	int res = pthread_mutex_lock(&queue->mutex);
	if (res) return 0;
	
	// Acquire the other mutex.
	if (mutex_to_lock) pthread_mutex_lock(mutex_to_lock);
	
	size_t retval = queue->length;
	
	// Release the mutex.
	res = pthread_mutex_unlock(&queue->mutex);
	
	return retval;
}
