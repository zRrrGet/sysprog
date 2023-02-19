#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <stdint.h>
#include <ctype.h>
#include "libcoro.h"

#define MAX_FILE_NUM 32
#define MAX_NUM_OF_INTEGERS_PER_FILE 200000
#define DEFAULT_COROUTINE_COUNT (-1)
#define DEFAULT_LATENCY 0

typedef struct {
    int *array;
    int count;
} Array_t;

static int g_coroutineCount = DEFAULT_COROUTINE_COUNT;
static int g_targetLatency = DEFAULT_LATENCY;

static struct {
    Array_t contents[MAX_FILE_NUM];
    int numOfContents;
    int availableContentInd;
} g_filePool = {0};

static void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}
  
static int partition(int *arr, int l, int h)
{
    int x = arr[h];
    int i = (l - 1);
  
    for (int j = l; j <= h - 1; j++) {
        if (arr[j] <= x) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }
    swap(&arr[i + 1], &arr[h]);
    return (i + 1);
}

static uint64_t getTimeInMicroSec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

static void yieldDecide(uint64_t *startTime, uint64_t *totalTime) {
    uint64_t timeInMicroSec = getTimeInMicroSec();
    if (timeInMicroSec - *startTime >= g_targetLatency / g_coroutineCount) {
        *totalTime += getTimeInMicroSec() - *startTime;
        coro_yield();
        *startTime = getTimeInMicroSec();
    }
}

static void quickSortIterative(int *arr, int l, int h, uint64_t *startTime, uint64_t *totalTime)
{
    int stack[h - l + 1];
    int top = -1;
  
    stack[++top] = l;
    stack[++top] = h;
  
    while (top >= 0) {
        h = stack[top--];
        l = stack[top--];
  
        int p = partition(arr, l, h);
        if (p - 1 > l) {
            stack[++top] = l;
            stack[++top] = p - 1;
        }
        if (p + 1 < h) {
            stack[++top] = p + 1;
            stack[++top] = h;
        }

        yieldDecide(startTime, totalTime);
    }
}

// free args in there
static int sortCoroed(void *voidArgs)
{
    uint64_t startTime = getTimeInMicroSec(), totalTime = 0;
    char *name = voidArgs;
    struct coro *this = coro_this();
    while (g_filePool.availableContentInd < g_filePool.numOfContents) {
        Array_t currentContent = g_filePool.contents[g_filePool.availableContentInd];
        ++g_filePool.availableContentInd;
        quickSortIterative(currentContent.array, 0, currentContent.count-1, &startTime, &totalTime);
    }
    totalTime += getTimeInMicroSec() - startTime;
    printf("%s: switch count %lld, total time in ms %ld\n", name, coro_switch_count(this), totalTime);
    free(voidArgs);
}

static int parseArgs(int argc, char **argv)
{
    char c;
    while ((c = getopt(argc, argv, "c:l:")) != -1) {
        switch (c) {
            case 'c':
                g_coroutineCount = atoi(optarg);
                printf("Coroutine count is %d\n", g_coroutineCount);
                if (g_coroutineCount <= 0) {
                    fprintf(stderr, "Too low coroutine count.\n");
                    return 1;
                }
                break;
            case 'l':
                g_targetLatency = atoi(optarg);
                printf("Target latency is %d\n", g_targetLatency);
                break;
            case '?':
                if (optopt == 'c' || optopt == 'l') {
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                }
                else if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option `-%c'.\n", optopt);
                }
                else {
                    fprintf(stderr,"Unknown option character `\\x%x'.\n", optopt);
                }
                return 1;
            default:
                abort();
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    uint64_t startTime = getTimeInMicroSec();

    if (parseArgs(argc, argv) != 0) {
        return 1;
    }

    coro_sched_init();

    // reading each file and filling the pool
    for (int i = optind; i < argc && i < MAX_FILE_NUM; ++i, ++g_filePool.numOfContents) {
        printf("File input: %s\n", argv[i]);

        FILE *file = fopen(argv[i], "r");
        if (file == NULL) {
            fprintf(stderr, "Failed to open fail %s", argv[i]);
            continue;
        }
        g_filePool.contents[g_filePool.numOfContents].array = malloc(MAX_NUM_OF_INTEGERS_PER_FILE * sizeof(int));
        int numCount = 0;
        for (; numCount < MAX_NUM_OF_INTEGERS_PER_FILE; ++numCount) {
            int number;
            if (fscanf(file, "%d", &number) != 1) {
                break;
            }
            g_filePool.contents[g_filePool.numOfContents].array[numCount] = number;
        }
        fclose(file);
        g_filePool.contents[g_filePool.numOfContents].count = numCount;
    }

    if (g_coroutineCount == DEFAULT_COROUTINE_COUNT) {
        g_coroutineCount = g_filePool.numOfContents;
    }

    // creating coros
    for (int i = 0; i < g_coroutineCount; ++i) {
        char name[16];
		sprintf(name, "sortCoroed_%d", i);
        coro_new(sortCoroed, strdup(name));
    }

    // waiting for coros to finish
    struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		coro_delete(c);
	}

    // merging sorted arrays
    int indeces[MAX_FILE_NUM] = {0};
    FILE *file = fopen("result", "w");
    while (true) {
        bool moreNumsToGo = false;
        for (int i = 0, flag = 0; i < g_filePool.numOfContents && i < MAX_FILE_NUM; ++i) {
            if (indeces[i] < g_filePool.contents[i].count) {
                moreNumsToGo = true;
                break;
            } else {
                free(g_filePool.contents[i].array);
                g_filePool.contents[i].array = NULL;
            }
        }
        if (!moreNumsToGo) {
            break;
        }
        int minInd = -1;
        for (int i = 0; i < g_filePool.numOfContents; ++i) {
            // existential horrors here
            if ((minInd == -1 && indeces[i] < g_filePool.contents[i].count) 
                || (indeces[i] < g_filePool.contents[i].count && g_filePool.contents[i].array[indeces[i]] < g_filePool.contents[minInd].array[indeces[minInd]])) {
                minInd = i;
            }
        }
        fprintf(file, "%d ", g_filePool.contents[minInd].array[indeces[minInd]]);
        ++indeces[minInd];
    }
    fclose(file);

    uint64_t endTime = getTimeInMicroSec();
    printf("Total work time in ms: %ld\n", endTime - startTime);
	return 0;
}
