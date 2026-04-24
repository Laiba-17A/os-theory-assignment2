#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define RUNWAYS 3
#define MAXQ 500

typedef enum {
    EMERGENCY=0,
    LOW_FUEL=1,
    LAND=2,
    CARGO=3,
    TAKEOFF=4
} type;

typedef struct flight {
    int id;
    type t;
    int pri;
    time_t arr;
    int emergency;
} flight;

typedef struct {
    flight *a[MAXQ];
    int size;
} pq_t;

typedef struct {
    int id;
    int busy;
    int maintenance;
    flight *cur;
    pthread_mutex_t lock;
} runway;

pq_t pq;
runway r[RUNWAYS];

pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t emlock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

int run = 1;
int idgen = 1;
int emflag = 0;

void push(flight *f) {
    int i = pq.size++;
    pq.a[i] = f;

    while (i > 0) {
        int p = (i-1)/2;
        if (pq.a[p]->pri <= pq.a[i]->pri) break;
        flight *t = pq.a[p];
        pq.a[p] = pq.a[i];
        pq.a[i] = t;
        i = p;
    }
}

flight* pop() {
    if (!pq.size) return NULL;

    flight *t = pq.a[0];
    pq.a[0] = pq.a[--pq.size];

    int i = 0;
    while (1) {
        int l=2*i+1, rgt=2*i+2, b=i;

        if (l<pq.size && pq.a[l]->pri < pq.a[b]->pri) b=l;
        if (rgt<pq.size && pq.a[rgt]->pri < pq.a[b]->pri) b=rgt;

        if (b==i) break;

        flight *tmp=pq.a[i];
        pq.a[i]=pq.a[b];
        pq.a[b]=tmp;
        i=b;
    }

    return t;
}

void* generator(void *arg) {
    int seq[] = {LAND, TAKEOFF, LOW_FUEL, CARGO, LAND, EMERGENCY, TAKEOFF};
    int n = sizeof(seq)/sizeof(seq[0]);

    for (int i=0;i<n;i++) {
        usleep(400000);

        flight *f = malloc(sizeof(flight));
        f->id = idgen++;
        f->t = seq[i];
        f->pri = seq[i];
        f->arr = time(NULL);
        f->emergency = (seq[i]==EMERGENCY);

        pthread_mutex_lock(&qlock);
        push(f);
        printf("flight %d added\n", f->id);
        pthread_cond_signal(&cv);
        pthread_mutex_unlock(&qlock);

        if (f->emergency) {
            pthread_mutex_lock(&emlock);
            emflag = 1;
            pthread_mutex_unlock(&emlock);
        }
    }

    run = 0;
    pthread_cond_broadcast(&cv);
    return NULL;
}

void* controller(void *arg) {
    int id = *(int*)arg;
    runway *rw = &r[id];

    while (run || pq.size) {
        pthread_mutex_lock(&qlock);

        while (!pq.size && run)
            pthread_cond_wait(&cv, &qlock);

        if (!pq.size) {
            pthread_mutex_unlock(&qlock);
            break;
        }

        flight *f = pop();
        pthread_mutex_unlock(&qlock);

        pthread_mutex_lock(&rw->lock);
        rw->busy = 1;
        rw->cur = f;
        pthread_mutex_unlock(&rw->lock);

        printf("runway %d handling flight %d\n", rw->id, f->id);
        usleep(800000);
        printf("runway %d done flight %d\n", rw->id, f->id);

        free(f);

        pthread_mutex_lock(&rw->lock);
        rw->busy = 0;
        rw->cur = NULL;
        pthread_mutex_unlock(&rw->lock);
    }

    return NULL;
}

void* monitor(void *arg) {
    while (run) {
        sleep(1);

        pthread_mutex_lock(&emlock);

        if (emflag) {
            printf("EMERGENCY detected!\n");
            emflag = 0;
        }

        pthread_mutex_unlock(&emlock);
    }

    return NULL;
}

int main() {
    pthread_t gen, mon, ct[RUNWAYS];
    int ids[RUNWAYS];

    for (int i=0;i<RUNWAYS;i++) {
        r[i].id = i+1;
        r[i].busy = 0;
        pthread_mutex_init(&r[i].lock,NULL);
    }

    pthread_create(&gen,NULL,generator,NULL);
    pthread_create(&mon,NULL,monitor,NULL);

    for (int i=0;i<RUNWAYS;i++) {
        ids[i]=i;
        pthread_create(&ct[i],NULL,controller,&ids[i]);
    }

    pthread_join(gen,NULL);

    for (int i=0;i<RUNWAYS;i++)
        pthread_join(ct[i],NULL);

    run=0;
    pthread_cancel(mon);
    pthread_join(mon,NULL);

    printf("done\n");
    return 0;
}
