
#ifndef NITWIT_PICOC_SHADOWS_H_
#define NITWIT_PICOC_SHADOWS_H_

#include <map>

struct Value;

class Shadows {
public:
    Shadows(): shadows() {
        //
    }
    ~Shadows() {
//        for (auto& v: shadows)
//            free(v.second);
    }
    std::map<int, Value*> shadows;
};

#endif
