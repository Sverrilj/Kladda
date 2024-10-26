// Setting _DEFAULT_SOURCE is necessary to activate visibility of
// certain header file contents on GNU/Linux systems.
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>

// err.h contains various nonstandard BSD extensions, but they are
// very handy.
#include <err.h>

#include <pthread.h>

#include "job_queue.h"

struct worker_args {
    struct job_queue *jq;
    const char *needle;
    pthread_mutex_t *print_mutex;
};

void *worker_thread_func(void *arg) {
    struct worker_args *args = (struct worker_args *)arg;
    struct job_queue *jq = args->jq;
    const char *needle = args->needle;
    pthread_mutex_t *print_mutex = args->print_mutex;

    while (1) {
        void *data;
        int res = job_queue_pop(jq, &data);
        if (res != 0) {
            // Job queue has been destroyed or an error occurred
            break;
        }
        if (data == NULL) {
            // NULL job indicates no more jobs will be added
            break;
        }
        char *path = (char *)data;

        FILE *f = fopen(path, "r");
        if (f == NULL) {
            warn("failed to open %s", path);
            free(path);
            continue;
        }

        char *line = NULL;
        size_t linelen = 0;
        int lineno = 1;

        while (getline(&line, &linelen, f) != -1) {
            if (strstr(line, needle) != NULL) {
                pthread_mutex_lock(print_mutex);
                printf("%s:%d: %s", path, lineno, line);
                pthread_mutex_unlock(print_mutex);
            }
            lineno++;
        }

        free(line);
        fclose(f);
        free(path);
    }

    free(args); // Free the arguments structure
    return NULL;
}

int main(int argc, char * const *argv) {
    if (argc < 2) {
        err(1, "usage: [-n INT] STRING paths...");
        exit(1);
    }

    int num_threads = 1;
    char const *needle;
    char * const *paths;

    if (argc > 3 && strcmp(argv[1], "-n") == 0) {
        num_threads = atoi(argv[2]);

        if (num_threads < 1) {
            err(1, "invalid thread count: %s", argv[2]);
        }

        if (argc < 5) {
            err(1, "usage: [-n INT] STRING paths...");
            exit(1);
        }

        needle = argv[3];
        paths = &argv[4];
    } else {
        needle = argv[1];
        paths = &argv[2];
    }

    // Initialize the job queue and worker threads here.
    struct job_queue jq;
    if (job_queue_init(&jq, 100) != 0) {
        err(1, "failed to initialize job queue");
    }

    pthread_mutex_t print_mutex;
    pthread_mutex_init(&print_mutex, NULL);

    pthread_t *threads = malloc(sizeof(pthread_t) * num_threads);
    if (threads == NULL) {
        err(1, "failed to allocate memory for threads");
    }

    for (int i = 0; i < num_threads; i++) {
        // Create a separate args struct for each thread
        struct worker_args *thread_args = malloc(sizeof(struct worker_args));
        if (thread_args == NULL) {
            err(1, "failed to allocate memory for thread arguments");
        }
        thread_args->jq = &jq;
        thread_args->needle = needle;
        thread_args->print_mutex = &print_mutex;

        if (pthread_create(&threads[i], NULL, worker_thread_func, thread_args) != 0) {
            err(1, "failed to create thread");
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

    FTSENT *p;
    while ((p = fts_read(ftsp)) != NULL) {
        switch (p->fts_info) {
        case FTS_D:
            break;
        case FTS_F: {
            // Process the file p->fts_path by pushing it onto the job queue.
            char *path_copy = strdup(p->fts_path);
            if (path_copy == NULL) {
                err(1, "strdup failed");
            }
            if (job_queue_push(&jq, path_copy) != 0) {
                err(1, "failed to push job onto queue");
            }
            break;
        }
        default:
            break;
        }
    }

    fts_close(ftsp);

    // Shut down the job queue and the worker threads here.
    // Signal the worker threads that there are no more jobs.
    for (int i = 0; i < num_threads; i++) {
        if (job_queue_push(&jq, NULL) != 0) {
            err(1, "failed to push NULL job onto queue");
        }
    }

    // Wait for all worker threads to finish.
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            err(1, "failed to join thread");
        }
    }

    // Clean up.
    job_queue_destroy(&jq);
    pthread_mutex_destroy(&print_mutex);
    free(threads);

    return 0;
}
