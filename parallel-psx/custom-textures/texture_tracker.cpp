#include "texture_tracker.hpp"
#include "libretro.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "libretro-common/include/retro_dirent.h"
#include <assert.h>
#include "dbg_input_callback.h"
#include "image_io.hpp"
#include <cmath>

// Actually using the implementation in deps/zlib/crc32.c I think
#include "scrc32.h"

extern char retro_cd_base_name[4096];
extern char retro_cd_base_directory[4096];
#ifdef _WIN32
   static char retro_slash = '\\';
#else
   static char retro_slash = '/';
#endif

namespace PSX {

std::string dump_path() {
    std::string fullpath;

    fullpath += retro_cd_base_directory;
    fullpath += retro_slash;
    fullpath += retro_cd_base_name;
    fullpath += "-texture-dump";
    fullpath += retro_slash;

    return fullpath;
}

std::string replacements_path() {
    std::string fullpath;

    fullpath += retro_cd_base_directory;
    fullpath += retro_slash;
    fullpath += retro_cd_base_name;
    fullpath += "-texture-replacements";
    fullpath += retro_slash;

    return fullpath;
}

std::string replacement_filename_from_hash(uint32_t hash, uint32_t palette_hash) {
    std::ostringstream oss;
    oss << replacements_path() << std::hex << hash << "-" << palette_hash << ".png";
    return oss.str();
}

inline uint8_t *loaded_pixel(LoadedImage &image, int x, int y) {
    return &image.owned_data[(y * image.width + x) * 4];
}

LoadedImage generate_mip(LoadedImage &higher) {
    // Generate custom mipmaps in order to avoid transparent (0, 0, 0, 0) and semi-transparent (r, g, b, a>=128)
    // mixing to create some dark opaque value (r, g, b, a<128).

    LoadedImage result;
    // Assumes higher.width and higher.height are both divisible by 2 (and also therefore > 1)
    result.width = higher.width / 2;
    result.height = higher.height / 2;
    result.owned_data.resize(result.width * result.height * 4);
    for (int y = 0; y < result.height; y++) {
        for (int x = 0; x < result.width; x++) {
            uint8_t *src00 = loaded_pixel(higher, x * 2 + 0, y * 2 + 0);
            uint8_t *src10 = loaded_pixel(higher, x * 2 + 1, y * 2 + 0);
            uint8_t *src01 = loaded_pixel(higher, x * 2 + 0, y * 2 + 1);
            uint8_t *src11 = loaded_pixel(higher, x * 2 + 1, y * 2 + 1);
            
            int numTransparent = 0;
            if (src00[0] == 0 && src00[1] == 0 && src00[2] == 0 && src00[3] == 0) numTransparent += 1;
            if (src10[0] == 0 && src10[1] == 0 && src10[2] == 0 && src10[3] == 0) numTransparent += 1;
            if (src01[0] == 0 && src01[1] == 0 && src01[2] == 0 && src01[3] == 0) numTransparent += 1;
            if (src11[0] == 0 && src11[1] == 0 && src11[2] == 0 && src11[3] == 0) numTransparent += 1;

            uint8_t *dst = loaded_pixel(result, x, y);
            if (numTransparent > 2) {
                dst[0] = 0;
                dst[1] = 0;
                dst[2] = 0;
                dst[3] = 0;
            } else {
                int r = src00[0] + src10[0] + src01[0] + src11[0];
                int g = src00[1] + src10[1] + src01[1] + src11[1];
                int b = src00[2] + src10[2] + src01[2] + src11[2];
                int a = src00[3] + src10[3] + src01[3] + src11[3];

                int numNotTransparent = 4 - numTransparent;
                dst[0] = r / numNotTransparent;
                dst[1] = g / numNotTransparent;
                dst[2] = b / numNotTransparent;
                dst[3] = a / numNotTransparent;
            }
        }
    }
    return result;
}

LoadedImage convert_tri_to_psx(uint8_t *image, int width, int height, int& alpha_flags) {
    LoadedImage result;
    result.width = width;
    result.height = height;
    result.owned_data.resize(width * height * 4);
    alpha_flags = 0;
    for (int i = 0; i < result.owned_data.size(); i += 4) {
        uint8_t *src = &image[i];
        uint8_t *dst = &result.owned_data[i];
        if (src[3] == 0) {
            // Transparent
            alpha_flags |= ALPHA_FLAG_TRANSPARENT;
            dst[0] = 0;
            dst[1] = 0;
            dst[2] = 0;
            dst[3] = 0;
        } else if (src[3] == 255) {
            alpha_flags |= ALPHA_FLAG_OPAQUE;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
                // Opaque black
                dst[0] = 1;
                dst[1] = 1;
                dst[2] = 1;
                dst[3] = 0;
            } else {
                // Opaque
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 0;
            }
        } else {
            alpha_flags |= ALPHA_FLAG_SEMI_TRANSPARENT;
            if (src[0] == 0 && src[1] == 0 && src[2] == 0) {
                // (0, 0, 0, 255) is a special reserved value
                dst[0] = 1;
                dst[1] = 1;
                dst[2] = 1;
                dst[3] = 255;
            } else {
                // Semi-transparent
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 255;
            }
        }
    }
    return result;
}

std::vector<LoadedImage> prepare_texture(RGBAImage &image, int& alpha_flags) {
    std::vector<LoadedImage> levels;
    int width = image.width;
    int height = image.height;
    levels.push_back(convert_tri_to_psx(image.data, width, height, alpha_flags));
    while (width % 2 == 0 && height % 2 == 0) {
        levels.push_back(generate_mip(levels.back()));

        width /= 2;
        height /= 2;
    }
    return levels;
}

/*
void convert_psx_to_tri(uint8_t *image, int width, int height) {
    for (int i = 0; i < width * height * 4; i += 4) {
        uint8_t *pixel = &image[i];
        if (pixel[3] == 0) {
            if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0) {
                // Transparent
                // do nothing, pixel is already in the correct format
            } else {
                // Opaque
                pixel[3] = 255;
            }
        } else {
            // Semi-transparent
            pixel[3] = 127;
        }
    }
}
*/

void io_thread(void *user_data) {
    std::shared_ptr<IOChannel> *ptr_to_ptr = (std::shared_ptr<IOChannel> *)user_data;
    std::shared_ptr<IOChannel> channel = *ptr_to_ptr;
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread starting\n");
    bool finished = false;
    while (!finished) {
        slock_lock(channel->lock);
        while (channel->requests.size() == 0 && !channel->done) {
            scond_wait(channel->cond, channel->lock);
        }
        if (channel->done) {
            finished = true;
        }
        std::vector<std::unique_ptr<IORequest>> requests = std::move(channel->requests); // Take all requests
        channel->requests.clear();
        slock_unlock(channel->lock);

        if (finished) {
            break;
        }

        for (std::unique_ptr<IORequest> &owned : requests) {
            if (LoadRequest *request = dynamic_cast<LoadRequest*>(owned.get())) {
                uint32_t hash = request->hash;
                uint32_t palette_hash = request->palette_hash;
                // TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread sees: %x-%x\n", hash, palette_hash);

                // Read in texture
                std::string path = replacement_filename_from_hash(hash, palette_hash);
                // TODO: use formats/image.h instead of stb_image?
                auto image = load_image(path.c_str());
                if (image->data != nullptr) {
                    // convert_tri_to_psx(image.data, image.width, image.height);
                
                    // Stick the response in the other vector
                    int alpha_flags_out = 0;
                    auto levels = prepare_texture(*image, alpha_flags_out);
                    IOResponse response = { hash, palette_hash, alpha_flags_out, std::move(levels) };

                    slock_lock(channel->lock);
                    channel->responses.push_back(std::move(response));
                    slock_unlock(channel->lock);
                } else {
                    TT_LOG(RETRO_LOG_ERROR, "failed to load: %s\n", path.c_str());
                }
            } else if (DumpRequest *dump = dynamic_cast<DumpRequest*>(owned.get())) {
                // TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread dumping: %s\n", dump->path.c_str());
                int success = write_image(dump->path.c_str(), dump->width, dump->height, dump->bytes.data());
                if (success == 0) {
                    TT_LOG(RETRO_LOG_ERROR, "failed to write to: %s\n", dump->path.c_str());
                }
            }
        }
    }
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "io thread ending\n");
}

