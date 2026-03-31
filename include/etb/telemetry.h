#ifndef ETB_TELEMETRY_H
#define ETB_TELEMETRY_H

#include <stdbool.h>
#include <stddef.h>

const char *etb_telemetry_event_file(void);

void etb_telemetry_emit_node_started(const char *node_id, const char *endpoint,
                                     const char *program_path, long pid);

void etb_telemetry_emit_request_received(const char *to_node_id,
                                         const char *to_endpoint,
                                         const char *from_node_id,
                                         const char *from_endpoint,
                                         const char *op,
                                         const char *query_text,
                                         const char *principal);

void etb_telemetry_emit_query_started(const char *node_id,
                                      const char *query_text);

void etb_telemetry_emit_query_finished(const char *node_id,
                                       const char *query_text, bool success,
                                       size_t answer_count,
                                       size_t bundle_count,
                                       const char *error_text);

void etb_telemetry_emit_bundle_imported(const char *node_id,
                                        const char *source_node_id,
                                        const char *query_text);

void etb_telemetry_emit_logic_invoke(const char *node_id,
                                     const char *goal_text,
                                     const char *target_principal,
                                     const char *target_endpoint);

#endif
