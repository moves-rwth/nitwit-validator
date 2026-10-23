// Function pointers: calls through pointers, arrays of them, callbacks, functions returning function pointers.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

typedef int (*BinaryOp)(int, int);

int add(int a, int b) {
    return a + b;
}

int subtract(int a, int b) {
    return a - b;
}

int multiply(int a, int b) {
    return a * b;
}

int apply(BinaryOp op, int a, int b) {
    return op(a, b);
}

BinaryOp selectOp(char symbol) {
    switch (symbol) {
    case '+':
        return add;
    case '-':
        return &subtract;
    default:
        return multiply;
    }
}

int fold(const int *values, int count, int start, int (*op)(int, int)) {
    int result = start;
    for (int i = 0; i < count; ++i) {
        result = (*op)(result, values[i]);
    }
    return result;
}

struct Command {
    const char *name;
    BinaryOp run;
};

void hello(void) {
    printf("hello\n");
}

int main(void) {
    BinaryOp table[] = {add, subtract, multiply};
    for (int i = 0; i < 3; ++i) {
        printf("%d ", table[i](7, 3));
    }
    printf("\n");
    printf("%d %d %d\n", apply(add, 2, 3), selectOp('-')(10, 4), (*selectOp('*'))(6, 7));

    int values[] = {1, 2, 3, 4, 5};
    printf("%d %d\n", fold(values, 5, 0, add), fold(values, 5, 1, multiply));

    struct Command commands[] = {{"add", add}, {"sub", subtract}};
    int k = __VERIFIER_nondet_int();
    printf("%s %d\n", commands[k].name, commands[k].run(100, 1));

    printf("%d %d %d\n", table[0] == add, table[1] == add, selectOp('+') == &add);
    void (*greet)(void) = hello;
    greet();
    (*greet)();
    (**greet)();

    // Converting a function pointer to another function pointer type and back yields the original pointer.
    void (*opaque)(void) = (void (*)(void))multiply;
    BinaryOp back = (BinaryOp)opaque;
    printf("%d %d\n", back(6, 9), back == multiply);
    BinaryOp none = 0;
    printf("%d\n", none == 0);
    return 0;
}
