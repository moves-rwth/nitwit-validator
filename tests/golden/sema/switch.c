// switch: the promoted controlling expression and case values converted to its type (C17 6.8.4.2), case ranges.
int classify(unsigned char c, unsigned u, long l) {
    switch (c) {
    case 'a':
    case 'b' ... 'z':
        return 1;
    case 300:
        return 2;
    default:
        break;
    }
    switch (u) {
    case -1:
        return 3;
    }
    switch (l) {
    case 1L << 40:
        return 4;
    }
    return 0;
}
