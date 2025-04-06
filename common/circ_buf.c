#include "circ_buf.h"

circ_buf_t *circ_buf_init(size_t element_size, size_t cap)
{
  circ_buf_t *buf;
  size_t size;

  size = element_size * cap;
  buf = malloc(sizeof(circ_buf_t));
  if (!buf)
      return NULL;

  buf->data = malloc(size);
  if (!buf->data) {
      free(buf);
      return NULL;
  }

  buf->count = 0; 
  buf->cap = cap;
  buf->value_size = element_size;
  buf->end = buf->data;
  buf->data_end = (char *)buf->data + cap * element_size;
  return buf;
}

int circ_buf_put(circ_buf_t *buf, void *value)
{
  if (!buf || !value)
    return -1;
    
  memcpy(buf->end, value, buf->value_size);
  buf->end = (char *)buf->end + buf->value_size;

  if (buf->end == buf->data_end)
    buf->end = buf->data;
  
  if (buf->cap != buf->count)
    buf->count ++;
}

void *circ_buf_get_idx(circ_buf_t *buf, int idx)
{
  if(!buf || idx < 0 || idx >= buf->count)
    return NULL;

  char *pos = (char *)buf->data + idx * buf->value_size;
  int rem = ((char *)buf->data_end - (char *)buf->end) / buf->value_size - 1;

  if (buf->cap == buf->count && buf->end > buf->data) {
    if (idx > rem)
      pos -= (char *)buf->data_end - (char *)buf->end;
    else
      pos += (char *)buf->end - (char *)buf->data;
  }

  return pos;
}

int circ_buf_full(circ_buf_t *buf)
{
  return buf->cap == buf->count;
}

void circ_buf_free(circ_buf_t **buf)
{
  free((*buf)->data);
  free(*buf);
  *buf = NULL;
} 