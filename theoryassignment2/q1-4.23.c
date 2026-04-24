#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int lim = 0;

int prime(int n) {
    if (n < 2)
        return 0;

    for (int i = 2; i * i <= n; i++)
        if (n % i == 0)
            return 0;

    return 1;
}

void *show_primes(void *arg) {
    printf("prime numbers up to %d:\n", lim);

    for (int i = 2; i <= lim; i++) {
        if (prime(i))
            printf("%d ", i);
    }

    printf("\n");
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("enter one number\n");
        return 1;
    }

    lim = atoi(argv[1]);

    if (lim < 2) {
        printf("no prime numbers\n");
        return 0;
    }

    pthread_t t;

    pthread_create(&t, NULL, show_primes, NULL);
    pthread_join(t, NULL);

    return 0;
}
