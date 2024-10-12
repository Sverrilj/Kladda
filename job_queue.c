#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "job_queue.h"

int job_queue_init(struct job_queue *job_queue, int capacity) {
    job_queue->buffer = malloc(sizeof(void*) * capacity);
    if (job_queue->buffer == NULL) {
        return -1; // Memory allocation failure.
    }

    job_queue->capacity = capacity;
    job_queue->count = 0;
    job_queue->head = 0;
    job_queue->tail = 0;

    pthread_mutex_init(&job_queue->lock, NULL);
    pthread_cond_init(&job_queue->not_full, NULL);
    pthread_cond_init(&job_queue->not_empty, NULL);

    return 0;
}

int job_queue_destroy(struct job_queue *job_queue) {
    pthread_mutex_lock(&job_queue->lock);

    // Wait for the queue to become empty.
    while (job_queue->count > 0) {
        pthread_cond_wait(&job_queue->not_empty, &job_queue->lock);
    }

    // Clean up.
    free(job_queue->buffer);
    pthread_mutex_destroy(&job_queue->lock);
    pthread_cond_destroy(&job_queue->not_full);
    pthread_cond_destroy(&job_queue->not_empty);

    pthread_mutex_unlock(&job_queue->lock);

    return 0;
}

int job_queue_push(struct job_queue *job_queue, void *data) {
    pthread_mutex_lock(&job_queue->lock);

    // Block if the queue is full.
    while (job_queue->count == job_queue->capacity) {
        pthread_cond_wait(&job_queue->not_full, &job_queue->lock);
    }

    // Add the job to the queue.
    job_queue->buffer[job_queue->tail] = data;
    job_queue->tail = (job_queue->tail + 1) % job_queue->capacity;
    job_queue->count++;

    // Signal that the queue is no longer empty.
    pthread_cond_signal(&job_queue->not_empty);
    pthread_mutex_unlock(&job_queue->lock);

    return 0;
}

int job_queue_pop(struct job_queue *job_queue, void **data) {
    pthread_mutex_lock(&job_queue->lock);

    // Block if the queue is empty.
    while (job_queue->count == 0) {
        pthread_cond_wait(&job_queue->not_empty, &job_queue->lock);
    }

    // Remove the job from the queue.
    *data = job_queue->buffer[job_queue->head];
    job_queue->head = (job_queue->head + 1) % job_queue->capacity;
    job_queue->count--;

    // Signal that the queue is no longer full.
    pthread_cond_signal(&job_queue->not_full);
    pthread_mutex_unlock(&job_queue->lock);

    return 0;
}