IOChannel::IOChannel() {
    lock = slock_new();
    cond = scond_new();
    // TODO: check for NULL
}
IOChannel::~IOChannel() {
    slock_free(lock);
    scond_free(cond);
}

IOThread::IOThread() {
    channel = std::make_shared<IOChannel>();
    sthread_t *thread = sthread_create(io_thread, &channel);
    sthread_detach(thread);
}
IOThread::~IOThread() {
    slock_lock(channel->lock);
    channel->done = true;
    slock_unlock(channel->lock);
    scond_signal(channel->cond);
}

void TextureTracker::dump_image(TextureUpload &upload, UsedMode &mode) {
    uint32_t hash = upload.hash;

    std::vector<uint8_t> bytes;

    // from glsl/vram.h
    int shift;
    switch (mode.mode) {
        case TextureMode::ABGR1555:
            shift = 0;
            break;
        case TextureMode::Palette8bpp:
            shift = 1;
            break;
        case TextureMode::Palette4bpp:
            shift = 2;
            break;
        case TextureMode::None:
        default:
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Tried to dump unused texture %x.\n", hash);
            return; // Early out
    }

    std::ostringstream suffixs;
    suffixs << dump_path() << std::hex << hash;

    uint16_t *palette = nullptr;
    uint32_t palette_hash = 0;
    if (mode.mode == TextureMode::Palette4bpp || mode.mode == TextureMode::Palette8bpp) {
        Rect palette_rect(mode.palette_offset_x, mode.palette_offset_y, mode.mode == TextureMode::Palette8bpp ? 256 : 16, 1);
        Palette p = get_palette(palette_rect);
        if (p.data != nullptr) {
            palette = p.data;
            suffixs << "-" << std::hex << p.hash;
            palette_hash = p.hash;
        }
    }

    if (dump_log != nullptr) {
        dump_log->dump(frame, hash, palette_hash, mode.mode);
    }

    if (palette != nullptr) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping palette %i, %i.\n", mode.palette_offset_x, mode.palette_offset_y);
    } else if (mode.mode != TextureMode::ABGR1555) {
        suffixs << "-missing";
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "MISSING palette %i, %i.\n", mode.palette_offset_x, mode.palette_offset_y);
    }

    int ppp = 1 << shift;
    int bpp = 16 >> shift;
    int mask = (1 << bpp) - 1;
    for (auto pixel : upload.image) {
        for (int p = 0; p < ppp; p++) {
            uint16_t subpixel = (pixel >> (p * bpp)) & mask;
            if (mode.mode != TextureMode::ABGR1555 && palette == nullptr) {
                // Missing palette, dump a grayscale version of the image data
                bytes.push_back(255.0 * subpixel / mask);
                bytes.push_back(255.0 * subpixel / mask);
                bytes.push_back(255.0 * subpixel / mask);
                bytes.push_back(255.0);
            } else {
                uint16_t abgr1555;
                if (mode.mode == TextureMode::ABGR1555) {
                    abgr1555 = subpixel;
                } else {
                    abgr1555 = palette[subpixel];
                }
                int r = ((abgr1555 >> 0) & 0x1f) * 255.0 / 31.0;
                int g = ((abgr1555 >> 5) & 0x1f) * 255.0 / 31.0;
                int b = ((abgr1555 >> 10) & 0x1f) * 255.0 / 31.0;
                int a = (abgr1555 >> 15) * 255.0;
                // Convert psx to tri
                if (a == 0) {
                    if (r == 0 && g == 0 && b == 0) {
                        // Transparent
                        // do nothing, pixel is already in the correct format
                    } else {
                        // Opaque
                        a = 255;
                    }
                } else {
                    // Semi-transparent
                    a = 127;
                }
                bytes.push_back(r);
                bytes.push_back(g);
                bytes.push_back(b);
                bytes.push_back(a);
            } 
        }
    }

    suffixs << ".png";
    std::string path = suffixs.str();

    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dump info: mode=%i, w=%i, h=%i, len=%i, bytesLen=%i\n", mode.mode, upload.width, upload.height, upload.image.size(), bytes.size());
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping to %s.\n", path.c_str());

    //stbi_write_png(path.c_str(), upload.width * ppp, upload.height, 4, bytes.data(), 4 * upload.width * ppp);
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting dump: %s\n", path.c_str());
    std::unique_ptr<DumpRequest> dump = std::unique_ptr<DumpRequest>(new DumpRequest);
    dump->path = path;
    dump->width = upload.width * ppp;
    dump->height = upload.height;
    dump->bytes = std::move(bytes);
    
    slock_lock(iothread.channel->lock);
    iothread.channel->requests.push_back(std::move(dump));
    slock_unlock(iothread.channel->lock);
    scond_signal(iothread.channel->cond);
}

std::set<HdTextureId> read_texture_directory(const char *path) {
    std::set<HdTextureId> result;
    RDIR *dir;
    dir = retro_opendir(path);
    if (dir != NULL) {
        while (retro_readdir(dir)) {
            // https://stackoverflow.com/questions/13701657/control-whole-string-with-sscanf
            uint32_t hash;
            uint32_t palette_hash;
            int chars_read;
            const char *name = retro_dirent_get_name(dir);
            if (sscanf(name, "%x-%x.png%n", &hash, &palette_hash, &chars_read) != 2 ||
                chars_read != strlen(name)
            ) {
                continue;
            }

            result.insert({ hash, palette_hash });
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "file found: %s\n", ent->d_name);
        }
        retro_closedir(dir);
    }
    return result;
}

TextureTracker::TextureTracker()
{
    // blit_log = std::unique_ptr<BlitLog>(new BlitLog);
    // dump_log = std::unique_ptr<DumpLog>(new DumpLog);

    known_files = read_texture_directory(replacements_path().c_str());
    TT_LOG(RETRO_LOG_INFO, "num hd textures: %d\n", known_files.size());

    // Read in the dump config file
    dump_ignore = parse_config_file((dump_path() + "/dump.cfg").c_str());
    for (RectMatch m : dump_ignore) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "Ignoring %d,%d,%d,%d\n", m.x, m.y, m.w, m.h);
    }
}

SRect toSRect(Rect rect) {
    return SRect(rect.x, rect.y, rect.width, rect.height);
}
Rect fromSRect(SRect rect) {
    return Rect(rect.x, rect.y, rect.width, rect.height);
}

