// Selection and iteration statements.
int sum(int n) {
    int total = 0;
    if (n < 0)
        return -1;
    else if (n == 0)
        return 0;
    else
        ;
    for (int i = 0; i < n; i++) {
        if (i % 2)
            continue;
        total += i;
    }
    for (;;)
        break;
    while (n > 10)
        n--;
    do {
        n++;
    } while (n < 5);
    {
        int nested = total;
        total = nested;
    }
    return total;
}
