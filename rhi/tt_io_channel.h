#ifndef TT_IO_CHANNEL_H__
#define TT_IO_CHANNEL_H__

/* The channel between the render thread and the HD texture IO workers.
 * See tt_io_channel.c. */

#include <stdint.h>
#include <stddef.h>
#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

/* First member of whatever travels through the channel. */
typedef struct tt_io_node
{
   struct tt_io_node *next;
} tt_io_node;

typedef struct tt_io_channel tt_io_channel;

typedef void (*tt_io_free_t)(tt_io_node *node);
typedef bool (*tt_io_match_t)(const tt_io_node *node, void *ctx);

/* How many requests of each priority the workers can see at once. The
 * rest wait with the render thread, where it can still reorder them. */
#define TT_IO_WINDOW 64

/* One reference, the caller's. NULL if it could not be set up. */
tt_io_channel *tt_io_channel_new(tt_io_free_t free_request, tt_io_free_t free_response);
void tt_io_channel_acquire(tt_io_channel *c);
/* The last release frees the channel and whatever is still in it. */
void tt_io_channel_release(tt_io_channel *c);

/* ---- render thread ---- */

/* Queues a request; high priority is served before low. The channel owns
 * the node from here. False (and the node freed) without a channel. */
bool tt_io_channel_push(tt_io_channel *c, tt_io_node *request, bool high);
/* Moves the first low priority request that matches, and that the
 * workers cannot see yet, to the back of the high priority ones. False if
 * there is none - never queued, already in the workers' window, or taken. */
bool tt_io_channel_promote(tt_io_channel *c, tt_io_match_t match, void *ctx);
/* Every response delivered so far, oldest first; the caller owns them. */
tt_io_node *tt_io_channel_take_responses(tt_io_channel *c);
/* Waits for a response to be there, for at most timeout_us. */
void tt_io_channel_wait_response(tt_io_channel *c, int64_t timeout_us);
/* Tells the workers to leave, drops what they cannot see yet, and gives
 * up the caller's reference. Requests not yet taken are dropped. */
void tt_io_channel_stop(tt_io_channel *c);

/* ---- workers ---- */

/* The next request, high priority first; sleeps while there is none.
 * NULL once the channel is stopped. The caller owns the node. */
tt_io_node *tt_io_channel_pop(tt_io_channel *c);
void tt_io_channel_push_response(tt_io_channel *c, tt_io_node *response);

#ifdef __cplusplus
}
#endif

#endif