Palette TextureTracker::get_palette(Rect palette_rect) {
    assert(palette_rect.height == 1);

    static std::unordered_set<RectIndex> overlap;
    for (RectIndex index : tracker.overlapping(palette_rect, overlap)) {
        EnduringTextureRect &other = tracker.textures[index]; // TODO: The `other.alive` check is unnecessary because tracker.overlapping never returns dead indices
        if (fromSRect(other.texture_rect.vram_rect).contains(palette_rect) && other.alive) {
            if (other.texture_rect.offset_x != 0 || other.texture_rect.offset_y != 0) {
                continue; // TODO: handle offset subrects
            }
            int x = palette_rect.x - other.texture_rect.vram_rect.x;
            int y = palette_rect.y - other.texture_rect.vram_rect.y;
            int offset = y * other.texture_rect.vram_rect.width + x;
            uint16_t *data = other.texture_rect.upload->image.data() + offset;
            uint32_t hash = crc32(0, (unsigned char*)data, palette_rect.width * sizeof(uint16_t));
            return { data, hash };
        }
    }
    return { nullptr, 0 };
}

uint32_t TextureTracker::get_palette_hash(Rect palette_rect) {
    for (CachedPaletteHash &cached : cached_palette_hashes) {
        if (cached.rect == palette_rect) {
            return cached.hash;
        }
    }
    Palette palette = get_palette(palette_rect);
    if (palette.data != nullptr) {
        cached_palette_hashes.push_back({ palette_rect, palette.hash });
        return palette.hash;
    }
    return 0; // TODO: better way to indicate no palette found?
}

void TextureTracker::clear_palette_cache(Rect rect) {
    cached_palette_hashes.clear();
}

void TextureTracker::clearRegion(Rect rect) {
    if (blit_log != nullptr) {
        blit_log->clear(rect);
    }
    if (rect.width == 0 || rect.height == 0) {
        // Some games do this, apparently.
        return;
    }
    tracker.clear(toSRect(rect));
    fused_pages.mark_dead(rect);

    clear_palette_cache(rect);
}

bool imageMatches(TextureUpload &upload, Rect rect, uint16_t *vram) {
    unsigned x = rect.x,
             y = rect.y,
             w = rect.width,
             h = rect.height;
    int index = 0;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            if (upload.image[index] != vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]) {
                return false;
            }
            index += 1;
        }
    }
    return true;
}

void TextureTracker::blit(Rect dst, Rect src) {
    if (blit_log != nullptr) {
        blit_log->blit(dst, src);
    }
    tracker.blit(toSRect(dst), toSRect(src));
    fused_pages.mark_dirty(dst);
    fused_pages.rebuild_dirty(tracker, uploader);
    clear_palette_cache(dst);
}

uint32_t TextureTracker::dbgHashVram(Rect rect, uint16_t *vram) {
    std::vector<uint16_t> vec;
    unsigned x = rect.x,
            y = rect.y,
            w = rect.width,
            h = rect.height;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            vec.push_back(vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]);
        }
    }
    uint32_t hash = crc32(0, (unsigned char*)vec.data(), rect.width * rect.height * sizeof(uint16_t));
    return hash;
}

std::pair<SRect, bool> intersect(SRect a, SRect b) {
    int left = MAX(a.left(), b.left());
    int right = MIN(a.right(), b.right());
    int top = MAX(a.top(), b.top());
    int bottom = MIN(a.bottom(), b.bottom());
    int width = right - left;
    int height = bottom - top;
    if (width <= 0 || height <= 0) {
        return std::make_pair(SRect(0, 0, 1, 1), false);
    } else {
        return std::make_pair(SRect(left, top, width, height), true);
    }
}

TextureRect subTexture(TextureRect original, SRect sub_vram_rect) {
    return TextureRect(
        original.upload,
        original.offset_x + sub_vram_rect.left() - original.vram_rect.left(),
        original.offset_y + sub_vram_rect.top() - original.vram_rect.top(),
        sub_vram_rect
    );
}

std::pair<TextureRect, bool> clip_texture_rect_to_vram(TextureRect &t, Rect vram_rect) {
    auto intersection = intersect(t.vram_rect, toSRect(vram_rect));
    if (intersection.second) {
        return std::make_pair(subTexture(t, intersection.first), true);
    } else {
        return std::make_pair(TextureRect(nullptr, 0, 0, SRect(0, 0, 1, 1)), false);
    }
}

void TextureTracker::notifyReadback(Rect rect, uint16_t *vram) {
    // These hacks are my workaround for the dialog frame texture restorable getting evicted by FMVs
    if (rect.width == 96 && rect.height == 224 && rect.y == 0 && (rect.x % 96) == 0) {
        // HACK: Looks like final fmv frame readback for cross fade, ignore
        return;
    }
    if (rect.width == 64 && rect.height == 224 && rect.y == 0 && (rect.x % 64) == 0) {
        // HACK: Looks like final fmv frame readback for cross fade, ignore
        return;
    }

    uint32_t hash = dbgHashVram(rect, vram);
    
    for (auto it = restorable_rects.begin(); it != restorable_rects.end(); ) {
        if (it->rect.intersects(rect)) {
            restorable_rects.erase(it);
        } else {
            it++;
        }
    }

    static std::unordered_set<RectIndex> overlap;

    std::vector<TextureRect> to_restore;
    for (RectIndex index : tracker.overlapping(rect, overlap)) {
        EnduringTextureRect &e = tracker.textures[index];
        if (e.alive) { // TODO: This check is unnecessary because tracker.overlapping never returns dead indices
            // Clip to the requested rect
            auto result = clip_texture_rect_to_vram(e.texture_rect, rect);
            if (result.second) {
                // assert(rect.contains(fromSRect(result.first.vram_rect)));
                to_restore.push_back(result.first);
            }
        }
    }

    restorable_rects.push_back({ rect, hash, std::move(to_restore) });
}

void TextureTracker::upload(Rect rect, uint16_t *vram) {
    clear_palette_cache(rect);

    if (rect.width == FB_WIDTH && rect.height == FB_HEIGHT) {
        // probably loading a save state, this is the entirety of vram
        tracker.clear(toSRect(rect));
        fused_pages.mark_dead(rect);
        return;
    }

    // Would this ever happen?
    if (rect.width == 0 || rect.height == 0) {
        return;
    }

    std::shared_ptr<TextureUpload> upload;
    bool preexisting = false;
    {
        std::vector<uint16_t> vec;
        unsigned x = rect.x,
                y = rect.y,
                w = rect.width,
                h = rect.height;
        for (int j = y; j < y + h; j++) {
            for (int i = x; i < x + w; i++) {
                vec.push_back(vram[j * FB_WIDTH + (i & (FB_WIDTH - 1))]);
            }
        }
        uint32_t hash = crc32(0, (unsigned char*)vec.data(), rect.width * rect.height * sizeof(uint16_t));
        // TODO: check for hash collision, by checking if existing upload has different dimensions. not sure how to recover if it does,
        //       but the odds of a collision are probably much higher than the odds that both textures would be in play simultaneously,
        //       so it'd probably be safe to simply ignore the newest upload and clear instead.
        upload = find_upload(hash);
        if (upload == nullptr) {
            upload = std::make_shared<TextureUpload>();
            upload->image = std::move(vec);
            upload->width = rect.width;
            upload->height = rect.height;
            upload->hash = hash;
            upload->dumpable = true;
            // Don't dump uploads specified by dump.cfg
            for (RectMatch rm : dump_ignore) {
                if (rm.matches(rect)) {
                    upload->dumpable = false;
                    break;
                }
            }
        } else {
            preexisting = true;
        }
    }
    
    if (blit_log != nullptr) {
        blit_log->upload(rect, upload->hash);
    }
    if (dump_log != nullptr) {
        dump_log->upload(frame, rect, upload->hash);
    }

    RestorableRect *restore = nullptr;
    for (RestorableRect &other : restorable_rects) {
        if (other.hash == upload->hash && other.rect == rect) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "RESTORATION: %x\n", other.hash);
            restore = &other;
            break;
        }
    }
    if (restore != nullptr) {
        for (TextureRect &t : restore->to_restore) {
            tracker.place(t); // TODO: clip to other.rect
        }
    } else {
        tracker.upload(toSRect(rect), upload);
    }
    fused_pages.mark_dirty(rect);
    fused_pages.rebuild_dirty(tracker, uploader);

    if (!preexisting) {
        load_hd_texture(upload->hash);
    }
}

