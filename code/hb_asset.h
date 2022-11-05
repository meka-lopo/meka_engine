/*
 * Written by Gyuhyun Lee
 */

#ifndef HB_ASSET_H
#define HB_ASSET_H

#define MAX_UNICODE_CODEPOINT 0x10FFFF
#define MAX_SUPPORTED_GLYPH_COUNT 2048

struct TextureAsset2D
{
    void *handle; // handle to the texture in GPU

    // TODO(gh) Also keep the bitmap memory 
    // so that we can re-load the texture when the
    // asset gets unloaded from the GPU, or keep the offset
    // to the pakced asset file?
    i32 width;
    i32 height;
    i32 bytes_per_pixel;
};

struct TextureAsset3D
{
    void *handle;
    i32 width;
    i32 height;
    i32 depth;
    i32 bytes_per_pixel;
};

// TODO(gh) Always assumes that the type of the mesh
// is VertexPN, and index is u32
struct MeshAsset
{
    void *vertex_buffer_handle; 
    void *index_buffer_handle; 

    u32 vertex_count;
    u32 index_count;
};

struct GlyphAsset
{
    f32 left_bearing_px; 
    f32 x_advance_px; // current position will be offset by this amount

    // TODO(gh) double check where we should be using this x offset
    f32 x_offset_px; 
    f32 y_offset_from_baseline_px; 

    v2 dim_px; // size of the bitmap 

    // TODO(gh) glyph to codepoint table?
    u32 unicode_codepoint;

    TextureAsset2D texture;
};

struct FontAsset
{
    u32 max_glyph_count;
    // TODO(gh) This might be a bit slow, as this is 4MB long.
    // We can sort this by the codepoint for faster lookup, but always make sure
    // to measure it!
    u16 *codepoint_to_glyphID_table; // use this to get the glyphID to index glyph & kerning information
    f32 *kerning_advances; // can hold glyph_count*glyph_count amount of data
    GlyphAsset *glyph_assets; // can hold glyph_count amount of data 

    // whenever we wanna move to the newline, we should move glyph_height + line_gap amount
    f32 ascent_from_baseline;
    f32 descent_from_baseline;
    f32 line_gap; 
};

// TODO(gh) This will be invisible from the game code, 
// when we seperate the asset pipeline from the game code
#include "stb_truetype.h"
struct LoadFontInfo
{
    // TODO(gh) put this elsewhere?
    void *device;

    FontAsset *font_asset;

    u16 populated_glyph_count;

    stbtt_fontinfo font_info;
    f32 font_scale;
    f32 desired_font_height_px;
};

struct GameAssets
{
    // TODO(gh) Put device and platform api here?
    FontAsset debug_font_asset;
    u32 populated_mesh_asset;
    MeshAsset mesh_assets[256];
};

#endif

