#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <pthread.h>
#include <semaphore.h>

#define n 1000
#define m 4
#define tile 100
#define row (n / tile)
#define total_tiles (row * row)
#define hot 35.0
#define cold -10.0
#define nan_val -9999.0

double sat[m][n][n];
double gmat[n][n];
double norm[n][n];
double risk[n][n];

double gmax, gmin, gmean, gvar;
long hot_cnt, cold_cnt, anom_cnt;

int hot_map[n][n];
int cold_map[n][n];

pthread_mutex_t stat_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t map_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;

sem_t sem_a, sem_b, sem_c;

typedef struct {
    int r, c;
    double s;
} cell;

cell top[10];

int q[total_tiles];
int head = 0;

void make_data() {
    srand(42);

    for (int s = 0; s < m; s++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if ((rand() % 100) < 5) {
                    sat[s][i][j] = nan_val;
                } else {
                    double v = -20.0 + (rand() % 6500) / 100.0;

                    if (i > 800 && j > 800 && s == 0)
                        v = 36.0 + rand() % 5;

                    if (i < 100 && j < 100 && s == 1)
                        v = -15.0 - rand() % 8;

                    sat[s][i][j] = v;
                }
            }
        }
    }
}

double fill_val(int s, int r, int c) {
    int dr[] = {-1, 1, 0, 0};
    int dc[] = {0, 0, -1, 1};

    double sum = 0;
    int cnt = 0;

    for (int i = 0; i < 4; i++) {
        int nr = r + dr[i];
        int nc = c + dc[i];

        if (nr >= 0 && nr < n && nc >= 0 && nc < n &&
            sat[s][nr][nc] != nan_val) {
            sum += sat[s][nr][nc];
            cnt++;
        }
    }

    return cnt ? sum / cnt : 0;
}

void *sat_thread(void *arg) {
    int s = *(int *)arg;

    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (sat[s][i][j] == nan_val)
                    sat[s][i][j] = fill_val(s, i, j);
            }
        }
    }

    pthread_exit(NULL);
}

void merge_data() {
    static int cover[n][n];

    memset(cover, 0, sizeof(cover));
    memset(gmat, 0, sizeof(gmat));

    for (int s = 0; s < m; s++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                gmat[i][j] += sat[s][i][j];
                cover[i][j]++;
            }
        }
    }

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            gmat[i][j] /= cover[i][j];
}

int next_tile() {
    pthread_mutex_lock(&q_lock);

    int t = (head < total_tiles) ? q[head++] : -1;

    pthread_mutex_unlock(&q_lock);
    return t;
}

void *tile_thread(void *arg) {
    int t;

    while ((t = next_tile()) >= 0) {
        int r0 = (t / row) * tile;
        int c0 = (t % row) * tile;
        int r1 = r0 + tile;
        int c1 = c0 + tile;

        double lmax = -DBL_MAX;
        double lmin = DBL_MAX;
        double sum = 0, sum2 = 0;
        long cnt = tile * tile;
        long an = 0;

        for (int i = r0; i < r1; i++) {
            for (int j = c0; j < c1; j++) {
                double v = gmat[i][j];

                if (v > lmax) lmax = v;
                if (v < lmin) lmin = v;

                sum += v;
                sum2 += v * v;
            }
        }

        double mean = sum / cnt;
        double var = sum2 / cnt - mean * mean;
        double sd = sqrt(fabs(var));

        for (int i = r0; i < r1; i++)
            for (int j = c0; j < c1; j++)
                if (fabs(gmat[i][j] - mean) > 2 * sd)
                    an++;

        pthread_mutex_lock(&stat_lock);

        if (lmax > gmax) gmax = lmax;
        if (lmin < gmin) gmin = lmin;

        gmean += sum;
        gvar += sum2;
        anom_cnt += an;

        pthread_mutex_unlock(&stat_lock);
    }

    pthread_exit(NULL);
}

void *task_a(void *arg) {
    long cnt = 0;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (gmat[i][j] > hot) {
                hot_map[i][j] = 1;
                cnt++;
            }
        }
    }

    hot_cnt = cnt;
    sem_post(&sem_a);
    pthread_exit(NULL);
}

void *task_b(void *arg) {
    long cnt = 0;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (gmat[i][j] < cold) {
                cold_map[i][j] = 1;
                cnt++;
            }
        }
    }

    cold_cnt = cnt;
    sem_post(&sem_b);
    pthread_exit(NULL);
}

void *task_c(void *arg) {
    double range = gmax - gmin;

    if (range == 0)
        range = 1;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            norm[i][j] = (gmat[i][j] - gmin) / range;

            if (norm[i][j] > 1)
                norm[i][j] = 1;
        }
    }

    sem_post(&sem_c);
    pthread_exit(NULL);
}

double prox(int r, int c, int mp[n][n]) {
    double p = 0;

    for (int dr = -5; dr <= 5; dr++) {
        for (int dc = -5; dc <= 5; dc++) {
            int nr = r + dr;
            int nc = c + dc;

            if (nr < 0 || nr >= n || nc < 0 || nc >= n)
                continue;

            if (mp[nr][nc]) {
                double d = abs(dr) + abs(dc);
                p += 1.0 / (d + 1.0);
            }
        }
    }

    return p;
}

void *risk_thread(void *arg) {
    sem_wait(&sem_a);
    sem_wait(&sem_b);
    sem_wait(&sem_c);

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double ph = prox(i, j, hot_map);
            double pc = prox(i, j, cold_map);

            risk[i][j] = norm[i][j] * ph / (pc + 1.0);
        }
    }

    pthread_exit(NULL);
}

int main() {
    printf("climate temperature matrix analysis\n");

    sem_init(&sem_a, 0, 0);
    sem_init(&sem_b, 0, 0);
    sem_init(&sem_c, 0, 0);

    make_data();

    pthread_t sat_t[m];
    int ids[m];

    for (int i = 0; i < m; i++) {
        ids[i] = i;
        pthread_create(&sat_t[i], NULL, sat_thread, &ids[i]);
    }

    for (int i = 0; i < m; i++)
        pthread_join(sat_t[i], NULL);

    merge_data();

    gmax = -DBL_MAX;
    gmin = DBL_MAX;

    for (int i = 0; i < total_tiles; i++)
        q[i] = i;

    pthread_t tiles[8];

    for (int i = 0; i < 8; i++)
        pthread_create(&tiles[i], NULL, tile_thread, NULL);

    for (int i = 0; i < 8; i++)
        pthread_join(tiles[i], NULL);

    long total = (long)n * n;

    gmean = gmean / total;
    gvar = gvar / total - gmean * gmean;

    pthread_t a, b, c, r;

    pthread_create(&a, NULL, task_a, NULL);
    pthread_create(&b, NULL, task_b, NULL);
    pthread_create(&c, NULL, task_c, NULL);
    pthread_create(&r, NULL, risk_thread, NULL);

    pthread_join(a, NULL);
    pthread_join(b, NULL);
    pthread_join(c, NULL);
    pthread_join(r, NULL);

    printf("max %.2f\n", gmax);
    printf("min %.2f\n", gmin);
    printf("mean %.4f\n", gmean);
    printf("variance %.4f\n", gvar);
    printf("hotspots %ld\n", hot_cnt);
    printf("coldspots %ld\n", cold_cnt);
    printf("anomalies %ld\n", anom_cnt);

    sem_destroy(&sem_a);
    sem_destroy(&sem_b);
    sem_destroy(&sem_c);

    return 0;
}
