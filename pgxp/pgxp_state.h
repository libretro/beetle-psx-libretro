#ifndef PGXP_STATE_H
#define PGXP_STATE_H

#include <stdint.h>
#include <string.h>
#include <boolean.h>

struct pgxp_saved_state;
struct pgxp_memory_state;
struct pgxp_vertex_state;

struct pgxp_saved_state *PGXP_StateSave(void);
bool PGXP_StatePrepare(struct pgxp_saved_state *state);
void PGXP_StateLoad(struct pgxp_saved_state *state);
void PGXP_StateFree(struct pgxp_saved_state *state);
void PGXP_StateStopTracking(void);

struct pgxp_memory_state *PGXP_MemoryStateSave(void);
void PGXP_MemoryStateLoad(const struct pgxp_memory_state *state);
void PGXP_MemoryStateFree(struct pgxp_memory_state *state);
void PGXP_MemoryStopTracking(void);

bool PGXP_VertexStateSave(struct pgxp_vertex_state **saved);
bool PGXP_VertexStatePrepare(const struct pgxp_vertex_state *saved, void **reserve);
void PGXP_VertexStateLoad(const struct pgxp_vertex_state *saved, void **reserve);
void PGXP_VertexStateFree(struct pgxp_vertex_state *saved);

size_t PGXP_CPUStateSize(void);
size_t PGXP_GTEStateSize(void);
size_t PGXP_GPUStateSize(void);
void PGXP_CPUState(uint8_t *data, bool load);
void PGXP_GTEState(uint8_t *data, bool load);
void PGXP_GPUState(uint8_t *data, bool load);

static inline void pgxp_state_copy(uint8_t **cursor, void *value,
      size_t size, bool load)
{
   if (load)
      memcpy(value, *cursor, size);
   else
      memcpy(*cursor, value, size);
   *cursor += size;
}

#endif
