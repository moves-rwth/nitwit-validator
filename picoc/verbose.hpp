#ifndef NITWIT_VERBOSE_HPP
#define NITWIT_VERBOSE_HPP

#ifdef VERBOSE
#include <cstdio>
#define cw_verbose printf
#else
#include <string>
void cw_verbose(std::string const& Format, ...);
#endif

#include "interpreter.hpp"

void printVarTable(Picoc* pc);

#include "lextoken.hpp"
char const* tokenToString(LexToken token);

char const* getType(Value* value);
char const* getType(ValueType* Typ);

#endif
