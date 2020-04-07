extern void __VERIFIER_error();
#include <stdio.h>
#include <stdlib.h>

struct JoinPoint {
    int a;
    int b;
    void *(*fp)(struct JoinPoint * );
};

void * func1(struct JoinPoint * jp){
    struct JoinPoint * x = malloc(sizeof(struct JoinPoint));
    x->a = 1;
    x->b = 1;

    return (void *) x;
}
void * func2(struct JoinPoint * jp) {
    __VERIFIER_error();
    return 0;
}

int main()
{
    struct JoinPoint *jp = malloc(sizeof(struct JoinPoint));
    jp->a = 0;
    jp->b = 0;
    jp->fp = &func1;

    struct JoinPoint * y = (struct JoinPoint *) jp->fp(jp);
    struct JoinPoint ** x = &y;
    printf("%d, %d\n", (*x)->a, (*x)->b);
    free(*x);

    jp->fp = &func2;
    y = (struct JoinPoint *) jp->fp(jp);

    free(jp);

    return 0;
}

