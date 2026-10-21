// Duff's device: a switch whose case labels are inside a do-while loop.
int printf(const char *, ...);
int __VERIFIER_nondet_int(void);

void copy(char *to, const char *from, int count) {
    int n = (count + 7) / 8;
    switch (count % 8) {
    case 0:
        do {
            *to++ = *from++;
        case 7:
            *to++ = *from++;
        case 6:
            *to++ = *from++;
        case 5:
            *to++ = *from++;
        case 4:
            *to++ = *from++;
        case 3:
            *to++ = *from++;
        case 2:
            *to++ = *from++;
        case 1:
            *to++ = *from++;
        } while (--n > 0);
    }
}

// Case labels may also be nested inside if statements and blocks of the switch body.
int jumpIntoIf(int value) {
    int result = 0;
    switch (value) {
    case 0:
        if (result == 0) {
        case 1:
            result += 1;
        } else {
        case 2:
            result += 2;
        }
        break;
    default:
        result = -1;
    }
    return result;
}

int main(void) {
    const char *source = "abcdefghijklmnopqrstuvwxyz";
    for (int count = 1; count <= 20; count += 3) {
        char target[32] = {0};
        copy(target, source, count);
        printf("%d %s\n", count, target);
    }
    int count = __VERIFIER_nondet_int();
    char target[32] = {0};
    copy(target, source, count);
    printf("%d %s\n", count, target);
    printf("%d %d %d %d\n", jumpIntoIf(0), jumpIntoIf(1), jumpIntoIf(2), jumpIntoIf(3));
    return 0;
}
