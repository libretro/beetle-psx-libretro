#ifndef HDTEXTURES_H
#define HDTEXTURES_H

/*
	Three spaces, local, vram, and hd_texture
		local:
			vTexLimits
			vWindow
			vUV
		vram:
			vBaseUV (texture location)
			vParam.xy (palette location)
			uFramebuffer
			hd_texture_vram_rect
		hd_texture:
			hd_texture_texel_offset
		go between local and vram using the texture mode (vParam.z stores the amount to shift: 0, 1, or 2)
		go between vram and hd_texture using hd_texture_vram_rect, hd_texture_texel_offset, and hd_texture_scale
*/

struct LevelInfo {
	vec4 clamp; // vec4(vec2(vTexLimits.xy) * level.local_to_level, (vec2(vTexLimits.zw) + 1.0) * level.local_to_level - 1.0)
	vec4 level_texel_rect; // (left, top, right-exclusive, bottom-exclusive)
	vec2 local_to_level; // hd texels per local
	vec2 level_texel_offset; // (vBaseUV - push.hd_texture_vram_rect.xy) * (hd_scale() >> level) + level_texel_rect.xy
	int level;
};

void makeLevel(int l, out LevelInfo level) {
	level.level = l;
	vec2 vram_to_level = (push.hd_texture_texel_rect.zw / push.hd_texture_vram_rect.zw) >> level.level;
	level.local_to_level = vec2(1.0 / float(1 << (vParam.z & 3)), 1.0) * vram_to_level;
	level.clamp = vec4(vec2(vTexLimits.xy) * level.local_to_level, (vec2(vTexLimits.zw) + 1.0) * level.local_to_level - 1.0);
	vec4 texel_rect = push.hd_texture_texel_rect >> l;
	level.level_texel_rect = vec4(texel_rect.xy, texel_rect.xy + texel_rect.zw);
	level.level_texel_offset = (vBaseUV - push.hd_texture_vram_rect.xy) * vram_to_level + texel_rect.xy;
}

/*
ivec2 hd_scale() {
	return push.hd_texture_texel_rect.zw / push.hd_texture_vram_rect.zw; // This should be evenly divisible
}

// This returns the appropriate scale between spaces, but no translation (vBaseUV)
vec2 local_to_hd_scale(int level) {
	float local_pixels_per_vram_texel = float(1 << (vParam.z & 3));

	vec2 local_to_vram = vec2(1.0 / local_pixels_per_vram_texel, 1.0);
	vec2 vram_to_hd = vec2(hd_scale() >> level);

	vec2 local_to_hd = local_to_vram * vram_to_hd;
	return local_to_hd;
}

vec2 limit_hd_coord(vec2 hd_uv, int level) {
	vec2 local_to_hd = local_to_hd_scale(level);

	vec2 hd_clamped = clamp(hd_uv, vec2(vTexLimits.xy) * local_to_hd, (vec2(vTexLimits.zw) + 1.0) * local_to_hd - 1.0);
	vec2 local_clamped = hd_clamped / local_to_hd;
	vec2 local_limited = ((ivec2(local_clamped) & vWindow.xy) | vWindow.zw) + fract(local_clamped);
	return local_limited * local_to_hd;
}

bool sample_hd_atlas_nearest(vec2 hd_uv, int level, out vec4 color) {
	vec2 translated_hd = (vBaseUV - push.hd_texture_vram_rect.xy) * (hd_scale() >> level) + limit_hd_coord(hd_uv, level);
	ivec4 level_texel_rect = push.hd_texture_texel_rect >> level;
	if (any(lessThan(vec4(translated_hd.xy, level_texel_rect.zw), vec4(0.0, 0.0, translated_hd.xy)))) {
		return false;
	}
	
	color = texelFetch(uHighResTexture, ivec2(translated_hd) + level_texel_rect.xy, level);
	return true;
}
*/

vec2 limit_hd_coord(vec2 hd_uv, const in LevelInfo level) {
	vec2 hd_clamped = clamp(hd_uv, level.clamp.xy, level.clamp.zw);
	vec2 local_clamped = hd_clamped / level.local_to_level;
	vec2 local_limited = ((ivec2(local_clamped) & vWindow.xy) | vWindow.zw) + fract(local_clamped);
	return local_limited * level.local_to_level;
}
bool hd_texel_valid(ivec2 texel, const in LevelInfo level) {
	return !any(bvec4(lessThan(texel, level.level_texel_rect.xy), greaterThanEqual(texel, level.level_texel_rect.zw)));
}
ivec2 hd_texel(vec2 hd_uv, const in LevelInfo level) {
	return ivec2(level.level_texel_offset + limit_hd_coord(hd_uv, level));
}
vec4 sample_hd_texture_nearest(vec2 hd_uv, const in LevelInfo level, inout bool valid) {
	ivec2 texel = hd_texel(hd_uv, level);
	vec4 color = texelFetch(uHighResTexture, texel, level.level);
	valid = valid && hd_texel_valid(texel, level) && color != vec4(0.0, 0.0, 0.0, 1.0); // special sentinel value indicates fallthrough to sd
	return color;
}

vec3 hue_to_rgb(float h) {
    h = fract(h) * 6.0;
    float f = fract(h);
    if (h < 1.0) {
        return vec3(1.0, f, 0.0);
    } else if (h < 2.0) {
        return vec3(1.0 - f, 1.0, 0.0);
    } else if (h < 3.0) {
        return vec3(0.0, 1.0, f);
    } else if (h < 4.0) {
        return vec3(0.0, 1.0 - f, 1.0);
    } else if (h < 5.0) {
        return vec3(f, 0.0, 1.0);
    } else { // if (h < 6.0) {
        return vec3(1.0, 0.0, 1.0 - f);
    }
}

