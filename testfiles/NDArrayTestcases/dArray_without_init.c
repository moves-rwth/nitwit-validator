extern void __VERIFIER_error();
extern int __VERIFIER_nondet_int();

int main() {
	int arr[4];
	arr[0] = 3;
	arr[1] = 2;
	arr[2] = 2;
	arr[3] = 1;

	if(arr[0] == (arr[1]*arr[2]-arr[3]))
		__VERIFIER_error();	
	else
		return 0;
}
