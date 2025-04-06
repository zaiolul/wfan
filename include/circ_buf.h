#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

typedef struct circular_buffer {
  void *data;
  void *data_end;
  size_t cap;
  size_t count;
  size_t value_size;
  void* end;

} circ_buf_t;

circ_buf_t *circ_buf_init(size_t element_size, size_t cap);

int circ_buf_put(circ_buf_t *buf, void *value);
void *circ_buf_get_idx(circ_buf_t *buf, int idx);
int circ_buf_full(circ_buf_t *buf);
void circ_buf_free(circ_buf_t **buf);