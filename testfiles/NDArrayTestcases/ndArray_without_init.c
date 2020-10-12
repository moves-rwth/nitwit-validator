extern void __VERIFIER_error();
extern int __VERIFIER_nondet_int();

int main() {
	int arr[4];
	arr[0] = 2;
	for(int i=1; i<4; i++) {
		arr[i] = __VERIFIER_nondet_int();
	}

	if(arr[0] == (arr[1]*arr[2]-arr[3]))
		__VERIFIER_error();	
	else
		return 0;
}
