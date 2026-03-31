#ifndef ETB_PARSER_H
#define ETB_PARSER_H

#include <stdbool.h>
#include <stddef.h>

#include "etb/ast.h"

bool etb_parse_program_text(const char *source, etb_program *program, char *error,
                            size_t error_size);
bool etb_parse_file(const char *path, etb_program *program, char *error,
                    size_t error_size);
bool etb_parse_atom_text(const char *source, etb_atom *atom, char *error,
                         size_t error_size);

#endif
