/* Full struct layouts (XXH32_state_t etc.) are only visible when this is set;
 * otherwise XXH_IMPLEMENTATION hits sizeof/incomplete-type errors. */
#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include "xxhash.h"
