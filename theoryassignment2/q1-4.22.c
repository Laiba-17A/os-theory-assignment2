#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

double avg = 0;
int mn = 0, mx = 0;

typedef struct {
    int *arr;
    int n;
} data;

void *find_avg(void *arg) {
    data *d = (data *)arg;
    double sum = 0;

    for (int i = 0; i < d->n; i++)
        sum += d->arr[i];

    avg = sum / d->n;
    pthread_exit(NULL);
}

void *find_min(void *arg) {
    data *d = (data *)arg;
    mn = d->arr[0];

    for (int i = 1; i < d->n; i++)
        if (d->arr[i] < mn)
            mn = d->arr[i];

    pthread_exit(NULL);
}

void *find_max(void *arg) {
    data *d = (data *)arg;
    mx = d->arr[0];

    for (int i = 1; i < d->n; i++)
        if (d->arr[i] > mx)
            mx = d->arr[i];

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("enter numbers\n");
        return 1;
    }

    int n = argc - 1;
    int *arr = malloc(n * sizeof(int));

    for (int i = 0; i < n; i++)
        arr[i] = atoi(argv[i + 1]);

    data d = {arr, n};
    pthread_t t1, t2, t3;

    pthread_create(&t1, NULL, find_avg, &d);
    pthread_create(&t2, NULL, find_min, &d);
    pthread_create(&t3, NULL, find_max, &d);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);

    printf("average value is %.0f\n", avg);
    printf("minimum value is %d\n", mn);
    printf("maximum value is %d\n", mx);

    free(arr);
    return 0;
}
