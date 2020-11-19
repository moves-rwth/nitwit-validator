extern void __VERIFIER_error();
extern int __VERIFIER_nondet_int();

int main() {
	int arr[2];
	arr[0] = 2;
	arr[1] = __VERIFIER_nondet_int();

	if(arr[0] == arr[1])
		__VERIFIER_error();	
	else
		return 0;
}