void TextureTracker::load_hd_texture(uint32_t hash) {
    auto it_low = known_files.lower_bound({ hash, 0 });
    auto it_high = known_files.upper_bound({ hash, 0xFFFFFFFF });
    if (it_low != it_high) {
        slock_lock(iothread.channel->lock);
        for (auto it = it_low; it != it_high; it++) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "requesting texture: %x-%x\n", hash, it->palette_hash);
            std::unique_ptr<LoadRequest> load = std::unique_ptr<LoadRequest>(new LoadRequest);
            load->hash = hash;
            load->palette_hash = it->palette_hash;
            iothread.channel->requests.push_back(std::move(load));
        }
        slock_unlock(iothread.channel->lock);
        scond_signal(iothread.channel->cond);
    }
}

void output_rect_json(std::ostream &stream, Rect &rect) {
    stream << "{ "
           << "\"x\": " << rect.x << ","
           << "\"y\": " << rect.y << ","
           << "\"width\": " << rect.width << ","
           << "\"height\": " << rect.height
           << "}\n";
}

void TextureTracker::dump_texture(std::shared_ptr<TextureUpload> &upload, UsedMode &mode, DumpedMode dump_mode) {
    if (!upload->dumpable) {
        return;
    }

    auto it = std::find(upload->dumped_modes.begin(), upload->dumped_modes.end(), dump_mode);
    if (it == upload->dumped_modes.end()) {
        upload->dumped_modes.push_back(dump_mode);
        if (dump_enabled) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Dumping %x\n", upload->hash);
            dump_image(*upload, mode);
        }
    }
}

std::pair<HdTextureHandle, bool> HandleLRUCache::get(Rect rect, uint32_t palette_hash) {
    for (int i = 0; i < entries.size(); i++) {
        CacheEntry &entry = entries[i];
        if (entry.handle.palette_hash == palette_hash && entry.rect.contains(rect)) {
            CacheEntry hit = entry;
            for (int j = i; j > 0; j--) {
                entries[j] = entries[j - 1];
            }
            entries[0] = hit;
            dbg_hits += 1;
            return { hit.handle, true };
        }
    }
    dbg_misses += 1;
    return { HdTextureHandle::make_none(), false };
}
void HandleLRUCache::insert(Rect rect, uint32_t palette_hash, HdTextureHandle handle) {
    if (entries.size() >= max_size) {
        entries.pop_back();
    }
    entries.insert(entries.begin(), { rect, handle });
}
void HandleLRUCache::clear() {
    entries.clear();
}

HdTextureHandle TextureTracker::get_hd_texture_index(Rect rect, UsedMode &mode, unsigned int page_x, unsigned int page_y, bool &fastpath_capable_out, bool &cache_hit) {
    fastpath_capable_out = false;
    Rect palette_rect(mode.palette_offset_x, mode.palette_offset_y, mode.mode == TextureMode::Palette8bpp ? 256 : 16, 1);

    // TODO: I'm pretty sure this doesn't handle TextureMode::ABGR1555

    uint32_t palette_hash = 0;
    cache_hit = false;
    if (hd_textures_enabled || dump_enabled) {
        if (mode.mode == TextureMode::Palette8bpp || mode.mode == TextureMode::Palette4bpp) {
            palette_hash = get_palette_hash(palette_rect);
        }
    }
    if (hd_textures_enabled) {
        // Check if the same texture as last time is used.
        auto cache_result = handle_cache.get(rect, palette_hash);
        cache_hit = cache_result.second;
        if (cache_hit) {
            // cache_result.first is currently always a non-fused, non-none, index + palette_hash
            // in the future it may be useful to cache none, but there's currently no way to check if such a containing rect is still alive (since HdTextureHandle's index would be -1)
            EnduringTextureRect &tex = tracker.textures[cache_result.first.index]; // Forgive me
            if (tex.alive) {
                fastpath_capable_out = fastpath_enabled && (tex.texture_rect.upload->textures[palette_hash].alpha_flags & ALPHA_FLAG_TRANSPARENT) == 0;
                return cache_result.first;
            }
        }
    }

    static std::unordered_set<RectIndex> overlap;
    tracker.overlapping(rect, overlap);

    // Dump texture
    for (RectIndex index : overlap) {
        TextureRect *tex = tracker.get_index(index);
        dump_texture(tex->upload, mode, { mode.mode, palette_hash });
    }
    if (frame_dump != nullptr) {
        if (frame_dump_need_comma) {
            *frame_dump << ",";
        } else {
            frame_dump_need_comma = true;
        }
        *frame_dump << " { \"rect\": ";
        output_rect_json(*frame_dump, rect);
        *frame_dump << ", \"mode\": { \"mode\": " << int(mode.mode)
            << ", \"palette_x\": " << mode.palette_offset_x
            << ", \"palette_y\": " << mode.palette_offset_y
            << "} }\n";
    }

    if (!hd_textures_enabled) {
        fastpath_capable_out = false;
        return HdTextureHandle::make_none();
    }

    HdTextureHandle result = HdTextureHandle::make_none();

    Rect result_rect;
    for (RectIndex index : overlap) {
        TextureRect *tex = tracker.get_index(index);
        auto overlapped_image = tex->upload->textures.find(palette_hash);
        if (overlapped_image != tex->upload->textures.end()) {
            if (result == HdTextureHandle::make_none()) {
                // note that if tex->vram_rect contains rect, then it will be the only entry in overlap, so an early out would be pointless
                result_rect = fromSRect(tex->vram_rect);
                fastpath_capable_out = fastpath_enabled && fromSRect(tex->vram_rect).contains(rect) && (overlapped_image->second.alpha_flags & ALPHA_FLAG_TRANSPARENT) == 0;
                result = HdTextureHandle::make(index, palette_hash);
            } else {
                // Multiple overlap, must fuse
                unsigned int width
                    = mode.mode == TextureMode::Palette4bpp ? 64
                    : mode.mode == TextureMode::Palette8bpp ? 128
                    : 256;
                Rect page_rect = { page_x, page_y, width, 256 };
                fastpath_capable_out = false;
                return fused_pages.get_or_make(page_rect, palette_hash, tracker, uploader);
            }
        }
    }

    if (result != HdTextureHandle::make_none()) {
        handle_cache.insert(result_rect, palette_hash, result);
    }
    return result;
}

