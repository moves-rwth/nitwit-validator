extern void __VERIFIER_error() __attribute__ ((__noreturn__));
extern void* __VERIFIER_nondet_pointer(void);


typedef unsigned int size_t;
extern  __attribute__((__nothrow__)) void *malloc(size_t __size ) __attribute__((__malloc__));

void err()
{ ERROR: __VERIFIER_error();}

typedef struct list {
 int n;
 int x;
} list;

int i = 1;
void * allocate_memory()
{
 return malloc(8U);
}


int main()
{
 struct list m = {1,1};
 void *l = VERIFIER_nondet_pointer();
 l = &m;
 ((list *)l)->n -= 1;
 ((list *)l)->x += 1;
 if (m.n == 0 && m.x == 2) err();


}
