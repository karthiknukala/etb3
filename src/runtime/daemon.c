#include "etb/cert.h"
#include "etb/engine.h"

#include <stdio.h>
#include <stdlib.h>

bool etb_daemon_run_local(const char *program_path, const char *query_text,
                          etb_certificate *certificate, char *error,
                          size_t error_size) {
  etb_program program;
  etb_engine engine;
  etb_atom query;
  etb_fact_list answers;
  size_t index;
  etb_program_init(&program);
  etb_engine_init(&engine);
  etb_atom_init(&query);
  etb_fact_list_init(&answers);
  if (!etb_parse_file(program_path, &program, error, error_size) ||
      !etb_engine_load_program(&engine, &program, error, error_size) ||
      !etb_engine_run_fixpoint(&engine, error, error_size) ||
      !etb_parse_atom_text(query_text, &query, error, error_size) ||
      !etb_engine_query(&engine, &query, &answers, error, error_size)) {
    etb_atom_free(&query);
    etb_fact_list_free(&answers);
    etb_program_free(&program);
    etb_engine_free(&engine);
    return false;
  }
  {
    etb_atom *atoms = (etb_atom *)calloc(answers.count == 0U ? 1U : answers.count,
                                         sizeof(etb_atom));
    if (answers.count > 0U && atoms == NULL) {
      snprintf(error, error_size, "out of memory");
      etb_atom_free(&query);
      etb_fact_list_free(&answers);
      etb_program_free(&program);
      etb_engine_free(&engine);
      return false;
    }
    for (index = 0U; index < answers.count; ++index) {
      atoms[index] = etb_atom_clone(&answers.items[index].atom);
    }
    if (!etb_certificate_build(certificate, &query, atoms, answers.count,
                               &engine.trace)) {
      snprintf(error, error_size, "failed to build certificate");
      for (index = 0U; index < answers.count; ++index) {
        etb_atom_free(&atoms[index]);
      }
      free(atoms);
      etb_atom_free(&query);
      etb_fact_list_free(&answers);
      etb_program_free(&program);
      etb_engine_free(&engine);
      return false;
    }
    for (index = 0U; index < answers.count; ++index) {
      etb_atom_free(&atoms[index]);
    }
    free(atoms);
  }
  return true;
}
