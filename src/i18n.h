#pragma once

#include <string>

// ES owns the UI language; everything else follows this flag.
extern bool zh;

const char *L(const char *cn, const char *en);
void load_language();
