// Simple and compound assignments (C17 6.5.16), increments and decrements.
char c;
unsigned short us;
double d;
int *p;
_Bool b;

void assignments(void) {
    c = 300;
    c += d;
    c *= 2;
    us <<= 3;
    us %= 7;
    p += 2;
    p -= c;
    b |= 2;
    d /= c;
    c++;
    --d;
    p++;
    c = us = 1;
}
