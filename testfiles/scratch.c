#include <stdio.h>
#include <math.h>
 void __VERIFIER_error(){
    printf("Error\n");
}
/* Example from "Abstract Domains for Bit-Level Machine Integer and
   Floating-point Operations" by Miné, published in WING 12.
*/
double __VERIFIER_nondet_double(void){
    return 22.822146805041292;
}
int __VERIFIER_nondet_int(void){
    return 0;
}
void __VERIFIER_assume(int expression){
    printf("Assume %d\n", expression);
}
void __VERIFIER_assert(int cond) { if (!(cond)) { ERROR: __VERIFIER_error(); } return; }
float pi = 3.14159 ;


double diff(double x1,double x2)
{
    if(x1 > x2)
        return (x1-x2) ;
    else
        return (x2-x1) ;
}

double radianMeasure(int degrees)
{
    return (degrees * (pi/180)) ;
}
int main()
{
    int x ;
    float angleInRadian ;
    float phaseLag = pi/2, phaseLead=pi ;
    double sum1=0.0, sum2 = 0.0 ;
    int temp;
    double count=0.0 ;
    printf("phaselag: %f, phaselead: %f", phaseLag, phaseLead);


    while(1)
    {
        x = __VERIFIER_nondet_int() ;
        __VERIFIER_assume(x > -180 && x < 180) ;
        angleInRadian = radianMeasure(x) ; printf("x is %d\n", x);
        printf("Diff: %f, sum1: %f, sum2: %f\n", diff(sum1, sum2), sum1, sum2);
        sum2 = sum2 + sin(angleInRadian+2*phaseLead);
        printf("Diff: %f, sum1: %f, sum2: %f\n", diff(sum1, sum2), sum1, sum2);
        sum1 = sum1 + cos(angleInRadian+3*phaseLag) ;
        printf("Diff: %f, sum1: %f, sum2: %f\n", diff(sum1, sum2), sum1, sum2);
        temp = __VERIFIER_nondet_int() ;
        count++ ;
        if(temp == 0) break ;
    }
    printf("Diff: %f, cnt: %f\n", diff(sum1, sum2), count*2);
    __VERIFIER_assert(diff(sum1,sum2) <= count*2) ;
    return 0 ;
}