HdTexture TextureTracker::get_hd_texture(HdTextureHandle handle) {
    if (!handle.fused) {
        // HdTextureHandle's are perhaps too tricky.  They assume that the RectTracker's textures vector hasn't removed anything since the handle was
        // created. So it would seem all you need to do is, in Renderer::reset_queue, call RectTracker::releaseDeadHandles. Except you have to be
        // very very careful that no handles outside of the queues (ie. local) exist across a call to reset_queue.  That is, the handle must go into
        // the queue as soon as possible, otherwise that hd texture might not work (previously it would segfault).
        TextureRect *tex = tracker.get_index(handle.index);
        if (tex == nullptr) {
            if (handle.index != -1) {
                TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
            }
            return {
                {0, 0, 1, 1},
                {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
                default_hd_texture
            };
        }
        TextureUpload &upload = *tex->upload;
        // Use find rather than index, because if a stale HdTextureHandle was provided this could segfault
        // because indexing on a key that isn't present would initialize a new one with a null pointer
        auto iter = upload.textures.find(handle.palette_hash);
        if (iter == upload.textures.end()) {
            TT_LOG(RETRO_LOG_WARN, "stale HdTextureHandle: %d, %x\n", handle.index, handle.palette_hash);
            return {
                {0, 0, 1, 1},
                {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
                default_hd_texture
            };
        }
        Vulkan::ImageHandle &image = iter->second.image;
        int scaleX = image->get_width() / upload.width;
        int scaleY = image->get_height() / upload.height;
        SRect texture_subrect = tex->texture_subrect();
        return {
            tex->vram_rect,
            {
                texture_subrect.x * scaleX,
                texture_subrect.y * scaleY,
                texture_subrect.width * scaleX,
                texture_subrect.height * scaleY
            },
            image
        };
    } else {
        return fused_pages.get_from_handle(handle, default_hd_texture);
    }
}

bool is_power_of_two(int n) {
    // https://stackoverflow.com/questions/108318/whats-the-simplest-way-to-test-whether-a-number-is-a-power-of-2-in-c
    return n != 0 && (n & (n - 1)) == 0;
}

// TEMPORARY:
void TextureTracker::on_queues_reset() {
    handle_cache.clear();
    tracker.releaseDeadHandles(); // This is called from reset_queue, so as of now no HdTextureHandle's exist

    // Poll HD uploads

    slock_lock(iothread.channel->lock);
    std::vector<IOResponse> responses = std::move(iothread.channel->responses); // Take the responses
    iothread.channel->responses.clear();
    slock_unlock(iothread.channel->lock);

    for (IOResponse &response : responses) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "received texture: %x\n", response.hash);
        auto upload = find_upload(response.hash);

        if (upload != nullptr && upload->textures.find(response.palette_hash) == upload->textures.end()) {
            // warn if the hd texture width/height aren't power of 2 multiples of the original vram
            int width = response.levels[0].width;
            int height = response.levels[0].height;

            if (width  % upload->width  == 0 && is_power_of_two(width  / upload->width) &&
                height % upload->height == 0 && is_power_of_two(height / upload->height))
            {
                TT_LOG_VERBOSE(RETRO_LOG_INFO, "Found active texture found for %x (0x%x)\n",
                    response.hash, response.alpha_flags
                );

                Vulkan::ImageHandle texture = uploader->upload_texture(response.levels);

                upload->textures[response.palette_hash] = { texture, response.alpha_flags };

                for (EnduringTextureRect &e : tracker.textures) {
                    if (e.alive && e.texture_rect.upload == upload) {
                        fused_pages.mark_dirty(fromSRect(e.texture_rect.vram_rect));
                    }
                }
            } else {
                TT_LOG(RETRO_LOG_WARN, "Dimension mismatch for %x-%x, original=%dx%d, replacement=%dx%d\n",
                    response.hash,
                    response.palette_hash,
                    upload->width,
                    upload->height,
                    width,
                    height
                );
            }
        }
    }
    fused_pages.rebuild_dirty(tracker, uploader);
    fused_pages.remove_dead();
}
std::shared_ptr<TextureUpload> TextureTracker::find_upload(uint32_t hash) {
    std::shared_ptr<TextureUpload> upload = tracker.find_upload(hash);

    if (upload != nullptr) {
        return upload;
    }

    // backup search, in case it's restorable but currently missing from the rect tracker
    for (RestorableRect &entry : restorable_rects) {
        for (TextureRect &t : entry.to_restore) {
            if (hash == t.upload->hash) {
                return t.upload;
            }
        }
    }

    return nullptr;
}

void TextureTracker::endFrame() {
    frame += 1;

    if (frame % 300 == 0) {
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "hit ratio: %f (%ld, %ld)\n", double(handle_cache.dbg_hits) / (handle_cache.dbg_hits + handle_cache.dbg_misses), handle_cache.dbg_hits, handle_cache.dbg_misses);
        handle_cache.dbg_hits = 0;
        handle_cache.dbg_misses = 0;
    }

    if (blit_log != nullptr) {
        blit_log->set_frame(frame);
    }

    if (frame_dump != nullptr) {
        *frame_dump << "]}\n";
        delete frame_dump;
        frame_dump = nullptr;
    }

    if (dbg_input_state_cb != 0) {
        if (frame_dump_key.query()) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Left bracket!\n");
            frame_dump = new std::ofstream(dump_path() + "test_dump.json");
            frame_dump_need_comma = false;
            *frame_dump << "{ \"initial\": [\n";
            bool need_comma = false;
            for (EnduringTextureRect &etexture : tracker.textures) {
                if (!etexture.alive) continue;
                TextureRect &texture = etexture.texture_rect;
                if (need_comma) {
                    *frame_dump << ",";
                } else {
                    need_comma = true;
                }
                *frame_dump << " { \"rect\": ";
                Rect rect = fromSRect(texture.vram_rect);
                output_rect_json(*frame_dump, rect);
                *frame_dump << ", \"hash\": \"" << std::hex << texture.upload->hash << "\" }\n" << std::dec;
            }
            *frame_dump << "], \"events\": [\n";
        }

        if (hd_toggle_key.query()) {
            hd_textures_enabled = !hd_textures_enabled;
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling hd textures: %s\n", hd_textures_enabled ? "on" : "off");
        }

        if (reload_key.query()) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Reloading hd textures from disk\n");
            reload_textures_from_disk();
        }

        if (fastpath_key.query()) {
            fastpath_enabled = !fastpath_enabled;
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Toggling fastpath %s\n", fastpath_enabled ? "ON" : "OFF");
        }
    }
}

void TextureTracker::reload_textures_from_disk() {
    // Reload the directory listing
    known_files = read_texture_directory(replacements_path().c_str());
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Found %d hd textures\n", known_files.size());

    // Delete existing textures
    std::set<uint32_t> hashes;
    for (EnduringTextureRect &texture : tracker.textures) {
        texture.texture_rect.upload->textures.clear();
        hashes.insert(texture.texture_rect.upload->hash);
    }
    for (RestorableRect &restorable : restorable_rects) {
        for (TextureRect &tr : restorable.to_restore) {
            tr.upload->textures.clear();
            hashes.insert(tr.upload->hash);
        }
    }

    // Delete fused textures
    fused_pages.mark_dead({0, 0, FB_WIDTH, FB_HEIGHT});

    // Issue requests to the iothread to load the hd textures
    for (uint32_t hash : hashes) {
        load_hd_texture(hash);
    }
}

// DumpLog
std::ostream& operator<<(std::ostream &o, const Rect &rect) {
    o << std::dec << rect.x << "," << rect.y << " " << rect.width << "x" << rect.height;
    return o;
}
std::ostream& operator<<(std::ostream &o, const TextureMode &mode) {
    switch(mode) {
        case TextureMode::Palette4bpp:
            o << "Palette4bpp";
            break;
        case TextureMode::Palette8bpp:
            o << "Palette8bpp";
            break;
        case TextureMode::ABGR1555:
            o << "ABGR1555";
            break;
        case TextureMode::None:
            o << "None";
            break;
    }
    return o;
}

