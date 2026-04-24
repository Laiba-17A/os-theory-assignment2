#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define DOCS 4
#define SENIORS 2

enum { NORMAL=0, SERIOUS=1, CRITICAL=2 };

typedef struct patient {
    int id;
    int pri;
    struct patient *next;
} patient;

typedef struct {
    patient *head[3];
    int cnt[3];
    int total;
} queue_t;

queue_t q;

typedef struct {
    int id;
    int type;   // 1 = senior, 0 = junior
    int norm_cnt;
} doctor;

doctor d[DOCS];

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv_pat = PTHREAD_COND_INITIALIZER;
pthread_cond_t cv_sen = PTHREAD_COND_INITIALIZER;

int free_sen = SENIORS;
int next_id = 1;
int done = 0;

void add(patient *p) {
    p->next = NULL;

    if (!q.head[p->pri]) q.head[p->pri] = p;
    else {
        patient *t = q.head[p->pri];
        while (t->next) t = t->next;
        t->next = p;
    }

    q.cnt[p->pri]++;
    q.total++;
}

patient* rem(int p) {
    patient *x = q.head[p];
    if (!x) return NULL;

    q.head[p] = x->next;
    q.cnt[p]--;
    q.total--;

    return x;
}

patient* pick(int type) {
    if (type) {
        if (q.cnt[CRITICAL]) return rem(CRITICAL);
        if (q.cnt[SERIOUS]) return rem(SERIOUS);
        if (q.cnt[NORMAL]) return rem(NORMAL);
    } else {
        if (q.cnt[SERIOUS]) return rem(SERIOUS);
        if (q.cnt[NORMAL]) return rem(NORMAL);
    }
    return NULL;
}

void treat(int id, patient *p) {
    char *pn[] = {"normal","serious","critical"};
    printf("Doctor %d treating patient %d (%s)\n", id, p->id, pn[p->pri]);
    usleep(300000 + rand()%500000);
    printf("Doctor %d finished patient %d\n", id, p->id);
    free(p);
}

void* doctor_thread(void *arg) {
    int id = *(int*)arg;
    doctor *doc = &d[id];

    while (1) {
        pthread_mutex_lock(&lock);

        while (!done && q.total == 0)
            pthread_cond_wait(&cv_pat, &lock);

        if (done && q.total == 0) {
            pthread_mutex_unlock(&lock);
            break;
        }

        patient *p;

        if (doc->norm_cnt >= 3 && q.cnt[SERIOUS]) {
            p = rem(SERIOUS);
            doc->norm_cnt = 0;
        } else {
            p = pick(doc->type);
        }

        if (!p) {
            pthread_mutex_unlock(&lock);
            continue;
        }

        if (p->pri == NORMAL) doc->norm_cnt++;
        else doc->norm_cnt = 0;

        if (p->pri == CRITICAL) free_sen--;

        pthread_mutex_unlock(&lock);

        treat(id, p);

        pthread_mutex_lock(&lock);

        if (p->pri == CRITICAL) free_sen++;

        pthread_cond_broadcast(&cv_pat);
        pthread_cond_broadcast(&cv_sen);

        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

void* monitor_thread(void *arg) {
    while (!done) {
        sleep(2);

        pthread_mutex_lock(&lock);

        if (q.cnt[SERIOUS] >= 5) {
            patient *p = rem(SERIOUS);
            if (p) {
                p->pri = CRITICAL;
                add(p);
                printf("Promotion: Serious -> Critical (patient %d)\n", p->id);
                pthread_cond_broadcast(&cv_pat);
            }
        }

        pthread_mutex_unlock(&lock);
    }

    return NULL;
}

void* patient_thread(void *arg) {
    int pri = *(int*)arg;

    patient *p = malloc(sizeof(patient));

    pthread_mutex_lock(&lock);

    p->id = next_id++;
    p->pri = pri;
    p->next = NULL;

    printf("Patient %d arrives (%d)\n", p->id, pri);

    if (pri == CRITICAL) {
        while (free_sen == 0)
            pthread_cond_wait(&cv_sen, &lock);
    }

    add(p);

    pthread_cond_signal(&cv_pat);
    pthread_mutex_unlock(&lock);

    return NULL;
}

int main() {
    srand(time(NULL));

    printf("Smart Hospital Simulation\n");

    pthread_t doc[DOCS], mon;
    int ids[DOCS];

    for (int i = 0; i < DOCS; i++) {
        ids[i] = i;
        d[i].id = i;
        d[i].type = (i < SENIORS) ? 1 : 0;
        d[i].norm_cnt = 0;

        pthread_create(&doc[i], NULL, doctor_thread, &ids[i]);
    }

    pthread_create(&mon, NULL, monitor_thread, NULL);

    int arr[] = {
        CRITICAL, NORMAL, SERIOUS, NORMAL,
        CRITICAL, SERIOUS, SERIOUS, SERIOUS,
        NORMAL, NORMAL
    };

    int n = sizeof(arr)/sizeof(arr[0]);
    pthread_t pt[20];

    for (int i = 0; i < n; i++) {
        usleep(200000);
        pthread_create(&pt[i], NULL, patient_thread, &arr[i]);
    }

    for (int i = 0; i < n; i++)
        pthread_join(pt[i], NULL);

    sleep(3);

    pthread_mutex_lock(&lock);
    done = 1;
    pthread_cond_broadcast(&cv_pat);
    pthread_cond_broadcast(&cv_sen);
    pthread_mutex_unlock(&lock);

    for (int i = 0; i < DOCS; i++)
        pthread_join(doc[i], NULL);

    pthread_cancel(mon);
    pthread_join(mon, NULL);

    printf("Simulation complete\n");

    return 0;
}
