/* Keep the host's legacy __assert declaration separate from the console ABI.
 * assert.h can intentionally be included again after changing NDEBUG. */
#pragma push_macro("__assert")
#undef __assert
#include_next <assert.h>
#pragma pop_macro("__assert")
