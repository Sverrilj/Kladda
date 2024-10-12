// Setting _DEFAULT_SOURCE is necessary to activate visibility of
// certain header file contents on GNU/Linux systems.
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <err.h>

#include "job_queue.h"
#include "histogram.h"

// Mutex for synchronizing access to global histogram and console.
pthread_mutex_t histogram_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global histogram (8 bits = 8 int elements).
int global_histogram[8] = { 0 };

// Worker thread function.
void* worker_thread(void *arg) {
    struct job_queue *job_queue = arg;  // Extract job queue from arguments.

    while (1) {
        char *file_path;
        if (job_queue_pop(job_queue, (void**)&file_path) == 0) {
            // Process file and compute local histogram.
            int local_histogram[8] = { 0 };
            FILE *f = fopen(file_path, "r");

            if (f == NULL) {
                warn("failed to open %s", file_path);
                free(file_path);
                continue;
            }

            char c;
            int i = 0;
            while (fread(&c, sizeof(c), 1, f) == 1) {
                i++;
                update_histogram(local_histogram, c);

                if (i % 100000 == 0) {
                    // Merge local histogram to global and print every 100,000 bytes.
                    pthread_mutex_lock(&histogram_mutex);
                    merge_histogram(local_histogram, global_histogram);
                    print_histogram(global_histogram);
                    pthread_mutex_unlock(&histogram_mutex);
                }
            }

            // Merge whatever remains in local histogram to global.
            pthread_mutex_lock(&histogram_mutex);
            merge_histogram(local_histogram, global_histogram);
            print_histogram(global_histogram);
            pthread_mutex_unlock(&histogram_mutex);

            fclose(f);
            free(file_path);
        } else {
            // If the job queue is empty, exit the thread.
            break;
        }
    }
    return NULL;
}

int main(int argc, char * const *argv) {
    if (argc < 2) {
        err(1, "usage: [-n INT] paths...");
        exit(1);
    }

    int num_threads = 1;
    char * const *paths = &argv[1];

    // Check if the user specified the number of threads with the `-n` option.
    if (argc > 3 && strcmp(argv[1], "-n") == 0) {
        num_threads = atoi(argv[2]);
        if (num_threads < 1) {
            err(1, "invalid thread count: %s", argv[2]);
        }

        paths = &argv[3];
    }

    // Initialize the job queue.
    struct job_queue job_queue;
    job_queue_init(&job_queue, 64);

    // Create worker threads.
    pthread_t *threads = calloc(num_threads, sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &job_queue) != 0) {
            err(1, "pthread_create() failed");
        }
    }

    // FTS_LOGICAL = follow symbolic links
    // FTS_NOCHDIR = do not change the working directory of the process
    int fts_options = FTS_LOGICAL | FTS_NOCHDIR;

    FTS *ftsp;
    if ((ftsp = fts_open(paths, fts_options, NULL)) == NULL) {
        err(1, "fts_open() failed");
        return -1;
    }

    // Traverse the directory tree and push file paths to the job queue.
    FTSENT *p;
    while ((p = fts_read(ftsp)) != NULL) {
        switch (p->fts_info) {
        case FTS_D:
            break;
        case FTS_F:
            job_queue_push(&job_queue, strdup(p->fts_path));  // Use strdup to duplicate the file path.
            break;
        default:
            break;
        }
    }

    // Close the directory traversal.
    fts_close(ftsp);

    // Destroy the job queue and wait for workers to finish.
    job_queue_destroy(&job_queue);

    // Join all worker threads.
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    free(threads);  // Clean up dynamically allocated thread array.

    return 0;
}
