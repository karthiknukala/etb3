cmake_minimum_required(VERSION 3.24)
project(etb3 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

include(CTest)

add_library(etb STATIC
  src/core/term.c
  src/core/ast.c
  src/core/parser.c
  src/core/canon.c
  src/core/cbor.c
  src/core/sha256.c
  src/core/symbol_table.c
  src/engine/unify.c
  src/engine/tables.c
  src/engine/negation.c
  src/engine/delegation.c
  src/engine/strata.c
  src/engine/eval.c
  src/engine/trace.c
  src/engine/cert.c
  src/runtime/artifact_store.c
  src/runtime/capability.c
  src/runtime/registry.c
  src/runtime/snapshot.c
  src/runtime/peer.c
  src/runtime/swim.c
  src/runtime/signer_soft.c
  src/runtime/signer_macos.c
  src/runtime/daemon.c
  src/net/frame.c
  src/net/rpc.c
  src/net/tls.c
)

target_include_directories(etb
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine
)

if(APPLE)
  target_link_libraries(etb PRIVATE "-framework Security" "-framework CoreFoundation")
endif()

add_executable(etbd src/cli/etbd.c)
target_link_libraries(etbd PRIVATE etb)
target_include_directories(etbd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

add_executable(etbctl src/cli/etbctl.c)
target_link_libraries(etbctl PRIVATE etb)

add_executable(sample_concat
  adapters/samples/sample_concat.c
  src/core/term.c
  src/core/cbor.c
)
target_include_directories(sample_concat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

add_executable(sample_receipt
  adapters/samples/sample_receipt.c
  src/core/term.c
  src/core/cbor.c
  src/core/sha256.c
)
target_include_directories(sample_receipt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

if(BUILD_TESTING)
  add_executable(etb_tests tests/test_main.c)
  target_link_libraries(etb_tests PRIVATE etb)
  target_compile_definitions(etb_tests PRIVATE
    ETB_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    ETB_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}")
  add_test(NAME etb_tests COMMAND etb_tests)
  add_test(
    NAME etb_two_node_banking
    COMMAND /bin/zsh ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/two_node_banking.sh
            ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
endif()