// modified from vram.h
vec4 sample_hd_texture_bilinear(vec2 uv /* hd */, const in LevelInfo level, inout bool valid) {
	float x = uv.x;
	float y = uv.y;

	// interpolate from centre of texel
	vec2 uv_frac = fract(vec2(x, y)) - vec2(0.5, 0.5);
	vec2 uv_offs = sign(uv_frac);
	uv_frac = abs(uv_frac);

	// sample 4 nearest texels
	vec4 texel_00 = sample_hd_texture_nearest(vec2(x + 0., y + 0.), level, valid);
	vec4 texel_10 = sample_hd_texture_nearest(vec2(x + uv_offs.x, y + 0.), level, valid);
	vec4 texel_01 = sample_hd_texture_nearest(vec2(x + 0., y + uv_offs.y), level, valid);
	vec4 texel_11 = sample_hd_texture_nearest(vec2(x + uv_offs.x, y + uv_offs.y), level, valid);

	// test for fully transparent texel
	// this ditches semi-transparency information, .w (or .a) is now a weight for true transparency
	texel_00.w = float(texel_00 != vec4(0.0));
	texel_10.w = float(texel_10 != vec4(0.0));
	texel_01.w = float(texel_01 != vec4(0.0));
	texel_11.w = float(texel_11 != vec4(0.0));
		
	// average samples
	return texel_00 * (1. - uv_frac.x) * (1. - uv_frac.y)
		+ texel_10 * uv_frac.x * (1. - uv_frac.y)
		+ texel_01 * (1. - uv_frac.x) * uv_frac.y
		+ texel_11 * uv_frac.x * uv_frac.y;
}

bool sample_hd_texture_nearest_hack(vec2 uv /* sd */, out vec4 color) {
	LevelInfo level;
	makeLevel(0, level);
	bool valid = true;
	color = sample_hd_texture_nearest(uv * level.local_to_level, level, valid);
	return valid;
}

vec4 sample_hd_fast(vec2 uv) {
	ivec2 vram_to_local = ivec2(1 << (vParam.z & 3), 1);

	vec2 lookup_uv = (uv / vec2(vram_to_local) + vBaseUV - push.hd_texture_vram_rect.xy) / vec2(push.hd_texture_vram_rect.zw);
	return texture(uHighResTexture, lookup_uv);
	/*
    float lod = textureQueryLod(uHighResTexture, lookup_uv).y;

	// Do not sample a mipmap level that is below native resolution.
	vec2 axis_lod = log2(push.hd_texture_texel_rect.zw / (push.hd_texture_vram_rect.zw * vram_to_local));
	int max_lod = max(0, int(min(axis_lod.x, axis_lod.y)));

	// fast path, use native trilinear filtering
	// TODO: this doesn't clamp for MSAA, because it doesn't know what level to clamp to? should clamp to min(max_lod, ceil(lod))?
	// TODO: this lack of clamping is also causing seams in backgrounds at 8x and higher
	return textureLod(uHighResTexture, lookup_uv, clamp(lod, 0, max_lod));
	*/
}

vec4 sample_hd_texture_trilinear(vec2 uv /* sd */, out bool valid) {
	ivec2 texture_size = textureSize(uHighResTexture, 0);
	ivec2 vram_to_local = ivec2(1 << (vParam.z & 3), 1);

    vec2 hd_uv = uv / vec2(vram_to_local) * (push.hd_texture_texel_rect.zw / push.hd_texture_vram_rect.zw);
    float lod = textureQueryLod(uHighResTexture, hd_uv / vec2(texture_size)).y;
    int lod_low = int(lod);
    int lod_high = lod_low + 1;
    float t = fract(lod);

	// Do not sample a mipmap level that is below native resolution.
	vec2 axis_lod = log2(push.hd_texture_texel_rect.zw / (push.hd_texture_vram_rect.zw * vram_to_local));
	int max_lod = max(0, int(min(axis_lod.x, axis_lod.y)));

    lod_low = clamp(lod_low, 0, max_lod);
    lod_high = clamp(lod_high, 0, max_lod);

	LevelInfo level;
	makeLevel(lod_high, level);
	vec2 uv_high = uv * level.local_to_level;
	vec2 uv_high_with_offset = level.level_texel_offset + uv_high;
	
	valid = true;
	vec4 color_high = sample_hd_texture_bilinear(uv * level.local_to_level, level, valid);

	vec4 color;
	if (lod_low != lod_high) {
		makeLevel(lod_low, level);
		vec4 color_low = sample_hd_texture_bilinear(uv * level.local_to_level, level, valid);
		color = mix(color_low, color_high, t);
	} else {
		color = color_high;
	}
	// This check is probably unnecessary, because if opacity is <= 1e-6 then the fragment will be discarded and color.rgb will be unused anyway
	if (color.a > 1e-6) {
		// Remove the influence of transparent samples on the color (transparent samples are vec4(0.0), so they will move the color towards black)
		color.rgb /= color.a;
	}
	return color;

    // DBG Visualize mipmap LOD level
    /*
    if (lod_low < 0.0) {
        color = vec4(hue_to_rgb(-lod_low / 6.0) * 0.5, 1.0);
        opacity = 1.0;
    } else {
        color = vec4(hue_to_rgb(lod_low / 6.0), 1.0);
        opacity = 1.0;
    }
    return true;
    */
}

#endif