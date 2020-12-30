void __VERIFIER_error(void);

int main()
{
    int z = ({int y = 2; z= 3*y;});

	if(z==6)
		__VERIFIER_error();
    
    return 0;
}
