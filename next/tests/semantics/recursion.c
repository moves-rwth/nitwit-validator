// Recursion: direct and mutual recursion, recursion on arrays, deep recursion with locals in every frame.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

long long factorial(int n) {
    return n <= 1 ? 1 : n * factorial(n - 1);
}

int fibonacci(int n) {
    return n < 2 ? n : fibonacci(n - 1) + fibonacci(n - 2);
}

int ackermann(int m, int n) {
    if (m == 0) {
        return n + 1;
    }
    if (n == 0) {
        return ackermann(m - 1, 1);
    }
    return ackermann(m - 1, ackermann(m, n - 1));
}

int isOdd(int n);

int isEven(int n) {
    return n == 0 ? 1 : isOdd(n - 1);
}

int isOdd(int n) {
    return n == 0 ? 0 : isEven(n - 1);
}

void quicksort(int *values, int low, int high) {
    if (low >= high) {
        return;
    }
    int pivot = values[high];
    int i = low;
    for (int j = low; j < high; ++j) {
        if (values[j] < pivot) {
            int t = values[i];
            values[i] = values[j];
            values[j] = t;
            ++i;
        }
    }
    int t = values[i];
    values[i] = values[high];
    values[high] = t;
    quicksort(values, low, i - 1);
    quicksort(values, i + 1, high);
}

int hanoi(int disks, int from, int to, int via) {
    if (disks == 0) {
        return 0;
    }
    return hanoi(disks - 1, from, via, to) + 1 + hanoi(disks - 1, via, to, from);
}

// Every frame has its own copy of the local array.
int depth(int n) {
    int local[4] = {n, n, n, n};
    if (n == 0) {
        return 0;
    }
    int below = depth(n - 1);
    return below + local[3] - local[0] + 1;
}

int main(void) {
    printf("%lld %d %d\n", factorial(20), fibonacci(20), ackermann(2, 3));
    printf("%d %d\n", isEven(10), isOdd(7));
    int values[] = {5, -3, 9, 0, 12, 7, -8, 5};
    quicksort(values, 0, 7);
    for (int i = 0; i < 8; ++i) {
        printf("%d ", values[i]);
    }
    printf("\n");
    int n = __VERIFIER_nondet_int();
    printf("%d %d %lld\n", hanoi(n, 1, 3, 2), depth(n * 1000), factorial(n));
    return 0;
}
