extern void __VERIFIER_error() __attribute__ ((__noreturn__));

extern void *__VERIFIER_nondet_pointer(void);

int main(void) {
	void *ptr = __VERIFIER_nondet_pointer(); 
    
	int z = 1;
	ptr = &z;

	*(int *)ptr += 1;

	if(z == 2)
		__VERIFIER_error();
	
	return 0;
}


