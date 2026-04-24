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
#define total (row * row)
#define thot 35.0
#define tcold -10.0
#define miss -9999.0

double sat[m][n][n];
double g[n][n];
double norm[n][n];
double risk[n][n];

int hot_map[n][n];
int cold_map[n][n];

double gmax, gmin, gmean, gvar;
long hot_cnt, cold_cnt, anom_cnt;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;

sem_t sa, sb, sc;

typedef struct {
    int r, c;
    double v;
} cell;

cell top[10];

int q[total];
int head = 0;

void gen() {
    srand(42);

    for (int s = 0; s < m; s++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (rand() % 100 < 5) {
                    sat[s][i][j] = miss;
                } else {
                    double x = -20 + (rand() % 6500) / 100.0;

                    if (i > 800 && j > 800 && s == 0)
                        x = 36 + rand() % 5;

                    if (i < 100 && j < 100 && s == 1)
                        x = -15 - rand() % 8;

                    sat[s][i][j] = x;
                }
            }
        }
    }
}

double fill(int s, int r, int c) {
    int dr[] = {-1, 1, 0, 0};
    int dc[] = {0, 0, -1, 1};

    double sum = 0;
    int cnt = 0;

    for (int i = 0; i < 4; i++) {
        int nr = r + dr[i];
        int nc = c + dc[i];

        if (nr >= 0 && nr < n && nc >= 0 && nc < n &&
            sat[s][nr][nc] != miss) {
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
                if (sat[s][i][j] == miss)
                    sat[s][i][j] = fill(s, i, j);
            }
        }
    }

    pthread_exit(NULL);
}

void merge() {
    static int cnt[n][n];

    memset(cnt, 0, sizeof(cnt));
    memset(g, 0, sizeof(g));

    for (int s = 0; s < m; s++) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                g[i][j] += sat[s][i][j];
                cnt[i][j]++;
            }
        }
    }

    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            g[i][j] /= cnt[i][j];
}

int next() {
    pthread_mutex_lock(&qlock);
    int t = (head < total) ? q[head++] : -1;
    pthread_mutex_unlock(&qlock);
    return t;
}

void *tile_thread(void *arg) {
    int t;

    while ((t = next()) >= 0) {
        int r1 = (t / row) * tile;
        int c1 = (t % row) * tile;
        int r2 = r1 + tile;
        int c2 = c1 + tile;

        double mx = -DBL_MAX;
        double mn = DBL_MAX;
        double sum = 0, sum2 = 0;
        long an = 0;
        int cnt = tile * tile;

        for (int i = r1; i < r2; i++) {
            for (int j = c1; j < c2; j++) {
                double x = g[i][j];

                if (x > mx) mx = x;
                if (x < mn) mn = x;

                sum += x;
                sum2 += x * x;
            }
        }

        double mean = sum / cnt;
        double var = sum2 / cnt - mean * mean;
        double sd = sqrt(fabs(var));

        for (int i = r1; i < r2; i++)
            for (int j = c1; j < c2; j++)
                if (fabs(g[i][j] - mean) > 2 * sd)
                    an++;

        pthread_mutex_lock(&lock);

        if (mx > gmax) gmax = mx;
        if (mn < gmin) gmin = mn;

        gmean += sum;
        gvar += sum2;
        anom_cnt += an;

        pthread_mutex_unlock(&lock);
    }

    pthread_exit(NULL);
}

void *task_a(void *arg) {
    long cnt = 0;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (g[i][j] > thot) {
                hot_map[i][j] = 1;
                cnt++;
            }
        }
    }

    hot_cnt = cnt;
    sem_post(&sa);
    pthread_exit(NULL);
}

void *task_b(void *arg) {
    long cnt = 0;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (g[i][j] < tcold) {
                cold_map[i][j] = 1;
                cnt++;
            }
        }
    }

    cold_cnt = cnt;
    sem_post(&sb);
    pthread_exit(NULL);
}

int near_both(int r, int c) {
    for (int dr = -2; dr <= 2; dr++) {
        for (int dc = -2; dc <= 2; dc++) {
            if (abs(dr) + abs(dc) > 2)
                continue;

            int nr = r + dr;
            int nc = c + dc;

            if (nr < 0 || nr >= n || nc < 0 || nc >= n)
                continue;

            if (hot_map[nr][nc] && cold_map[nr][nc])
                return 1;
        }
    }

    return 0;
}

void *task_c(void *arg) {
    double range = gmax - gmin;
    if (range == 0) range = 1;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double x = g[i][j];

            if (near_both(i, j))
                x += 0.1;

            norm[i][j] = (x - gmin) / range;

            if (norm[i][j] > 1)
                norm[i][j] = 1;
        }
    }

    sem_post(&sc);
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
    sem_wait(&sa);
    sem_wait(&sb);
    sem_wait(&sc);

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double h = prox(i, j, hot_map);
            double c = prox(i, j, cold_map);

            risk[i][j] = norm[i][j] * h / (c + 1.0);
        }
    }

    for (int k = 0; k < 10; k++)
        top[k].v = -1;

    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            double x = risk[i][j];
            int p = 0;

            for (int k = 1; k < 10; k++)
                if (top[k].v < top[p].v)
                    p = k;

            if (x > top[p].v) {
                top[p].r = i;
                top[p].c = j;
                top[p].v = x;
            }
        }
    }

    pthread_exit(NULL);
}

int main() {
    printf("climate matrix analysis\n");

    sem_init(&sa, 0, 0);
    sem_init(&sb, 0, 0);
    sem_init(&sc, 0, 0);

    gen();

    pthread_t st[m];
    int id[m];

    for (int i = 0; i < m; i++) {
        id[i] = i;
        pthread_create(&st[i], NULL, sat_thread, &id[i]);
    }

    for (int i = 0; i < m; i++)
        pthread_join(st[i], NULL);

    merge();

    gmax = -DBL_MAX;
    gmin = DBL_MAX;

    for (int i = 0; i < total; i++)
        q[i] = i;

    pthread_t tt[8];

    for (int i = 0; i < 8; i++)
        pthread_create(&tt[i], NULL, tile_thread, NULL);

    for (int i = 0; i < 8; i++)
        pthread_join(tt[i], NULL);

    long cells = (long)n * n;
    gmean /= cells;
    gvar = gvar / cells - gmean * gmean;

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

    printf("sample matrix 5x5\n");
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++)
            printf("%.2f ", norm[i][j]);
        printf("\n");
    }

    printf("top 10 risk cells\n");
    for (int i = 0; i < 10; i++)
        printf("%d %d %.4f\n", top[i].r, top[i].c, top[i].v);

    sem_destroy(&sa);
    sem_destroy(&sb);
    sem_destroy(&sc);

    return 0;
}
