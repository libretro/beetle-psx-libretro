#ifndef MC_ASYNC_H__
#define MC_ASYNC_H__

/* Asynchronous memory-card file writer. See mc_async.c. */

#include <stdint.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Writes one whole card image to its file. */
typedef void (*mc_async_write_t)(const char *path, const uint8_t *data, uint32_t size);

/* Starts the writer thread. Without one (threads unavailable, or the
 * thread could not be created) every enqueue writes inline. */
void mc_async_init(mc_async_write_t write);

/* Emulation thread only. Snapshots the card, so the caller may change the
 * source buffer as soon as this returns. */
void mc_async_enqueue(const char *path, const uint8_t *data, uint32_t size);

/* Writes everything still pending, then stops and joins the writer. */
void mc_async_flush_and_stop(void);

#ifdef MC_ASYNC_TEST
/* Non-zero: the next enqueues behave as if the snapshot could not be
 * allocated. */
extern int mc_async_test_no_memory;
/* Non-zero: mc_async_init behaves as if the thread could not be created. */
extern int mc_async_test_no_thread;
/* Called on the writer thread after a scan that found nothing to write,
 * before it decides whether to sleep or to leave. */
extern void (*mc_async_test_after_empty_scan)(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
