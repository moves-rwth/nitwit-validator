// Labels and goto: forward and backward jumps, labels in nested blocks, local labels.
int search(int *a, int n, int key) {
    int i = 0;
loop:
    if (i >= n)
        goto not_found;
    {
    check:
        if (a[i] == key)
            goto found;
    }
    i++;
    goto loop;
found:
    return i;
not_found:
    return ({
        __label__ out;
        int r = -1;
        goto out;
    out:
        r;
    });
}
