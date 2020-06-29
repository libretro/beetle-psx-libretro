#pragma once

#include "../atlas/atlas.hpp"
#include <set>
#include <map>
#include <memory>
#include <rthreads/rthreads.h>
#include "../vulkan/device.hpp"
#include <fstream>
#include "config_parser.h"
#include "libretro.h"

extern retro_log_printf_t log_cb;

// #define VERBOSE_TEXTURE_TRACKING

#define TT_LOG(...) log_cb(__VA_ARGS__)
#ifdef VERBOSE_TEXTURE_TRACKING
#define TT_LOG_VERBOSE(...) TT_LOG(__VA_ARGS__)
#else
#define TT_LOG_VERBOSE(...) do {} while (0)
#endif

namespace PSX {

class DumpLog {
public:
    DumpLog();
    void upload(uint64_t frame, Rect rect, uint32_t hash);
    void dump(uint64_t frame, uint32_t hash, uint32_t palette_hash, TextureMode mode);
private:
    std::ofstream dump_stream;
};

class BlitLog {
public:
    BlitLog();
    ~BlitLog();
    void upload(Rect rect, uint32_t hash);
    void blit(Rect dst, Rect src);
    void clear(Rect rect);
    void set_frame(uint32_t frame);
private:
    void comma();
    uint32_t frame = 0;
    bool need_comma = false;
    std::ofstream dump_stream;
};

struct HdTextureId {
    uint32_t hash;
    uint32_t palette_hash;

    bool operator>(const HdTextureId &other) const
    {
        if (hash != other.hash)
			return hash > other.hash;
		return palette_hash > other.palette_hash;
    }
    bool operator<(const HdTextureId &other) const
    {
        if (hash != other.hash)
			return hash < other.hash;
		return palette_hash < other.palette_hash;
    }
};

typedef int RectIndex; // I wanted a newtype but it's too much work in C++, so maybe TODO that later
struct HdTextureHandle {
    RectIndex index;
    uint32_t palette_hash;
    bool fused;

    bool operator==(const HdTextureHandle &other) const
    {
        return index == other.index && palette_hash == other.palette_hash && fused == other.fused;
    }

    bool operator!=(const HdTextureHandle &other) const
    {
        return !(*this == other);
    }

    bool operator>(const HdTextureHandle &other) const
    {
        if (index != other.index)
			return index > other.index;
		if (palette_hash != other.palette_hash)
            return palette_hash > other.palette_hash;
        return fused > other.fused;
    }

    static HdTextureHandle make(RectIndex index, uint32_t palette_hash) {
        return HdTextureHandle(index, palette_hash, false);
    }
    static HdTextureHandle make_fused(RectIndex index) {
        return HdTextureHandle(index, 0, true);
    }
    static HdTextureHandle make_none() {
        return HdTextureHandle::make(-1, 0);
    }

private:
    HdTextureHandle(RectIndex index, uint32_t palette_hash, bool fused)
    : index(index), palette_hash(palette_hash), fused(fused)
    {

    }
};

struct SRect {
    int x;
    int y;
    int width;
    int height;
    SRect(int x, int y, int width, int height):
    x(x), y(y), width(width), height(height) {
        if (width <= 0 || height <= 0) {
            printf("Illegally sized SRect: %d, %d\n", width, height);
            exit(1);
        }
    }
    inline int left() {
        return x;
    }
    inline int right() {
        return x + width;
    }
    inline int top() {
        return y;
    }
    inline int bottom() {
        return y + height;
    }

	inline bool operator==(const SRect &other) const
	{
		return x == other.x && y == other.y && width == other.width && height == other.height;
	}
    inline bool operator!=(const SRect &other) const
    {
        return !(*this == other);
    }
};

struct HdTexture {
    SRect vram_rect;
    SRect texel_rect; // hd texels
    Vulkan::ImageHandle texture;
};

struct DumpedMode {
    TextureMode mode;
    uint32_t palette_hash;

	inline bool operator==(const DumpedMode &other) const
	{
		return mode == other.mode && palette_hash == other.palette_hash;
	}
};

struct UsedMode {
    TextureMode mode;
    unsigned int palette_offset_x;
    unsigned int palette_offset_y;