DumpLog::DumpLog() {
    dump_stream = std::ofstream(dump_path() + "dump.log");
}
void DumpLog::upload(uint64_t frame, Rect rect, uint32_t hash) {
    dump_stream << "[" << std::dec << frame << "] Upload: " << std::hex << hash << " @ " << rect <<  "\n";
}
void DumpLog::dump(uint64_t frame, uint32_t hash, uint32_t palette_hash, TextureMode mode) {
    dump_stream << "[" << std::dec << frame << "] Dumped: " << std::hex << hash << " with palette " << std::hex << palette_hash << " in mode " << mode << "\n";
}

// BlitLog

/*
    type Rect = { x: number, y: number, width: number, height: number };
    type BlitEvent
        = { type: "upload", rect: Rect, hash: number, frame: number }
        | { type: "blit", dst: Rect, src: Rect, frame: number }
        | { type: "clear", rect: Rect, frame: number }
        ;
    BlitEvent[]
*/

BlitLog::BlitLog() {
    dump_stream = std::ofstream(dump_path() + "blit_log.json");
    dump_stream << "[\n";
}
BlitLog::~BlitLog() {
    dump_stream << "]";
}

void BlitLog::set_frame(uint32_t frame) {
    this->frame = frame;
}

void BlitLog::comma() {
    if (need_comma) {
        dump_stream << ",";
    } else {
        need_comma = true;
    }
}

void BlitLog::upload(Rect rect, uint32_t hash) {
    comma();
    dump_stream << " { \"type\": \"upload\", \"rect\": ";
    output_rect_json(dump_stream, rect);
    dump_stream << ", \"hash\": " << hash;
    dump_stream << ", \"frame\": " << frame << " }\n";
}

void BlitLog::blit(Rect dst, Rect src) {
    comma();
    dump_stream << " { \"type\": \"blit\", \"dst\": ";
    output_rect_json(dump_stream, dst);
    dump_stream << ", \"src\": ";
    output_rect_json(dump_stream, src);
    dump_stream << ", \"frame\": " << frame << " }\n";
}

void BlitLog::clear(Rect rect) {
    comma();
    dump_stream << " { \"type\": \"clear\", \"rect\": ";
    output_rect_json(dump_stream, rect);
    dump_stream << ", \"frame\": " << frame << " }\n";
}

// RectTracker

bool intersects(SRect a, SRect b) {
    return !(
        a.left() >= b.right() ||
        b.left() >= a.right() ||
        a.top() >= b.bottom() ||
        b.top() >= a.bottom()
    );
}

SRect bounds(int left, int right, int top, int bottom) {
    return SRect(left, top, right - left, bottom - top);
}

void split(SRect original, SRect remove, std::vector<SRect> &results) {
    auto intersectionResult = intersect(original, remove);
    if (!intersectionResult.second) {
        results.push_back(original);
        return;
    }

    SRect intersection = intersectionResult.first;

    // Top rect
    if (intersection.top() > original.top()) {
        results.push_back(bounds(
            original.left(),
            original.right(),
            original.top(),
            intersection.top()
        ));
    }

    // Bottom rect
    if (intersection.bottom() < original.bottom()) {
        results.push_back(bounds(
            original.left(),
            original.right(),
            intersection.bottom(),
            original.bottom()
        ));
    }

    // Left rect
    if (intersection.left() > original.left()) {
        results.push_back(bounds(
            original.left(),
            intersection.left(),
            intersection.top(),
            intersection.bottom()
        ));
    }

    // Right rect
    if (intersection.right() < original.right()) {
        results.push_back(bounds(
            intersection.right(),
            original.right(),
            intersection.top(),
            intersection.bottom()
        ));
    }
}

void RectTracker::upload(SRect rect, std::shared_ptr<TextureUpload> upload) {
    TextureRect texture(upload, 0, 0, rect);
    place(texture);
    lookup_grid_dirty = true;
}

SRect moved(SRect rect, int dx, int dy) {
    return SRect(rect.x + dx, rect.y + dy, rect.width, rect.height);
}

void RectTracker::blit(SRect dst, SRect src) {
    std::vector<TextureRect> to_place;
    int moveX = dst.x - src.x;
    int moveY = dst.y - src.y;
    for (EnduringTextureRect &eold : textures) {
        if (eold.alive) {
            TextureRect &old = eold.texture_rect;
            auto intersection = intersect(old.vram_rect, src);
            if (intersection.second) {
                auto sub = subTexture(old, intersection.first);
                auto subMoved = TextureRect(sub.upload, sub.offset_x, sub.offset_y, moved(sub.vram_rect, moveX, moveY));
                to_place.push_back(subMoved);
            }
        }
    }
    clear_rect(dst);
    for (TextureRect &t : to_place) {
        place(t);
    }
    lookup_grid_dirty = true;
}
void RectTracker::clear(SRect rect) {
    clear_rect(rect);
    lookup_grid_dirty = true;
}
void RectTracker::releaseDeadHandles() {
    auto retainedIt = textures.begin();
    for (auto it = textures.begin(); it != textures.end(); it++) {
        if (it->alive) {
            *retainedIt = *it;
            retainedIt++;
        }
    }
    textures.erase(retainedIt, textures.end());

    lookup_grid_dirty = true;
}

std::unordered_set<RectIndex>& RectTracker::overlapping(Rect uvrect, std::unordered_set<RectIndex> &results) {
    if (lookup_grid_dirty) {
        rebuild_lookup_grid();
    }

    // TODO: remove this when renderer/build_attribs doesn't have an unnecessary - 1
    if (uvrect.width == 0) {
        uvrect.width = 1;
    }

    SRect rect = toSRect(uvrect);

    results.clear();
    lookup_grid.get(rect, results);
    return results;
}

TextureRect* RectTracker::get_index(RectIndex index) {
    if (index < 0 || index >= textures.size()) {
        return nullptr;
    }
    return &textures[index].texture_rect;
}

void RectTracker::clear_rect(SRect &rect) {
    std::vector<SRect> splits;
    splits.reserve(4);

    std::vector<TextureRect> newTextures;
    for (EnduringTextureRect &eold : textures) {
        if (eold.alive) {
            TextureRect &old = eold.texture_rect;

            splits.clear();
            split(old.vram_rect, rect, splits);
            if (splits.size() == 1 && splits[0] == old.vram_rect) {
                // The rect didn't split, do nothing
            } else {
                // The rect split, mark this texture as dead and push its splits to be added
                eold.alive = false;
                for (SRect &vr : splits) {
                    newTextures.push_back(subTexture(old, vr));
                }
            }
        }
    }
    for (TextureRect newTexture : newTextures) {
        textures.push_back({ newTexture, true });
    }
}
void RectTracker::place(TextureRect texture) {
    clear_rect(texture.vram_rect);
    textures.push_back({ texture, true });
}

void RectTracker::rebuild_lookup_grid() {
    lookup_grid.clear();
    for (int i = 0; i < textures.size(); i++) {
        if (textures[i].alive) {
            lookup_grid.insert(textures[i].texture_rect.vram_rect, i);
        }
    }
    lookup_grid_dirty = false;
}

std::shared_ptr<TextureUpload> RectTracker::find_upload(uint32_t hash) {
    for (EnduringTextureRect &eold : textures) {
        if (eold.texture_rect.upload->hash == hash) {
            return eold.texture_rect.upload;
        }
    }
    return nullptr;
}

int clamp(int x, int low, int high) {
    return MIN(MAX(x, low), high);
}

struct CellBounds {
    int lowX;
    int highX; // exclusive
    int lowY;
    int highY; // exclusive
};

