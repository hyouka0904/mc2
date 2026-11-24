// SPDX-License-Identifier: GPL-3.0-only
// unit_savelayout only needs sizeof() of game structures, but in Debug builds
// (_ARMOR) the game headers emit inline checks that reference a few library
// symbols. These stand-ins are never called.
#if defined(_ARMOR)
#include "mclib.h"

int Stuff::ArmorLevel = 0;
Stuff::Signature::~Signature() {}
void Stuff::Is_Signature_Bad(const volatile Signature*) {}
void Stuff::MStringRepresentation::TestInstance() const {}
int __cdecl InternalFunctionPause(const char*, ...) { return 0; }
#endif