	inline bool operator==(const UsedMode &other) const
	{
		return mode == other.mode && palette_offset_x == other.palette_offset_x && palette_offset_y == other.palette_offset_y;
	}
};

struct HdImageHandle {
    Vulkan::ImageHandle image;
    int alpha_flags;
};
struct TextureUpload {
    std::vector<uint16_t> image;
    bool dumpable;
    int width;
    int height;
    uint32_t hash;
    std::vector<DumpedMode> dumped_modes;
	std::map<uint32_t, HdImageHandle> textures; // palette hash -> imagehandle
};

struct LoadedImage {
    std::vector<uint8_t> owned_data; // RGBA format
    int width;
    int height;
};

class TextureUploader
{
public:
	virtual ~TextureUploader() = default;
	virtual Vulkan::ImageHandle upload_texture(std::vector<LoadedImage> &image) = 0;
    virtual Vulkan::ImageHandle create_texture(int width, int height, int levels) = 0;
    virtual Vulkan::CommandBufferHandle &command_buffer_hack_fixme() = 0;
};

struct IORequest {
    virtual ~IORequest() = default; // Need some virtual method for dynamic_cast
};

struct DumpRequest : IORequest {
    std::string path;
    int width;
    int height;
    std::vector<uint8_t> bytes;
};
struct LoadRequest : IORequest {
    uint32_t hash;
    uint32_t palette_hash;
};

const int ALPHA_FLAG_OPAQUE = 1;
const int ALPHA_FLAG_SEMI_TRANSPARENT = 2;
const int ALPHA_FLAG_TRANSPARENT = 4;

struct IOResponse {
    uint32_t hash;
    uint32_t palette_hash;
    int alpha_flags;
    std::vector<LoadedImage> levels;
};

class IOChannel {
public:
    IOChannel();
    ~IOChannel();
    slock_t *lock;
    scond_t *cond;
    std::vector<std::unique_ptr<IORequest>> requests;
    std::vector<IOResponse> responses;
    bool done = false;
private:
};

class IOThread {
public:
    IOThread();
    ~IOThread();
    std::shared_ptr<IOChannel> channel;
private:
};

struct Palette {
    uint16_t *data;
    uint32_t hash;
};

struct CachedPaletteHash {
    Rect rect;
    uint32_t hash;
};

//============
// RectTracker
struct TextureRect {
    std::shared_ptr<TextureUpload> upload;
    // the offset into the original upload rect (offset_x + vram_rect.width <= upload->width)
    int offset_x;
    int offset_y;
    SRect vram_rect;
    TextureRect(std::shared_ptr<TextureUpload> upload, int offset_x, int offset_y, SRect vram_rect): 
    upload(upload), offset_x(offset_x), offset_y(offset_y), vram_rect(vram_rect)
    {
    }

    // in vram size (not hd), local to the uploaded data, different hd textures for different palettes could have different sizes anyway
    SRect texture_subrect() const {
        return SRect(offset_x, offset_y, vram_rect.width, vram_rect.height);
    }

	inline bool operator==(const TextureRect &other) const
	{
		return upload.get() == other.upload.get() && offset_x == other.offset_x && offset_y == other.offset_y && vram_rect == other.vram_rect;
	}
    inline bool operator!=(const TextureRect &other) const
    {
        return !(*this == other);
    }
};

// TODO: better name
struct EnduringTextureRect {
    TextureRect texture_rect;
    bool alive;
};

const int LOOKUP_GRID_COLUMNS = 16;
const int LOOKUP_GRID_ROWS = 2;
const int LOOKUP_CELL_WIDTH = 64;
const int LOOKUP_CELL_HEIGHT = 256;

class LookupGrid {
public:
    void insert(SRect r, RectIndex index);
    void get(SRect r, std::unordered_set<RectIndex> &results);
    void clear();
private:
    struct LookupEntry {
        SRect rect;
        RectIndex index;
    };
    std::vector<LookupEntry> cells[LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS]; // Each cell is a psx texture page, 64x256
};

class RectTracker {
public:
    void place(TextureRect texture);
    void upload(SRect rect, std::shared_ptr<TextureUpload> upload);
    void blit(SRect dst, SRect src);
    void clear(SRect rect);
    void releaseDeadHandles();
    std::vector<EnduringTextureRect> textures;
    std::unordered_set<RectIndex>& overlapping(Rect rect, std::unordered_set<RectIndex>& results);

    /**
     * This pointer will be valid until the next upload/blit/clear/endFrame, so use it immediately and don't try anything funny.
     * Returns nullptr when index is out of range
    **/
    TextureRect* get_index(RectIndex index);

    /** Returns nullptr if no texture with the given hash can be found */
    std::shared_ptr<TextureUpload> find_upload(uint32_t hash);
private:
    LookupGrid lookup_grid;
    bool lookup_grid_dirty = false;

    void clear_rect(SRect &rect);
    void rebuild_lookup_grid();
};
// RectTracker
//============

struct FusionRects {
    std::vector<TextureRect> rects;
    Rect vram_rect;
    unsigned int scaleX = 0;
    unsigned int scaleY = 0;

    bool operator==(const FusionRects &other) const {
        return vram_rect == other.vram_rect && scaleX == other.scaleX && scaleY == other.scaleY && rects == other.rects;
    }

    bool operator!=(const FusionRects &other) const {
        return !(*this == other);
    }
};

struct FusedPage {
    Vulkan::ImageHandle texture;

    uint32_t palette;
    Rect full_page_rect;

    bool dirty = false;
    bool dead = false;

