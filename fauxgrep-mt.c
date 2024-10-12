// Setting _DEFAULT_SOURCE is necessary to activate visibility of
// certain header file contents on GNU/Linux systems.
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <err.h>

#include "job_queue.h"

// Mutex for synchronizing output to the console.
pthread_mutex_t stdout_mutex = PTHREAD_MUTEX_INITIALIZER;

// Struct to pass job queue and needle to worker threads.
struct worker_args {
    struct job_queue *job_queue;
    char const *needle;
};

// Search a single file for the given needle, and print any matches.
void fauxgrep_file(char const *needle, char const *path) {
    FILE *f = fopen(path, "r");

    if (f == NULL) {
        warn("failed to open %s", path);
        return;
    }

    char *line = NULL;
    size_t linelen = 0;
    int lineno = 1;

    while (getline(&line, &linelen, f) != -1) {
        // Remove the trailing newline if it exists.
        if (line[strlen(line) - 1] == '\n') {
            line[strlen(line) - 1] = '\0';
        }

        // Only print the line if it contains the search string.
        if (strstr(line, needle) != NULL) {
            pthread_mutex_lock(&stdout_mutex);
            printf("%s:%d: %s\n", path, lineno, line);  // Only print matched lines.
            pthread_mutex_unlock(&stdout_mutex);
        }
        lineno++;
    }

    free(line);
    fclose(f);
}


// Worker function that processes files from the job queue.
void* worker_thread(void *arg) {
    struct worker_args *args = arg;  // Use the struct to access both job_queue and needle
    struct job_queue *job_queue = args->job_queue;
    char const *needle = args->needle;  // Now the needle is available here

    while (1) {
        char *file_path;
        if (job_queue_pop(job_queue, (void**)&file_path) == 0) {
            printf("Worker thread processing file: %s\n", file_path);  // Debug output
            fauxgrep_file(needle, file_path);  // Use needle and file_path correctly
            free(file_path);  // Free the strdup-ed memory.
        } else {
            // Job queue is shutting down, exit the thread.
            break;
        }
    }

    return NULL;
}

int main(int argc, char * const *argv) {
    if (argc < 2) {
        err(1, "usage: [-n INT] STRING paths...");
        exit(1);
    }

    int num_threads = 1;
    char const *needle = argv[1];
    char * const *paths = &argv[2];

    // Check if the user specified the number of threads with the `-n` option.
    if (argc > 3 && strcmp(argv[1], "-n") == 0) {
        num_threads = atoi(argv[2]);
        if (num_threads < 1) {
            err(1, "invalid thread count: %s", argv[2]);
        }

        needle = argv[3];
        paths = &argv[4];
    } else {
        needle = argv[1];
        paths = &argv[2];
    }

    // Initialize the job queue.
    struct job_queue job_queue;
    job_queue_init(&job_queue, 64);

    // Create a struct for passing arguments to threads.
    struct worker_args args;
    args.job_queue = &job_queue;
    args.needle = needle;

    // Create worker threads.
    pthread_t *threads = calloc(num_threads, sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, worker_thread, &args) != 0) {
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
            printf("Adding file to job queue: %s\n", p->fts_path);  // Debug output to confirm files are added to the job queue
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
