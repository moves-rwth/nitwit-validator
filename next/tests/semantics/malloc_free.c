// Dynamic memory: malloc, calloc (zeroed), realloc (keeps the contents), free, linked structures on the heap.
int printf(const char *, ...);
typedef __typeof__(sizeof 0) size_t;
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
int __VERIFIER_nondet_int(void);

struct Node {
    int value;
    struct Node *next;
};

struct Node *push(struct Node *head, int value) {
    struct Node *node = malloc(sizeof *node);
    node->value = value;
    node->next = head;
    return node;
}

int main(void) {
    int n = __VERIFIER_nondet_int();
    int *values = malloc(n * sizeof(int));
    for (int i = 0; i < n; ++i) {
        values[i] = i * 3;
    }
    values = realloc(values, 2 * n * sizeof(int));
    for (int i = n; i < 2 * n; ++i) {
        values[i] = -i;
    }
    int sum = 0;
    for (int i = 0; i < 2 * n; ++i) {
        sum += values[i];
    }
    printf("%d %d %d\n", values[n - 1], values[2 * n - 1], sum);
    free(values);

    long long *zeros = calloc(16, sizeof(long long));
    long long total = 0;
    for (int i = 0; i < 16; ++i) {
        total += zeros[i];
    }
    printf("%lld\n", total);
    free(zeros);

    struct Node *list = 0;
    for (int i = 1; i <= n; ++i) {
        list = push(list, i * i);
    }
    int length = 0;
    for (struct Node *p = list; p; p = p->next) {
        printf("%d ", p->value);
        ++length;
    }
    printf("(%d)\n", length);
    while (list) {
        struct Node *next = list->next;
        free(list);
        list = next;
    }

    int rows = 3, cols = n;
    int **matrix = malloc(rows * sizeof(int *));
    for (int i = 0; i < rows; ++i) {
        matrix[i] = calloc(cols, sizeof(int));
        matrix[i][i % cols] = i + 1;
    }
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            printf("%d", matrix[i][j]);
        }
        printf("\n");
    }
    for (int i = 0; i < rows; ++i) {
        free(matrix[i]);
    }
    free(matrix);

    char *text = malloc(4);
    text[0] = 'o';
    text[1] = 'k';
    text[2] = '\0';
    text = realloc(text, 100);
    printf("%s\n", text);
    free(text);
    free(0);
    void *nothing = realloc(0, 8); // realloc of a null pointer allocates
    printf("%d\n", nothing != 0);
    free(nothing);
    return 0;
}
