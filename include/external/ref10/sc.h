#ifndef SC_H
#define SC_H

#include "plugins.h"

/*
The set of scalars is \Z/l
where l = 2^252 + 27742317777372353535851937790883648493.
*/

PLUGIN_API void sc_reduce(unsigned char *);
PLUGIN_API void sc_muladd(unsigned char *,const unsigned char *,const unsigned char *,const unsigned char *);

#endif
