extern void __VERIFIER_error();

typedef struct A {
	int x;	
} A;

typedef struct B {
   A a;
   int x;
} B;

int main() {	
	B z = {{1},2};
    if (z.a.x == 1) {
        __VERIFIER_error();
    } else {
        return 0;
    }
}