CellBounds cellBounds(SRect vram) {
    return CellBounds({
        clamp(vram.left() / LOOKUP_CELL_WIDTH, 0, LOOKUP_GRID_COLUMNS),
        clamp(ceil(vram.right() / float(LOOKUP_CELL_WIDTH)), 0, LOOKUP_GRID_COLUMNS),
        clamp(vram.top() / LOOKUP_CELL_HEIGHT, 0, LOOKUP_GRID_ROWS),
        clamp(ceil(vram.bottom() / float(LOOKUP_CELL_HEIGHT)), 0, LOOKUP_GRID_ROWS)
    });
}

void LookupGrid::insert(SRect r, RectIndex index) {
    CellBounds c = cellBounds(r);
    for (int x = c.lowX; x < c.highX; x++) {
        for (int y = c.lowY; y < c.highY; y++) {
            cells[y * LOOKUP_GRID_COLUMNS + x].push_back({r, index});
        }
    }
}
void LookupGrid::get(SRect r, std::unordered_set<RectIndex> &results) {
    CellBounds c = cellBounds(r);
    for (int x = c.lowX; x < c.highX; x++) {
        for (int y = c.lowY; y < c.highY; y++) {
            for (LookupEntry &entry : cells[y * LOOKUP_GRID_COLUMNS + x]) {
                if (intersects(entry.rect, r)) {
                    results.insert(entry.index);
                }
            }
        }
    }
}
void LookupGrid::clear() {
    for (int i = 0; i < LOOKUP_GRID_COLUMNS * LOOKUP_GRID_ROWS; i++) {
        cells[i].clear();
    }
}

// FusedPages

int64_t page_bytes(FusionRects &fusion) {
    return fusion.scaleX * fusion.scaleY * fusion.vram_rect.width * fusion.vram_rect.height * 4;
}

void FusedPages::dbg_print_info() {
    int64_t num_bytes = 0;
    for (FusedPage &page : pages) {
        num_bytes += page_bytes(page.fusion);
    }
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Fused Pages: %lu, Bytes: %ld (%.1f MiB)\n", pages.size(), num_bytes, num_bytes / 1048576.0);
}

bool srect_gt(const SRect &a, const SRect &b) {
    if (a.x != b.x)
        return a.x > b.x;
    if (a.y != b.y)
        return a.y > b.y;
    if (a.width != b.width)
        return a.width > b.width;
    return a.height > b.height;
}

FusionRects fusion_rects(Rect full_page_rect, uint32_t palette_hash, RectTracker &tracker) {
    FusionRects f;
    f.scaleX = 0;
    f.scaleY = 0;
    f.vram_rect = {0, 0, 0, 0};

    for (EnduringTextureRect &e : tracker.textures) {
        if (!e.alive) {
            continue;
        }
        auto intersection = intersect(toSRect(full_page_rect), e.texture_rect.vram_rect);
        if (intersection.second) {
            TextureUpload &upload = *e.texture_rect.upload;
            auto hd_texture = upload.textures.find(palette_hash);
            if (hd_texture != upload.textures.end()) {
                // Clip to the destination texture (important, otherwise it might blit out of bounds which may have wrought havoc upon my sanity)
                TextureRect clipped = subTexture(e.texture_rect, intersection.first);
                f.scaleX = std::max(f.scaleX, hd_texture->second.image->get_width() / upload.width);
                f.scaleY = std::max(f.scaleY, hd_texture->second.image->get_height() / upload.height);
                Rect r = fromSRect(clipped.vram_rect);
                if (f.vram_rect.width == 0) {
                    f.vram_rect = r;
                } else {
                    f.vram_rect.extend_bounding_box(r);
                }
                f.rects.push_back(clipped);
            }
        }
    }

    // Sort rects so that the vector itself can be compared
    std::sort(f.rects.begin(), f.rects.end(), [](const TextureRect &a, const TextureRect &b) {
        // Compare .upload by internal pointer
        if (a.upload.get() != b.upload.get())
            return a.upload.get() > b.upload.get();
		if (a.vram_rect != b.vram_rect)
			return srect_gt(a.vram_rect, b.vram_rect);
        return srect_gt(a.texture_subrect(), b.texture_subrect());
	});

    return f;
}

void rebuild_page(FusedPage &page, RectTracker &tracker, TextureUploader *uploader) {
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilding page for %x, %d,%d %dx%d\n",
        page.palette,
        page.fusion.vram_rect.x,
        page.fusion.vram_rect.y,
        page.fusion.vram_rect.width,
        page.fusion.vram_rect.height
    );

    page.dirty = false;

    {
        FusionRects fusion = fusion_rects(page.full_page_rect, page.palette, tracker);
        if (page.fusion == fusion) {
            TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: no change\n");
            return;
        }
        page.fusion = std::move(fusion);
    }

    if (page.fusion.rects.size() == 0) {
        page.dead = true;
        page.texture.reset();
        TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page is now dead\n");
        return;
    }

    // assert(page.scaleX > 0 && page.scaleX <= 64);
    // assert(page.scaleY > 0 && page.scaleY <= 64);

    Vulkan::CommandBufferHandle &cmd = uploader->command_buffer_hack_fixme();

    int texture_width = page.fusion.vram_rect.width * page.fusion.scaleX;
    int texture_height = page.fusion.vram_rect.height * page.fusion.scaleY;
    // assert(texture_width > 0 && texture_width < 10000);
    // assert(texture_height > 0 && texture_height < 10000);

    // TODO: I don't know SHIT about barriers.
    
    // special sentinel value
    // Note that due to the way textures are put into a page, these special values will not bleed into neighbors in the mipmaps,
    // because the mipmaps are only used down to the original resolution, and hd textures are aligned to that original resolution's
    // texels.
    VkClearValue fallthrough = {0.0, 0.0, 0.0, 1.0};

    int mip_levels = log2(std::min(page.fusion.scaleX, page.fusion.scaleY)) + 1;

    if (page.texture && page.texture->get_width() == texture_width && page.texture->get_height() == texture_height) {
        // Switch back into transfer dst layout
        cmd->image_barrier(*page.texture,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        cmd->clear_image(*page.texture, fallthrough);
    } else {
        page.texture = uploader->create_texture(texture_width, texture_height, mip_levels);
        cmd->clear_image(*page.texture, fallthrough);
    }

    // Second pass to blit all the existing textures into the new texture
    for (TextureRect &tex : page.fusion.rects) {
        TextureUpload &upload = *tex.upload;

        auto hd_texture = upload.textures.find(page.palette);
        if (hd_texture == upload.textures.end()) {
            // That's odd
            continue;
        }

        Vulkan::ImageHandle &image = hd_texture->second.image;

        int srcWidth = image->get_width();
        int srcHeight = image->get_height();

        int sx = srcWidth / upload.width;
        int sy = srcHeight / upload.height;

        int rx = page.fusion.scaleX / sx;
        int ry = page.fusion.scaleY / sy;

        SRect subrect = tex.texture_subrect();

        VkOffset3D dst_offset = {
            (tex.vram_rect.x - int(page.fusion.vram_rect.x)) * int(page.fusion.scaleX),
            (tex.vram_rect.y - int(page.fusion.vram_rect.y)) * int(page.fusion.scaleY),
            0
        };
        VkOffset3D dst_extent = {
            tex.vram_rect.width * int(page.fusion.scaleX),
            tex.vram_rect.height * int(page.fusion.scaleY),
            1
        };

        // Switch into transfer src
        // what the fuck am I doing?
        cmd->image_barrier(
            *image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0
        );

        // Blit into every mipmap level down to base vram
        // TODO: this isn't a great way to do this, will probably be blurrier than it could be if src and dst aspect ratios are different
        // TODO: is this line even right? it sure doesn't look right
        int full_res_levels = log2(std::max(rx, ry)) + 1;
        // assert(max_level >= 0 && max_level <= 6);
        // TODO: this is incredibly finicky, and one bad (out of bounds) blit can bork everything
        for (int dstLevel = 0; dstLevel < mip_levels; dstLevel++) {
            int srcLevel = std::max(0, dstLevel - full_res_levels);

            cmd->blit_image(*page.texture, *image,
                dst_offset,
                dst_extent,
                {
                    (sx * subrect.x) >> srcLevel,
                    (sy * subrect.y) >> srcLevel,
                    0
                },
                { 
                    (sx * subrect.width) >> srcLevel,
                    (sy * subrect.height) >> srcLevel,
                    1
                },
                dstLevel,
                srcLevel
            );
            
            dst_offset.x >>= 1;
            dst_offset.y >>= 1;
            dst_extent.x = std::max(dst_extent.x >> 1, 1);
            dst_extent.y = std::max(dst_extent.y >> 1, 1);
        }

        // Change back to shader read
        cmd->image_barrier(
            *image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0
        );
    }

    // I have no idea what the fuck I'm doing
    // Make the fused texture readable by shaders
    cmd->image_barrier(*page.texture,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);

    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Rebuilt page: page now %ux%u, %ld bytes (%.1f MiB)\n",
        page.fusion.vram_rect.width * page.fusion.scaleX, page.fusion.vram_rect.height * page.fusion.scaleY,
        page_bytes(page.fusion), page_bytes(page.fusion) / 1048576.0
    );
}

