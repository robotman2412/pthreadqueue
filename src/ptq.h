
#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <string.h>

#ifdef PTQ_REVEAL_PRIVATE

// Complete definition in private API.

// One link of data in the queue.
typedef struct ptq_link ptq_link_t;

// One link of data in the queue.
struct ptq_link {
	// The previous link of the list.
	ptq_link_t *prev;
	// The next link of the list.
	ptq_link_t *next;
	// The memory of this link.
	void       *memory;
};

// Queue reference used to read or write.
struct ptq_queue {
	// Mutex that is aqcuired before reading/writing.
	pthread_mutex_t mutex;
	// Semaphore used to allow blocking reads.
	sem_t           read_sem;
	// Semaphore used to allow blocking writes.
	sem_t           write_sem;
	// Semaphore used to allow awaiting empty.
	sem_t           empty_sem;
	
	// Most recently added queue entry.
	ptq_link_t     *head;
	// Least recently added queue entry.
	ptq_link_t     *tail;
	
	// Current length of the queue.
	size_t          length;
	// Maximum length of the queue, if any.
	size_t          max_length;
	// The size of objects in the queue.
	size_t          entry_size;
};

// Queue reference used to read or write.
typedef struct ptq_queue *ptq_queue_t;

#else

// Very simple public API.

// Queue reference used to read or write.
typedef void *ptq_queue_t;

#endif

// Creates a queue with a length limit.
ptq_queue_t ptq_create_max(size_t entry_size, size_t max_length);

// Creates a queue without a length limit.
ptq_queue_t ptq_create(size_t entry_size);

// Destroys a queue.
// Not a thread-safe method.
void ptq_destroy(ptq_queue_t queue);

// Nonblocking write to the queue.
// Returns true if a write was performed, false otherwise.
// If not NULL, will also lock mutex_to_lock on success.
bool ptq_send_nonblock(ptq_queue_t to, void *object, pthread_mutex_t *mutex_to_lock);

// Blocking write to the queue.
// Waits until the queue has space before returning.
// If not NULL, will also lock mutex_to_lock on success.
bool ptq_send_block(ptq_queue_t to, void *object, pthread_mutex_t *mutex_to_lock);

// Nonblocking read from the queue.
// Returns true if a read was performed, false otherwise.
// If not NULL, will also lock mutex_to_lock on success.
bool ptq_receive_nonblock(ptq_queue_t from, void *object, pthread_mutex_t *mutex_to_lock);

// Nonblocking read from the queue.
// Waits until the queue has space before returning.
// If not NULL, will also lock mutex_to_lock on success.
bool ptq_receive_block(ptq_queue_t from, void *object, pthread_mutex_t *mutex_to_lock);

// Blocking wait for empty.
// Waits until the queue is completely empty before returning.
// Does not gaurantee that the queue remains empty; only waits for it to happen.
// If not NULL, will also lock mutex_to_lock on success.
bool ptq_join(ptq_queue_t queue, pthread_mutex_t *mutex_to_lock);

// Gets the length of the queue.
// If not NULL, will also lock mutex_to_lock on success.
size_t ptq_get_length(ptq_queue_t queue, pthread_mutex_t *mutex_to_lock);
