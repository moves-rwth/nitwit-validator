// switch (C17 6.8.4.2): fall-through, default anywhere, nested switches, switch on char and long long, break and
// continue inside loops.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

const char *classify(int value) {
    switch (value) {
    case 0:
        return "zero";
    default:
        return "other";
    case 1:
    case 2:
        return "small";
    case -1:
        return "minus one";
    }
}

int fallThrough(int value) {
    int result = 0;
    switch (value) {
    case 1:
        result += 1;
    case 2:
        result += 10;
    case 3:
        result += 100;
        break;
    case 4:
        result += 1000;
    default:
        result += 10000;
    }
    return result;
}

int main(void) {
    for (int i = -2; i <= 3; ++i) {
        printf("%d:%s ", i, classify(i));
    }
    printf("\n");
    for (int i = 0; i <= 5; ++i) {
        printf("%d ", fallThrough(i));
    }
    printf("\n");

    const char *text = "a1b2-c";
    int letters = 0, digits = 0, others = 0;
    for (const char *p = text; *p; ++p) {
        switch (*p) {
        case 'a':
        case 'b':
        case 'c':
            ++letters;
            continue;
        case '0':
        case '1':
        case '2':
            ++digits;
            break;
        default:
            ++others;
        }
    }
    printf("%d %d %d\n", letters, digits, others);

    long long big = 5000000000LL;
    switch (big) {
    case 5000000000LL:
        printf("big\n");
        break;
    case 5:
        printf("five\n");
        break;
    }

    int k = __VERIFIER_nondet_int();
    int j = __VERIFIER_nondet_int();
    switch (k) {
    case 1:
        switch (j) {
        case 2:
            printf("inner 2\n");
            break;
        default:
            printf("inner default\n");
        }
        printf("after inner\n");
    case 2:
        printf("outer 2\n");
        break;
    }

    // A switch without a matching case and without default skips the body.
    switch (k + 100) {
    case 1:
        printf("never\n");
    }
    // The controlling expression is promoted; case labels are converted to the promoted type.
    unsigned char uc = 200;
    switch (uc) {
    case 200:
        printf("uc 200\n");
        break;
    case -56:
        printf("uc -56\n");
        break;
    }
    unsigned u = 4294967295u;
    switch (u) {
    case -1:
        printf("u matches -1\n");
        break;
    default:
        printf("u default\n");
    }
    return 0;
}
