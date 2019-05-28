//extern void __VERIFIER_error();
#include <stdio.h>
#include <stdlib.h>

struct JoinPoint {
    int a;
    int b;
};

void * func1(struct JoinPoint * jp){
    struct JoinPoint * x = malloc(sizeof(struct JoinPoint));
    x->a = 1;
    x->b = 1;

    return (void *) x;
}


int main()
{
    void *(*fp)(struct JoinPoint * );
    fp = &func1;

    struct JoinPoint *jp = malloc(sizeof(struct JoinPoint));
    jp->a = 0;
    jp->b = 0;

    struct JoinPoint * y = (struct JoinPoint *) fp(jp);
    struct JoinPoint ** x = &y;
    printf("%d, %d\n", (*x)->a, (*x)->b);
    free(*x);
    free(jp);

    return 0;
}

