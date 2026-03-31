#ifndef ETB_CANON_H
#define ETB_CANON_H

#include <stdbool.h>
#include <stddef.h>

#include "etb/ast.h"

bool etb_atom_relation_key(const etb_atom *atom, char **key);
bool etb_atom_canonical_text(const etb_atom *atom, char **text);
bool etb_term_canonical_text(const etb_term *term, char **text);

#endif
