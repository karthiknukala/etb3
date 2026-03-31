#include <stdlib.h>
#include <string.h>

typedef struct etb_symbol_table {
  char **items;
  size_t count;
  size_t capacity;
} etb_symbol_table;

static etb_symbol_table ETB_GLOBAL_SYMBOLS = {0};

const char *etb_symbol_intern(const char *text) {
  size_t index;
  char **grown;
  for (index = 0U; index < ETB_GLOBAL_SYMBOLS.count; ++index) {
    if (strcmp(ETB_GLOBAL_SYMBOLS.items[index], text) == 0) {
      return ETB_GLOBAL_SYMBOLS.items[index];
    }
  }
  if (ETB_GLOBAL_SYMBOLS.count == ETB_GLOBAL_SYMBOLS.capacity) {
    ETB_GLOBAL_SYMBOLS.capacity =
        ETB_GLOBAL_SYMBOLS.capacity == 0U ? 32U : ETB_GLOBAL_SYMBOLS.capacity * 2U;
    grown = (char **)realloc(ETB_GLOBAL_SYMBOLS.items,
                             sizeof(char *) * ETB_GLOBAL_SYMBOLS.capacity);
    if (grown == NULL) {
      return NULL;
    }
    ETB_GLOBAL_SYMBOLS.items = grown;
  }
  ETB_GLOBAL_SYMBOLS.items[ETB_GLOBAL_SYMBOLS.count] = strdup(text);
  if (ETB_GLOBAL_SYMBOLS.items[ETB_GLOBAL_SYMBOLS.count] == NULL) {
    return NULL;
  }
  ETB_GLOBAL_SYMBOLS.count += 1U;
  return ETB_GLOBAL_SYMBOLS.items[ETB_GLOBAL_SYMBOLS.count - 1U];
}