HdTexture FusedPages::get_from_handle(HdTextureHandle handle, Vulkan::ImageHandle &default_hd_texture) {
    if (handle.index < 0 || handle.index >= pages.size()) {
        TT_LOG(RETRO_LOG_WARN, "BAD fused index!\n");
        return {
            {0, 0, 1, 1},
            {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
            default_hd_texture
        };
    }
    FusedPage &page = pages[handle.index];
    if (!page.texture) {
        TT_LOG(RETRO_LOG_WARN, "Missing fused texture!\n");
        return {
            {0, 0, 1, 1},
            {0, 0, int(default_hd_texture->get_width()), int(default_hd_texture->get_height())},
            default_hd_texture
        };
    }
    return {
        toSRect(page.fusion.vram_rect),
        { 0, 0, int(page.texture->get_width()), int(page.texture->get_height()) },
        page.texture
    };
}

HdTextureHandle FusedPages::get_or_make(Rect page_rect, uint32_t palette, RectTracker &tracker, TextureUploader *uploader) {
    for (int x = 0; x < pages.size(); x++) {
        FusedPage &page = pages[x];
        if (!page.dead && page.palette == palette && page.full_page_rect == page_rect) {
            // return page
            return HdTextureHandle::make_fused(x);
        }
    }

    // Make a new fused page
    TT_LOG_VERBOSE(RETRO_LOG_INFO, "Creating new fused page for palette %x\n", palette);
    FusedPage page;
    page.dead = false;
    page.dirty = false;
    page.full_page_rect = page_rect;
    page.palette = palette;
    rebuild_page(page, tracker, uploader);
    pages.push_back(page);
    return HdTextureHandle::make_fused(pages.size() - 1);
}
void FusedPages::mark_dirty(Rect rect) {
    for (FusedPage &page : pages) {
        if (!page.dead && page.full_page_rect.intersects(rect)) {
            page.dirty = true;
        }
    }
}
void FusedPages::mark_dead(Rect rect) {
    for (FusedPage &page : pages) {
        if (!page.dead && page.full_page_rect.intersects(rect)) {
            page.dead = true;
        }
    }
}
void FusedPages::rebuild_dirty(RectTracker &tracker, TextureUploader *uploader) {
    bool changed = false;
    for (FusedPage &page : pages) {
        if (!page.dead && page.dirty) {
            rebuild_page(page, tracker, uploader);
            changed = true;
        }
    }
    if (changed) {
        dbg_print_info();
    }
}
void FusedPages::remove_dead() {
    auto retainedIt = pages.begin();
    for (auto it = pages.begin(); it != pages.end(); it++) {
        if (!it->dead) {
            *retainedIt = *it;
            retainedIt++;
        }
    }
    pages.erase(retainedIt, pages.end());
}


//========================================
// Save State

TextureUpload copy_upload_without_handles(const TextureUpload &to_copy) {
    TextureUpload copy = to_copy;
    copy.textures.clear();
    return copy;
}

TextureRectSaveState to_save_state(const TextureRect &t, std::map<uint32_t, TextureUpload> &uploads) {
    uint32_t hash = t.upload->hash;
    if (uploads.find(hash) == uploads.end()) {
        uploads[hash] = copy_upload_without_handles(*t.upload);
    }
    return {
        t.upload->hash,
        t.offset_x,
        t.offset_y,
        t.vram_rect
    };
}
TextureRect from_save_state(const TextureRectSaveState &t, std::map<uint32_t, std::shared_ptr<TextureUpload>> &uploads) {
    auto it = uploads.find(t.upload_hash);
    if (it == uploads.end()) {
        TT_LOG(RETRO_LOG_ERROR, "SaveState upload missing!\n");
    }
    return {
        it->second,
        t.offset_x,
        t.offset_y,
        t.vram_rect
    };
}

TextureTrackerSaveState TextureTracker::save_state() {
    TextureTrackerSaveState state;

    for (EnduringTextureRect &r : tracker.textures) {
        if (r.alive) {
            state.rects.push_back(to_save_state(r.texture_rect, state.uploads));
        }
    }
    for (RestorableRect &r : restorable_rects) {
        RestorableRectSaveState saved;
        saved.hash = r.hash;
        saved.rect = r.rect;
        for (TextureRect &t : r.to_restore) {
            saved.to_restore.push_back(to_save_state(t, state.uploads));
        }
        state.restorable.push_back(std::move(saved));
    }

    return state;
}


void TextureTracker::load_state(const TextureTrackerSaveState &state) {
    std::map<uint32_t, std::shared_ptr<TextureUpload>> uploads;
    for (auto it = state.uploads.begin(); it != state.uploads.end(); it++) {
        auto ptr = std::shared_ptr<TextureUpload>(new TextureUpload);
        *ptr = it->second;
        uploads[it->first] = std::move(ptr);
    }

    clearRegion({ 0, 0, FB_WIDTH, FB_HEIGHT });
    tracker.textures.clear(); // load_state should only be called right after creating this TextureTracker, so this ought to be empty already anyway
    for (const TextureRectSaveState &r : state.rects) {
        tracker.place(from_save_state(r, uploads));
    }
    restorable_rects.clear();
    for (const RestorableRectSaveState &r : state.restorable) {
        RestorableRect loaded;
        loaded.hash = r.hash;
        loaded.rect = r.rect;
        for (const TextureRectSaveState &t : r.to_restore) {
            loaded.to_restore.push_back(from_save_state(t, uploads));
        }
        restorable_rects.push_back(std::move(loaded));
    }
    // Need to reload the hd textures, too
    for (auto it = state.uploads.begin(); it != state.uploads.end(); it++) {
        load_hd_texture(it->first);
    }
}
// End of Save State
//========================================


bool DbgHotkey::query() {
    uint16_t state = dbg_input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, key);
    bool is_key_down = state != 0;
    bool just_pressed = is_key_down && !was_key_down;
    was_key_down = is_key_down;
    return just_pressed;
}

}
