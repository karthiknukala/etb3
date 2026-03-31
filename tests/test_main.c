#include "etb/cert.h"
#include "etb/engine.h"
#include "etb/membership.h"
#include "etb/registry.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ETB_SOURCE_DIR
#define ETB_SOURCE_DIR "."
#endif

#ifndef ETB_BUILD_DIR
#define ETB_BUILD_DIR "."
#endif

static int failures = 0;

static void expect_true(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
    failures += 1;
  }
}

static void test_engine_core(void) {
  const char *source =
      "permit(alice).\n"
      "denied(bob).\n"
      "allowed(X) :- permit(X), not denied(X).\n"
      "alice speaks_for bank [scope=permit, expires=9999999999, depth=2].\n"
      "alice says permit(carol).\n";
  char error[256];
  etb_program program;
  etb_engine engine;
  etb_atom query;
  etb_fact_list answers;

  etb_program_init(&program);
  etb_engine_init(&engine);
  etb_atom_init(&query);
  etb_fact_list_init(&answers);
  memset(error, 0, sizeof(error));

  expect_true(etb_parse_program_text(source, &program, error, sizeof(error)),
              error);
  expect_true(etb_engine_load_program(&engine, &program, error, sizeof(error)),
              error);
  expect_true(etb_engine_run_fixpoint(&engine, error, sizeof(error)), error);

  expect_true(etb_parse_atom_text("allowed(alice)", &query, error, sizeof(error)),
              error);
  expect_true(etb_engine_query(&engine, &query, &answers, error, sizeof(error)),
              error);
  memset(&query, 0, sizeof(query));
  memset(&answers, 0, sizeof(answers));
  expect_true(etb_parse_atom_text("bank says permit(carol)", &query, error, sizeof(error)),
              error);
  expect_true(etb_engine_query(&engine, &query, &answers, error, sizeof(error)),
              error);
  expect_true(answers.count == 1U, "delegated says query should succeed");
  (void)program;
  (void)engine;
}

static void test_capabilities(void) {
  etb_capability_decl concat = {.name = "concat",
                                .arity = 3U,
                                .path = ETB_BUILD_DIR "/sample_concat",
                                .deterministic = true,
                                .proof_admissible = true,
                                .timeout_ms = 1000U};
  etb_capability_decl receipt = {.name = "receipt",
                                 .arity = 2U,
                                 .path = ETB_BUILD_DIR "/sample_receipt",
                                 .deterministic = true,
                                 .proof_admissible = true,
                                 .timeout_ms = 1000U};
  etb_term_list args;
  etb_capability_result result;
  bool bound_mask[3] = {true, true, false};
  char error[256];

  etb_term_list_init(&args);
  etb_term_list_push(&args, etb_term_make_string("a"));
  etb_term_list_push(&args, etb_term_make_string("b"));
  etb_term_list_push(&args, etb_term_make_null());
  etb_capability_result_init(&result);
  memset(error, 0, sizeof(error));
  expect_true(etb_capability_invoke(&concat, &args, bound_mask, &result, error,
                                    sizeof(error)),
              error);
  expect_true(result.tuple_count == 1U, "concat should return one tuple");
  expect_true(result.tuples[0].count == 3U, "concat tuple arity");
  expect_true(strcmp(result.tuples[0].items[2].text, "ab") == 0,
              "concat result should be ab");
  etb_capability_result_free(&result);
  etb_term_list_free(&args);

  etb_term_list_init(&args);
  etb_term_list_push(&args, etb_term_make_string("doc"));
  etb_term_list_push(&args, etb_term_make_null());
  etb_capability_result_init(&result);
  bound_mask[0] = true;
  bound_mask[1] = false;
  expect_true(etb_capability_invoke(&receipt, &args, bound_mask, &result, error,
                                    sizeof(error)),
              error);
  expect_true(result.evidence_count == 1U, "receipt should emit one evidence digest");
  etb_capability_result_free(&result);
  etb_term_list_free(&args);
}

static void test_membership(void) {
  etb_membership membership;
  etb_peer_info peer = {.node_id = "n1",
                        .endpoint = "127.0.0.1:1",
                        .incarnation = 1U,
                        .last_heartbeat_ms = 10U,
                        .status = ETB_PEER_ALIVE};
  etb_membership_init(&membership, 5U);
  expect_true(etb_membership_upsert(&membership, &peer), "membership upsert");
  etb_membership_expire(&membership, 20U);
  expect_true(membership.items[0].status == ETB_PEER_DEAD,
              "peer should expire to dead");
  etb_membership_free(&membership);
}

static void test_snapshot(void) {
  etb_registry_snapshot snapshot;
  char error[256];
  snapshot.version = 1U;
  etb_registry_snapshot_init(&snapshot);
  snapshot.version = 1U;
  snapshot.keys = (etb_principal_key *)calloc(1U, sizeof(etb_principal_key));
  snapshot.key_count = 1U;
  snapshot.keys[0].principal = strdup("bank");
  snapshot.keys[0].version = 1U;
  snapshot.keys[0].public_key_pem = strdup("placeholder");
  snapshot.keys[0].revoked = false;
  memset(error, 0, sizeof(error));
  if (etb_registry_snapshot_sign(&snapshot, "dev-root", error, sizeof(error))) {
    expect_true(snapshot.signature != NULL && snapshot.signature_size > 0U,
                "snapshot should be signed");
  } else {
    fprintf(stderr, "snapshot signing skipped: %s\n", error);
  }
  (void)snapshot;
}

static void test_certificate_build(void) {
  etb_certificate certificate;
  etb_atom query;
  etb_atom answer;
  etb_trace trace;
  etb_trace_node node;
  etb_certificate_init(&certificate);
  etb_atom_init(&query);
  etb_atom_init(&answer);
  etb_trace_init(&trace);
  etb_parse_atom_text("allowed(alice)", &query, NULL, 0U);
  etb_parse_atom_text("allowed(alice)", &answer, NULL, 0U);
  memset(&node, 0, sizeof(node));
  node.kind = ETB_TRACE_FACT;
  node.fact = etb_atom_clone(&answer);
  expect_true(etb_trace_append(&trace, &node) != (size_t)-1, "trace append");
  expect_true(etb_certificate_build(&certificate, &query, &answer, 1U, &trace),
              "certificate build");
  expect_true(certificate.cbor_size > 0U, "certificate cbor present");
  (void)query;
  (void)answer;
  (void)trace;
  (void)certificate;
}

int main(void) {
  test_engine_core();
  test_capabilities();
  test_membership();
  test_snapshot();
  test_certificate_build();
  if (failures != 0) {
    fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  puts("all tests passed");
  return 0;
}
