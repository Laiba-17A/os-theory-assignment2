#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

typedef enum { normal = 0, serious = 1, critical = 2 } priority;
typedef enum { junior = 0, senior = 1 } dtype;

typedef struct patient {
    int id;
    priority pri;
    time_t arr;
    struct patient *next;
} patient;

typedef struct {
    patient *head[3];
    int cnt[3];
    int total;
} queue_t;

#define docs 4
#define seniors 2
#define juniors (docs - seniors)

typedef struct {
    int id;
    dtype type;
    int busy;
    int norm_cnt;
} doctor;

queue_t q;
doctor d[docs];

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cv_pat = PTHREAD_COND_INITIALIZER;
pthread_cond_t cv_sen = PTHREAD_COND_INITIALIZER;

int free_sen = seniors;
int next_id = 1;
int done = 0;

void add(patient *p) {
    p->next = NULL;

    if (!q.head[p->pri]) {
        q.head[p->pri] = p;
    } else {
        patient *t = q.head[p->pri];
        while (t->next)
            t = t->next;
        t->next = p;
    }

    q.cnt[p->pri]++;
    q.total++;
}

patient *remove_pri(priority p) {
    patient *x = q.head[p];

    if (!x)
        return NULL;

    q.head[p] = x->next;
    q.cnt[p]--;
    q.total--;

    return x;
}

patient *pick(dtype t) {
    if (t == senior) {
        if (q.cnt[critical]) return remove_pri(critical);
        if (q.cnt[serious]) return remove_pri(serious);
        if (q.cnt[normal]) return remove_pri(normal);
    } else {
        if (q.cnt[serious]) return remove_pri(serious);
        if (q.cnt[normal]) return remove_pri(normal);
    }

    return NULL;
}

void treat(doctor *doc, patient *p) {
    char *pn[] = {"normal", "serious", "critical"};
    char *dn[] = {"junior", "senior"};

    printf("%s doctor %d treating %s patient %d\n",
           dn[doc->type], doc->id, pn[p->pri], p->id);

    usleep(500000 + rand() % 1000000);

    printf("doctor %d finished patient %d\n", doc->id, p->id);

    free(p);
}

void *doctor_thread(void *arg) {
    int idx = *(int *)arg;
    doctor *doc = &d[idx];

    while (1) {
        pthread_mutex_lock(&lock);

        while (!done) {
            if (doc->type == senior) {
                if (q.total)
                    break;
            } else {
                if (q.cnt[serious] || q.cnt[normal])
                    break;
            }

            pthread_cond_wait(&cv_pat, &lock);
        }

        if (done && q.total == 0) {
            pthread_mutex_unlock(&lock);
            break;
        }

        patient *p = NULL;

        if (doc->norm_cnt >= 3 && q.cnt[serious]) {
            printf("doctor %d forced to take serious patient\n", doc->id);
            p = remove_pri(serious);
            doc->norm_cnt = 0;
        } else {
            p = pick(doc->type);
        }

        if (!p) {
            pthread_mutex_unlock(&lock);
            continue;
        }

        if (p->pri == normal)
            doc->norm_cnt++;
        else
            doc->norm_cnt = 0;

        doc->busy = 1;

        if (doc->type == senior)
            free_sen--;

        pthread_mutex_unlock(&lock);

        treat(doc, p);

        pthread_mutex_lock(&lock);

        doc->busy = 0;

        if (doc->type == senior)
            free_sen++;

        pthread_cond_broadcast(&cv_pat);
        pthread_cond_broadcast(&cv_sen);

        pthread_mutex_unlock(&lock);
    }

    pthread_exit(NULL);
}

void *monitor_thread(void *arg) {
    while (!done) {
        sleep(2);

        pthread_mutex_lock(&lock);

        if (q.cnt[serious] >= 5) {
            patient *p = remove_pri(serious);

            if (p) {
                p->pri = critical;
                add(p);
                printf("patient %d promoted serious to critical\n", p->id);
                pthread_cond_broadcast(&cv_pat);
            }
        }

        pthread_mutex_unlock(&lock);
    }

    pthread_exit(NULL);
}

void *patient_thread(void *arg) {
    int pri = *(int *)arg;
    char *pn[] = {"normal", "serious", "critical"};

    patient *p = malloc(sizeof(patient));

    pthread_mutex_lock(&lock);

    p->id = next_id++;
    p->pri = pri;
    p->arr = time(NULL);
    p->next = NULL;

    printf("%s patient %d arrives\n", pn[p->pri], p->id);

    if (p->pri == critical) {
        while (free_sen == 0) {
            printf("critical patient %d waiting for senior doctor\n", p->id);
            pthread_cond_wait(&cv_sen, &lock);
        }
    }

    add(p);

    pthread_cond_signal(&cv_pat);
    pthread_mutex_unlock(&lock);

    pthread_exit(NULL);
}

int main() {
    srand(42);

    printf("smart hospital emergency department\n");

    for (int i = 0; i < docs; i++) {
        d[i].id = i + 1;
        d[i].type = (i < seniors) ? senior : junior;
        d[i].busy = 0;
        d[i].norm_cnt = 0;

        printf("doctor %d %s\n",
               d[i].id,
               d[i].type == senior ? "senior" : "junior");
    }

    pthread_t dt[docs];
    int ids[docs];

    for (int i = 0; i < docs; i++) {
        ids[i] = i;
        pthread_create(&dt[i], NULL, doctor_thread, &ids[i]);
    }

    pthread_t mon;
    pthread_create(&mon, NULL, monitor_thread, NULL);

    int arr[] = {
        critical, normal, serious, normal,
        critical, serious, serious, serious,
        serious, serious, normal, normal, normal
    };

    int total = sizeof(arr) / sizeof(arr[0]);
    pthread_t pt[32];

    for (int i = 0; i < total; i++) {
        usleep(200000);
        pthread_create(&pt[i], NULL, patient_thread, &arr[i]);
    }

    for (int i = 0; i < total; i++)
        pthread_join(pt[i], NULL);

    sleep(6);

    pthread_mutex_lock(&lock);
    done = 1;
    pthread_cond_broadcast(&cv_pat);
    pthread_cond_broadcast(&cv_sen);
    pthread_mutex_unlock(&lock);

    for (int i = 0; i < docs; i++)
        pthread_join(dt[i], NULL);

    pthread_cancel(mon);
    pthread_join(mon, NULL);

    printf("simulation complete\n");

    return 0;
}
