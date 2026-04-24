#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

typedef enum {
    emergency = 0,
    low_fuel = 1,
    reg_land = 2,
    cargo = 3,
    reg_takeoff = 4
} ftype;

char *names[] = {
    "emergency",
    "low_fuel",
    "regular_landing",
    "cargo_takeoff",
    "regular_takeoff"
};

typedef struct {
    int id;
    ftype type;
    int pri;
    int dyn;
    time_t arr;
    int emg;
    int stop;
} flight;

#define max_f 500
#define runways 3

typedef struct {
    flight *arr[max_f];
    int size;
} pqueue;

typedef struct {
    int id;
    int busy;
    int maintain;
    flight *cur;
    pthread_mutex_t lock;
} runway;

pqueue pq;
runway rw[runways];

pthread_mutex_t pq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t em_lock = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t cv_f = PTHREAD_COND_INITIALIZER;
pthread_cond_t cv_e = PTHREAD_COND_INITIALIZER;

int running = 1;
int next_id = 1;
int em_wait = 0;

void push(flight *f) {
    int i = pq.size++;
    pq.arr[i] = f;

    while (i > 0) {
        int p = (i - 1) / 2;

        if (pq.arr[p]->dyn <= pq.arr[i]->dyn)
            break;

        flight *t = pq.arr[p];
        pq.arr[p] = pq.arr[i];
        pq.arr[i] = t;
        i = p;
    }
}

flight *pop() {
    if (pq.size == 0)
        return NULL;

    flight *top = pq.arr[0];
    pq.arr[0] = pq.arr[--pq.size];

    int i = 0;

    while (1) {
        int l = 2 * i + 1;
        int r = 2 * i + 2;
        int b = i;

        if (l < pq.size && pq.arr[l]->dyn < pq.arr[b]->dyn)
            b = l;

        if (r < pq.size && pq.arr[r]->dyn < pq.arr[b]->dyn)
            b = r;

        if (b == i)
            break;

        flight *t = pq.arr[i];
        pq.arr[i] = pq.arr[b];
        pq.arr[b] = t;
        i = b;
    }

    return top;
}

void use_runway(runway *r, flight *f) {
    printf("runway %d handling flight %d %s\n",
           r->id, f->id, names[f->type]);

    usleep(1000000 + rand() % 1000000);

    printf("runway %d finished flight %d\n", r->id, f->id);

    free(f);
}

void *generator(void *arg) {
    int seq[] = {
        reg_land,
        reg_takeoff,
        low_fuel,
        cargo,
        reg_land,
        emergency,
        reg_land,
        reg_takeoff
    };

    int total = sizeof(seq) / sizeof(seq[0]);

    for (int i = 0; i < total; i++) {
        usleep(500000);

        flight *f = malloc(sizeof(flight));

        f->id = next_id++;
        f->type = seq[i];
        f->pri = seq[i];
        f->dyn = seq[i];
        f->arr = time(NULL);
        f->emg = (seq[i] == emergency);
        f->stop = 0;

        pthread_mutex_lock(&pq_lock);

        push(f);

        printf("flight %d %s added\n", f->id, names[f->type]);

        pthread_cond_broadcast(&cv_f);
        pthread_mutex_unlock(&pq_lock);

        if (f->emg) {
            pthread_mutex_lock(&em_lock);
            em_wait = 1;
            pthread_cond_signal(&cv_e);
            pthread_mutex_unlock(&em_lock);
        }
    }

    sleep(6);

    running = 0;

    pthread_mutex_lock(&pq_lock);
    pthread_cond_broadcast(&cv_f);
    pthread_mutex_unlock(&pq_lock);

    pthread_exit(NULL);
}

void *controller(void *arg) {
    int id = *(int *)arg;
    runway *r = &rw[id];

    while (running || pq.size > 0) {
        pthread_mutex_lock(&pq_lock);

        while (pq.size == 0 && running)
            pthread_cond_wait(&cv_f, &pq_lock);

        if (pq.size == 0) {
            pthread_mutex_unlock(&pq_lock);
            break;
        }

        flight *f = pop();

        pthread_mutex_unlock(&pq_lock);

        pthread_mutex_lock(&r->lock);

        if (r->maintain) {
            printf("runway %d under maintenance\n", r->id);

            pthread_mutex_unlock(&r->lock);

            pthread_mutex_lock(&pq_lock);
            push(f);
            pthread_mutex_unlock(&pq_lock);

            sleep(1);
            continue;
        }

        r->busy = 1;
        r->cur = f;

        pthread_mutex_unlock(&r->lock);

        if (f->stop) {
            printf("flight %d stopped for emergency\n", f->id);

            pthread_mutex_lock(&r->lock);
            r->busy = 0;
            r->cur = NULL;
            pthread_mutex_unlock(&r->lock);

            pthread_mutex_lock(&pq_lock);
            push(f);
            pthread_mutex_unlock(&pq_lock);

            continue;
        }

        use_runway(r, f);

        pthread_mutex_lock(&r->lock);
        r->busy = 0;
        r->cur = NULL;
        pthread_mutex_unlock(&r->lock);
    }

    pthread_exit(NULL);
}

void *monitor(void *arg) {
    while (running) {
        pthread_mutex_lock(&em_lock);

        while (!em_wait && running)
            pthread_cond_wait(&cv_e, &em_lock);

        if (!running) {
            pthread_mutex_unlock(&em_lock);
            break;
        }

        printf("emergency flight detected\n");

        for (int i = 0; i < runways; i++) {
            pthread_mutex_lock(&rw[i].lock);

            if (rw[i].cur && rw[i].cur->type != emergency) {
                rw[i].cur->stop = 1;
                printf("flight %d marked for stop\n", rw[i].cur->id);
            }

            pthread_mutex_unlock(&rw[i].lock);
        }

        em_wait = 0;

        pthread_mutex_unlock(&em_lock);
    }

    pthread_exit(NULL);
}

int main() {
    srand(time(NULL));

    printf("airport runway control system\n");

    for (int i = 0; i < runways; i++) {
        rw[i].id = i + 1;
        rw[i].busy = 0;
        rw[i].maintain = 0;
        rw[i].cur = NULL;

        pthread_mutex_init(&rw[i].lock, NULL);

        printf("runway %d ready\n", rw[i].id);
    }

    pthread_t gen, mon, con[runways];
    int ids[runways];

    pthread_create(&gen, NULL, generator, NULL);
    pthread_create(&mon, NULL, monitor, NULL);

    for (int i = 0; i < runways; i++) {
        ids[i] = i;
        pthread_create(&con[i], NULL, controller, &ids[i]);
    }

    pthread_join(gen, NULL);

    for (int i = 0; i < runways; i++)
        pthread_join(con[i], NULL);

    pthread_cancel(mon);
    pthread_join(mon, NULL);

    for (int i = 0; i < runways; i++)
        pthread_mutex_destroy(&rw[i].lock);

    printf("all flights processed\n");

    return 0;
}
