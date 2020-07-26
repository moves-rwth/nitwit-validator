extern void __VERIFIER_error();

typedef struct A {
	int y;
	int z;	
} A;

typedef struct B {
   A a;
   int c;
} B;

int main() {	
	B z = {{1,3},3};
    if (z.a.y == 1 && z.a.z == 3) {
        __VERIFIER_error();
    } else {
        return 0;
    }
}

