// Scopes: shadowing, block-scope extern declarations, redeclarations at file scope.
extern int shared;
int shared = 1;
int shadowed = 2;

int scopes(int param) {
    int shadowed = param;
    {
        extern int shadowed;
        int inner = shadowed;
        {
            int shadowed = inner;
            param = shadowed;
        }
    }
    extern int later;
    int helper(void);
    return shadowed + later + helper();
}

int later = 3;
int helper(void) { return shared; }
