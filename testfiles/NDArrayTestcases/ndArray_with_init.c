extern void __VERIFIER_error();
extern int __VERIFIER_nondet_int();

int main() {
	int x1 = __VERIFIER_nondet_int();
	int x2 = __VERIFIER_nondet_int();
	int arr[4] = {x1, x2, 2, 1};
	if(arr[0] == (arr[1]*arr[2]-arr[3]))
		__VERIFIER_error();	
	else
		return 0;
}