    FusionRects fusion;
};

class FusedPages {
public:
    HdTextureHandle get_or_make(Rect page_rect, uint32_t palette, RectTracker &tracker, TextureUploader *uploader);
    HdTexture get_from_handle(HdTextureHandle handle, Vulkan::ImageHandle &default_hd_texture);
    void mark_dirty(Rect rect); // For blit dst, upload, and hd texture load
    void mark_dead(Rect rect); // For clear
    void rebuild_dirty(RectTracker &tracker, TextureUploader *uploader);
    void remove_dead();

    void dbg_print_info();
private:
    std::vector<FusedPage> pages;
};

struct RestorableRect {
    Rect rect;
    uint32_t hash;
    std::vector<TextureRect> to_restore;
};

class DbgHotkey {
public:
    DbgHotkey(retro_key key): key(key) {}
    bool query();
    retro_key key;
private:
    bool was_key_down = false;
};

struct CacheEntry {
    Rect rect;
    HdTextureHandle handle;
};

class HandleLRUCache {
public:
    HandleLRUCache(int max_size): max_size(max_size) { entries.reserve(max_size); }
    std::pair<HdTextureHandle, bool> get(Rect rect, uint32_t palette_hash);
    void insert(Rect rect, uint32_t palette_hash, HdTextureHandle handle);
    void clear();
    int64_t dbg_hits;
    int64_t dbg_misses;
private:
    int max_size;
    std::vector<CacheEntry> entries;
};

//========================================
// Save State
struct TextureRectSaveState {
    uint32_t upload_hash;
    int offset_x;
    int offset_y;
    SRect vram_rect;
};

struct RestorableRectSaveState {
    Rect rect;
    uint32_t hash;
    std::vector<TextureRectSaveState> to_restore;
};

struct TextureTrackerSaveState {
    std::vector<TextureRectSaveState> rects;
    std::vector<RestorableRectSaveState> restorable;
    std::map<uint32_t, TextureUpload> uploads;
};
// End of Save State
//========================================

class TextureTracker {
public:
    TextureTracker();

    TextureTrackerSaveState save_state();
    void load_state(const TextureTrackerSaveState &state);

    // Put texture in highres vram
    void upload(Rect rect, uint16_t *vram);
    void blit(Rect dst, Rect src);
    // Clear highres vram to fallback to lowres
    void clearRegion(Rect rect);
    // Monitor VRAM readback
    void notifyReadback(Rect rect, uint16_t *vram);
    uint32_t dbgHashVram(Rect rect, uint16_t *vram);

    HdTextureHandle get_hd_texture_index(Rect rect, UsedMode &mode, unsigned int page_x, unsigned int page_y, bool &fastpath_capable, bool &cache_hit);
    HdTexture get_hd_texture(HdTextureHandle index);
    void endFrame();
    void on_queues_reset();

	void set_texture_uploader(TextureUploader *t)
	{
		uploader = t;
        std::vector<LoadedImage> default_levels;

        LoadedImage default_image;
        default_image.width = 1;
        default_image.height = 1;
        default_image.owned_data.push_back(0);
        default_image.owned_data.push_back(0);
        default_image.owned_data.push_back(0);
        default_image.owned_data.push_back(0);
        default_levels.push_back(std::move(default_image));
        
        default_hd_texture = uploader->upload_texture(default_levels);
	}

    bool dump_enabled = false;
    bool hd_textures_enabled = false;
private:
    IOThread iothread;
    TextureUploader *uploader;

    Vulkan::ImageHandle default_hd_texture;

    Palette get_palette(Rect palette_rect);
    uint32_t get_palette_hash(Rect palette_rect);

    std::vector<RectMatch> dump_ignore;

    std::set<HdTextureId> known_files;
    std::vector<CachedPaletteHash> cached_palette_hashes;
    std::vector<RestorableRect> restorable_rects;
    FusedPages fused_pages;
    uint64_t frame = 0;

    RectTracker tracker;
    HandleLRUCache handle_cache = 4;
    void dump_texture(std::shared_ptr<TextureUpload> &upload, UsedMode &mode, DumpedMode dump_mode);
    
    DbgHotkey frame_dump_key = RETROK_LEFTBRACKET; // disgusting
    std::ofstream *frame_dump = nullptr;
    bool frame_dump_need_comma = false;

    std::unique_ptr<BlitLog> blit_log;
    std::unique_ptr<DumpLog> dump_log;

    DbgHotkey hd_toggle_key = RETROK_RIGHTBRACKET;

    void load_hd_texture(uint32_t hash);

    DbgHotkey reload_key = RETROK_QUOTE;
    void reload_textures_from_disk();

    DbgHotkey fastpath_key = RETROK_SEMICOLON;
    bool fastpath_enabled = true;

    void dump_image(TextureUpload &upload, UsedMode &mode);

    void clear_palette_cache(Rect rect);

    /** Returns nullptr if no texture with the given hash can be found */
    std::shared_ptr<TextureUpload> find_upload(uint32_t hash);
};

}