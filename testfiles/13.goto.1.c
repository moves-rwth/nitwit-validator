extern void __VERIFIER_error();

int main()
{
    int m = 1;
    l1:
    if (m){
        ++m;
        if (m == 5){
            m = 0;
        }
        goto l1;
    }
    else
    l2:
    {
        --m;
        if (m == -3){
            goto error;
        }
        goto l2;
    }


    return 0;
    error:
        __VERIFIER_error();
}

