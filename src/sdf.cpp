#define GL_GLEXT_PROTOTYPES

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <lodepng.h>
#include <emscripten.h>
#include <cmath>
#include "sdf.h"

const char* sdf_vertex = R"glsl(
    /*
     * Copyright (c) 2017 Anton Stepin astiopin@gmail.com
     *
     * Permission is hereby granted, free of charge, to any person obtaining a copy
     * of this software and associated documentation files (the "Software"), to deal
     * in the Software without restriction, including without limitation the rights
     * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
     * copies of the Software, and to permit persons to whom the Software is
     * furnished to do so, subject to the following conditions:
     *
     * The above copyright notice and this permission notice shall be included in all
     * copies or substantial portions of the Software.
     *
     * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
     * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
     * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
     * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
     * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
     * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
     * SOFTWARE.
     *
     */
    uniform float sdf_tex_size; // Size of font texture. Assuming square image
    uniform mat4 ProjMtx;
    attribute vec2  Position;        // Vertex position
    attribute vec2  UV;       // Tex coord
    attribute float sdf_size;   // Signed distance field size in screen pixels
    varying vec2  Frag_UV;
    varying float doffset;
    varying float sdf_texel;
    void main(void) {
        Frag_UV = UV;
        doffset = 1.0 / sdf_size;       // Distance field delta for one screen pixel
        sdf_texel = 1.0 / sdf_tex_size;
        gl_Position = ProjMtx * vec4(Position.xy, 0, 1);
    }
)glsl";

const char* sdf_fragment = R"glsl(
    /*
     * Copyright (c) 2017 Anton Stepin astiopin@gmail.com
     *
     * Permission is hereby granted, free of charge, to any person obtaining a copy
     * of this software and associated documentation files (the "Software"), to deal
     * in the Software without restriction, including without limitation the rights
     * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
     * copies of the Software, and to permit persons to whom the Software is
     * furnished to do so, subject to the following conditions:
     *
     * The above copyright notice and this permission notice shall be included in all
     * copies or substantial portions of the Software.
     *
     * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
     * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
     * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
     * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
     * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
     * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
     * SOFTWARE.
     *
     */
    precision mediump float;
    uniform sampler2D font_tex;
    uniform float hint_amount;
    uniform float subpixel_amount;
    uniform vec3 bg_color;
    uniform vec3 font_color;
    varying vec2  Frag_UV;
    varying float doffset;
    varying float sdf_texel;
    /*
     *  Subpixel coverage calculation
     *
     *  v - edge slope    -1.0 to 1.0          triplet
     *  a - pixel coverage 0.0 to 1.0          coverage
     *
     *      |<- glyph edge                      R  G  B
     *  +---+---+                             +--+--+--+
     *  |   |XXX| v = 1.0 (edge facing west)  |  |xx|XX|
     *  |   |XXX| a = 0.5 (50% coverage)      |  |xx|XX|
     *  |   |XXX|                             |  |xx|XX|
     *  +---+---+                             +--+--+--+
     *    pixel                                0  50 100
     *
     *
     *        R   G   B
     *
     *   1.0        +--+   <- top (abs( v ))
     *              |
     *       -+-----+--+-- <- ceil: 100% coverage
     *        |     |XX|
     *   0.0  |  +--+XX|
     *        |  |xx|XX|
     *       -+--+--+--+-- <- floor: 0% coverage
     *           |
     *  -1.0  +--+         <-  -abs(v)
     *        |
     *        |
     *        |
     *  -2.0  +            <- bottom: -abs(v)-1.0
     */
    vec3 subpixel( float v, float a ) {
        float vt      = 0.6 * v; // 1.0 will make your eyes bleed
        vec3  rgb_max = vec3( -vt, 0.0, vt );
        float top     = abs( vt );
        float bottom  = -top - 1.0;
        float cfloor  = mix( top, bottom, a );
        vec3  res     = clamp( rgb_max - vec3( cfloor ), 0.0, 1.0 );
        return res;
    }
    void main() {
        // Sampling the texture, L pattern
        float sdf       = texture2D( font_tex, Frag_UV ).r;
        float sdf_north = texture2D( font_tex, Frag_UV + vec2( 0.0, sdf_texel ) ).r;
        float sdf_east  = texture2D( font_tex, Frag_UV + vec2( sdf_texel, 0.0 ) ).r;
        // Estimating stroke direction by the distance field gradient vector
        vec2  sgrad     = vec2( sdf_east - sdf, sdf_north - sdf );
        float sgrad_len = max( length( sgrad ), 1.0 / 128.0 );
        vec2  grad      = sgrad / vec2( sgrad_len );
        float vgrad = abs( grad.y ); // 0.0 - vertical stroke, 1.0 - horizontal one

        float horz_scale  = 1.1; // Blurring vertical strokes along the X axis a bit
        float vert_scale  = 0.6; // While adding some contrast to the horizontal strokes
        float hdoffset    = mix( doffset * horz_scale, doffset * vert_scale, vgrad );
        float res_doffset = mix( doffset, hdoffset, hint_amount );

        float alpha       = smoothstep( 0.5 - res_doffset, 0.5 + res_doffset, sdf );
        // Additional contrast
        alpha             = pow( alpha, 1.0 + 0.2 * vgrad * hint_amount );
        // Unfortunately there is no support for ARB_blend_func_extended in WebGL.
        // Fortunately the background is filled with a solid color so we can do
        // the blending inside the shader.

        // Discarding pixels beyond a threshold to minimise possible artifacts.
        if ( alpha < 20.0 / 256.0 ) discard;

        vec3 channels = subpixel( grad.x * 0.5 * subpixel_amount, alpha );
        // For subpixel rendering we have to blend each color channel separately
        vec3 res = mix( bg_color, font_color, channels );

        gl_FragColor = vec4( res, 1.0 );
    }
)glsl";

const char* test_text = R"(
To be, or not to be--that is the question:
Whether 'tis nobler in the mind to suffer
The slings and arrows of outrageous fortune
Or to take arms against a sea of troubles
And by opposing end them. To die, to sleep--
No more--and by a sleep to say we end
The heartache, and the thousand natural shocks
That flesh is heir to. 'Tis a consummation
Devoutly to be wished. To die, to sleep--
To sleep--perchance to dream: ay, there's the rub,
For in that sleep of death what dreams may come
When we have shuffled off this mortal coil,
Must give us pause. There's the respect
That makes calamity of so long life.
)";

u32 sdf_program;
GLuint sdf_vbo;
GLuint sdf_vao;
GLuint sdf_elements;

s32 uniform_texture;
s32 uniform_projection_matrix;
s32 uniform_sdf_tex_size;
s32 uniform_hint_amount;
s32 uniform_subpixel_amount;
s32 uniform_bg_color;
s32 uniform_font_color;

s32 attrib_position;
s32 attrib_uv;
s32 attrib_sdf_size;

u32 sdf_texture_id = 0;

SDF_Glyph glyphs[500];

static void init_roboto() {
    glyphs['0'] = {48, {0.711914, 0}, {0.770264, 0.121094}, 0.004867, 0.004867, 0.048665, 4};
    glyphs['1'] = {49, {0.770264, 0}, {0.813232, 0.121094}, 0.007194, 0.007194, 0.048665, 4};
    glyphs['2'] = {50, {0.813232, 0}, {0.874268, 0.121094}, 0.003936, 0.003936, 0.048665, 4};
    glyphs['3'] = {51, {0.874268, 0}, {0.932617, 0.121094}, 0.003978, 0.003978, 0.048665, 4};
    glyphs['4'] = {52, {0.932617, 0}, {0.996582, 0.121094}, 0.002243, 0.002243, 0.048665, 4};
    glyphs['5'] = {53, {0, 0.121094}, {0.058105, 0.242188}, 0.006517, 0.006517, 0.048665, 4};
    glyphs['6'] = {54, {0.058105, 0.121094}, {0.116455, 0.242188}, 0.005586, 0.005586, 0.048665, 4};
    glyphs['7'] = {55, {0.116455, 0.121094}, {0.17749, 0.242188}, 0.003258, 0.003258, 0.048665, 4};
    glyphs['8'] = {56, {0.17749, 0.121094}, {0.236084, 0.242188}, 0.00474, 0.00474, 0.048665, 4};
    glyphs['9'] = {57, {0.236084, 0.121094}, {0.294189, 0.242188}, 0.004232, 0.004232, 0.048665, 4};
    glyphs['!'] = {33, {0, 0}, {0.028564, 0.121094}, 0.006771, 0.006771, 0.022301, 8};
    glyphs['"'] = {34, {0.028564, 0}, {0.06543, 0.121094}, 0.005755, 0.005755, 0.027718, 8};
    glyphs['#'] = {35, {0.06543, 0}, {0.13208, 0.121094}, 0.005036, 0.005036, 0.053363, 8};
    glyphs['$'] = {36, {0.13208, 0}, {0.190918, 0.121094}, 0.004655, 0.004655, 0.048665, 8};
    glyphs['%'] = {37, {0.190918, 0}, {0.265625, 0.121094}, 0.004443, 0.004443, 0.063477, 8};
    glyphs['&'] = {38, {0.265625, 0}, {0.334473, 0.121094}, 0.004274, 0.004274, 0.05387, 8};
    glyphs['\''] = {39, {0.334473, 0}, {0.360352, 0.121094}, 0.004359, 0.004359, 0.015107, 8};
    glyphs['('] = {40, {0.360352, 0}, {0.4021, 0.121094}, 0.005628, 0.005628, 0.029622, 8};
    glyphs[')'] = {41, {0.4021, 0}, {0.443848, 0.121094}, 0.001608, 0.001608, 0.03013, 8};
    glyphs['*'] = {42, {0.443848, 0}, {0.498291, 0.121094}, 0.001185, 0.001185, 0.037324, 8};
    glyphs['+'] = {43, {0.498291, 0}, {0.559814, 0.121094}, 0.003301, 0.003301, 0.049131, 8};
    glyphs[','] = {44, {0.559814, 0}, {0.591064, 0.121094}, 0.001227, 0.001227, 0.017012, 8};
    glyphs['-'] = {45, {0.591064, 0}, {0.631104, 0.121094}, 0.001566, 0.001566, 0.02391, 8};
    glyphs['.'] = {46, {0.631104, 0}, {0.660156, 0.121094}, 0.006094, 0.006094, 0.022809, 8};
    glyphs['/'] = {47, {0.660156, 0}, {0.711914, 0.121094}, 0.000762, 0.000762, 0.035716, 8};
    glyphs[':'] = {58, {0.294189, 0.121094}, {0.323486, 0.242188}, 0.005671, 0.005671, 0.02099, 8};
    glyphs[';'] = {59, {0.323486, 0.121094}, {0.355713, 0.242188}, 0.001735, 0.001735, 0.018324, 8};
    glyphs['<'] = {60, {0.355713, 0.121094}, {0.409668, 0.242188}, 0.003047, 0.003047, 0.044053, 8};
    glyphs['='] = {61, {0.409668, 0.121094}, {0.464355, 0.242188}, 0.006432, 0.006432, 0.047565, 8};
    glyphs['>'] = {62, {0.464355, 0.121094}, {0.52002, 0.242188}, 0.005671, 0.005671, 0.04528, 8};
    glyphs['?'] = {63, {0.52002, 0.121094}, {0.57373, 0.242188}, 0.003174, 0.003174, 0.040921, 8};
    glyphs['@'] = {64, {0.57373, 0.121094}, {0.662598, 0.242188}, 0.004486, 0.004486, 0.077822, 8};
    glyphs['A'] = {65, {0.662598, 0.121094}, {0.736328, 0.242188}, 0.001185, 0.001185, 0.056536, 2};
    glyphs['B'] = {66, {0.736328, 0.121094}, {0.797607, 0.242188}, 0.007152, 0.007152, 0.053955, 2};
    glyphs['C'] = {67, {0.797607, 0.121094}, {0.864502, 0.242188}, 0.005036, 0.005036, 0.05641, 2};
    glyphs['D'] = {68, {0.864502, 0.121094}, {0.928467, 0.242188}, 0.007152, 0.007152, 0.056833, 2};
    glyphs['E'] = {69, {0.928467, 0.121094}, {0.987061, 0.242188}, 0.007152, 0.007152, 0.049258, 2};
    glyphs['F'] = {70, {0, 0.242188}, {0.057617, 0.363281}, 0.007152, 0.007152, 0.047904, 2};
    glyphs['G'] = {71, {0.057617, 0.242188}, {0.124512, 0.363281}, 0.005163, 0.005163, 0.059033, 2};
    glyphs['H'] = {72, {0.124512, 0.242188}, {0.191162, 0.363281}, 0.007152, 0.007152, 0.061784, 2};
    glyphs['I'] = {73, {0.191162, 0.242188}, {0.21875, 0.363281}, 0.007744, 0.007744, 0.023571, 2};
    glyphs['J'] = {74, {0.21875, 0.242188}, {0.2771, 0.363281}, 0.002243, 0.002243, 0.047819, 2};
    glyphs['K'] = {75, {0.2771, 0.242188}, {0.34375, 0.363281}, 0.007152, 0.007152, 0.054336, 2};
    glyphs['L'] = {76, {0.34375, 0.242188}, {0.400635, 0.363281}, 0.007152, 0.007152, 0.046634, 2};
    glyphs['M'] = {77, {0.400635, 0.242188}, {0.481445, 0.363281}, 0.007152, 0.007152, 0.075664, 2};
    glyphs['N'] = {78, {0.481445, 0.242188}, {0.548096, 0.363281}, 0.007152, 0.007152, 0.061784, 2};
    glyphs['O'] = {79, {0.548096, 0.242188}, {0.616943, 0.363281}, 0.004993, 0.004993, 0.059583, 2};
    glyphs['P'] = {80, {0.616943, 0.242188}, {0.680664, 0.363281}, 0.007152, 0.007152, 0.054674, 2};
    glyphs['Q'] = {81, {0.680664, 0.242188}, {0.75, 0.363281}, 0.004613, 0.004613, 0.059583, 2};
    glyphs['R'] = {82, {0.75, 0.242188}, {0.814209, 0.363281}, 0.007109, 0.007109, 0.053363, 2};
    glyphs['S'] = {83, {0.814209, 0.242188}, {0.878418, 0.363281}, 0.003385, 0.003385, 0.051416, 2};
    glyphs['T'] = {84, {0.878418, 0.242188}, {0.945557, 0.363281}, 0.002074, 0.002074, 0.051712, 2};
    glyphs['U'] = {85, {0, 0.363281}, {0.063965, 0.484375}, 0.005924, 0.005924, 0.056198, 2};
    glyphs['V'] = {86, {0.063965, 0.363281}, {0.13623, 0.484375}, 0.001185, 0.001185, 0.05514, 2};
    glyphs['W'] = {87, {0.13623, 0.363281}, {0.228027, 0.484375}, 0.002581, 0.002581, 0.076891, 2};
    glyphs['X'] = {88, {0.228027, 0.363281}, {0.297119, 0.484375}, 0.002412, 0.002412, 0.054336, 2};
    glyphs['Y'] = {89, {0.297119, 0.363281}, {0.367188, 0.484375}, 0.000635, 0.000635, 0.052051, 2};
    glyphs['Z'] = {90, {0.367188, 0.363281}, {0.431396, 0.484375}, 0.003639, 0.003639, 0.051882, 2};
    glyphs['['] = {91, {0.431396, 0.363281}, {0.466797, 0.484375}, 0.006178, 0.006178, 0.022979, 8};
    glyphs['\\'] = {92, {0.466797, 0.363281}, {0.519287, 0.484375}, 0.001693, 0.001693, 0.035547, 8};
    glyphs[']'] = {93, {0.519287, 0.363281}, {0.554688, 0.484375}, 0.000381, 0.000381, 0.022979, 8};
    glyphs['^'] = {94, {0.554688, 0.363281}, {0.604736, 0.484375}, 0.002708, 0.002708, 0.036224, 8};
    glyphs['_'] = {95, {0.604736, 0.363281}, {0.662842, 0.484375}, 0.000169, 0.000169, 0.039102, 8};
    glyphs['`'] = {96, {0.662842, 0.363281}, {0.699951, 0.484375}, 0.002412, 0.002412, 0.026787, 8};
    glyphs['a'] = {97, {0.699951, 0.363281}, {0.75708, 0.484375}, 0.004613, 0.004613, 0.047142, 1};
    glyphs['b'] = {98, {0.75708, 0.363281}, {0.815186, 0.484375}, 0.005924, 0.005924, 0.048623, 1};
    glyphs['c'] = {99, {0.815186, 0.363281}, {0.873291, 0.484375}, 0.003893, 0.003893, 0.045365, 1};
    glyphs['d'] = {100, {0.873291, 0.363281}, {0.931396, 0.484375}, 0.00402, 0.00402, 0.048877, 1};
    glyphs['e'] = {101, {0.931396, 0.363281}, {0.989746, 0.484375}, 0.003936, 0.003936, 0.045915, 1};
    glyphs['f'] = {102, {0, 0.484375}, {0.047119, 0.605469}, 0.002539, 0.002539, 0.030088, 1};
    glyphs['g'] = {103, {0.047119, 0.484375}, {0.105225, 0.605469}, 0.004062, 0.004062, 0.048623, 1};
    glyphs['h'] = {104, {0.105225, 0.484375}, {0.160645, 0.605469}, 0.005924, 0.005924, 0.047734, 1};
    glyphs['i'] = {105, {0.160645, 0.484375}, {0.189209, 0.605469}, 0.005967, 0.005967, 0.021032, 1};
    glyphs['j'] = {106, {0.189209, 0.484375}, {0.226074, 0.605469}, -0.002751f, -0.002751f, 0.020693, 1};
    glyphs['k'] = {107, {0.226074, 0.484375}, {0.283447, 0.605469}, 0.005967, 0.005967, 0.043926, 1};
    glyphs['l'] = {108, {0.283447, 0.484375}, {0.310791, 0.605469}, 0.006602, 0.006602, 0.021032, 1};
    glyphs['m'] = {109, {0.310791, 0.484375}, {0.394287, 0.605469}, 0.005882, 0.005882, 0.07596, 1};
    glyphs['n'] = {110, {0.394287, 0.484375}, {0.449707, 0.605469}, 0.005924, 0.005924, 0.047819, 1};
    glyphs['o'] = {111, {0.449707, 0.484375}, {0.510742, 0.605469}, 0.003851, 0.003851, 0.049427, 1};
    glyphs['p'] = {112, {0.510742, 0.484375}, {0.568848, 0.605469}, 0.005924, 0.005924, 0.048623, 1};
    glyphs['q'] = {113, {0.568848, 0.484375}, {0.626953, 0.605469}, 0.00402, 0.00402, 0.049258, 1};
    glyphs['r'] = {114, {0.626953, 0.484375}, {0.668457, 0.605469}, 0.005924, 0.005924, 0.029326, 1};
    glyphs['s'] = {115, {0.668457, 0.484375}, {0.724365, 0.605469}, 0.00402, 0.00402, 0.044687, 1};
    glyphs['t'] = {116, {0.724365, 0.484375}, {0.768799, 0.605469}, 0.000381, 0.000381, 0.028311, 1};
    glyphs['u'] = {117, {0.768799, 0.484375}, {0.824219, 0.605469}, 0.005755, 0.005755, 0.047777, 1};
    glyphs['v'] = {118, {0.824219, 0.484375}, {0.882568, 0.605469}, 0.001396, 0.001396, 0.041979, 1};
    glyphs['w'] = {119, {0.882568, 0.484375}, {0.963135, 0.605469}, 0.00182, 0.00182, 0.065127, 1};
    glyphs['x'] = {120, {0, 0.605469}, {0.058838, 0.726562}, 0.001735, 0.001735, 0.042952, 1};
    glyphs['y'] = {121, {0.058838, 0.605469}, {0.117188, 0.726562}, 0.000931, 0.000931, 0.041006, 1};
    glyphs['z'] = {122, {0.117188, 0.605469}, {0.172852, 0.726562}, 0.003724, 0.003724, 0.042952, 1};
    glyphs['{'] = {123, {0.172852, 0.605469}, {0.218018, 0.726562}, 0.002708, 0.002708, 0.029326, 8};
    glyphs['|'] = {124, {0.218018, 0.605469}, {0.243652, 0.726562}, 0.007406, 0.007406, 0.021117, 8};
    glyphs['}'] = {125, {0.243652, 0.605469}, {0.288818, 0.726562}, 0.000804, 0.000804, 0.029326, 8};
    glyphs['~'] = {126, {0.288818, 0.605469}, {0.356201, 0.726562}, 0.005544, 0.005544, 0.058949, 8};
}

static void calculate_font_metrics(SDF_Font& font, SDF_Font_Metrics& font_metrics, float pixel_size, float more_line_gap) {
    font_metrics.cap_scale   = pixel_size / font.cap_height;
    font_metrics.low_scale   = round( font.x_height * font_metrics.cap_scale ) / font.x_height;

    // Ascent should be a whole number since it's used to calculate the baseline
    // position which should lie at the pixel boundary
    font_metrics.ascent      = round( font.ascent * font_metrics.cap_scale );

    // Same for the line height
    font_metrics.line_height = round( font_metrics.cap_scale * ( font.ascent + font.descent + font.line_gap ) + more_line_gap );
}

static ImVec2 prepare_glyph_and_advance(SDF_Vert* out_verts,
                                        ImDrawIdx* out_idx,
                                        ImDrawIdx index_offset,
                                        ImVec2 pos,
                                        SDF_Font &font,
                                        SDF_Font_Metrics &font_metrics,
                                        SDF_Glyph &font_char,
                                        float kern = 0.0) {
    // Low case characters have first bit set in 'flags'
    bool lowcase = ( font_char.flags & 1 ) == 1;

    // Pen position is at the top of the line, Y goes up
    float baseline = pos.y - font_metrics.ascent;

    // Low case chars use their own scale
    float scale = lowcase ? font_metrics.low_scale : font_metrics.cap_scale;

    // Laying out the glyph rectangle
    ImVec2& a = font_char.a;
    ImVec2& c = font_char.c;
    float bottom = baseline - scale * (font.descent + font.iy);
    float top = bottom + scale * (font.row_height);
    float left = pos.x + scale * (font_char.bearing_x + kern - font.ix);
    float right = left + scale * (c.x - a.x);
    float p[] = {left, top, right, bottom};

    // Advancing pen position
    float new_pos_x = pos[0] + scale * ( font_char.advance_x );

    // Signed distance field size in screen pixels
    float sdf_size  = 2.0f * font.iy * scale;

    out_verts[0] = SDF_Vert{{p[0], p[3]}, {a.x, a.y}, sdf_size};
    out_verts[1] = SDF_Vert{{p[2], p[3]}, {c.x, a.y}, sdf_size};
    out_verts[2] = SDF_Vert{{p[2], p[1]}, {c.x, c.y}, sdf_size};
    out_verts[3] = SDF_Vert{{p[0], p[1]}, {a.x, c.y}, sdf_size};

    const ImDrawIdx indices[] = {
            0, 1, 2,
            0, 2, 3
    };

    for (u32 index = 0; index < ARRAY_SIZE(indices); index++) {
        out_idx[index] = index_offset + indices[index];
    }

    return {new_pos_x, pos.y};
}

void init_sdf() {
    Memory_Image font_image;

    if (!load_png_from_disk("resources/roboto.png", font_image)) {
        printf("Failed to load SDF font texture!\n");
    }

    sdf_texture_id = font_image.texture_id;

    init_roboto();

    sdf_program = glCreateProgram();
    u32 vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    u32 fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

    GL_CHECKED(glShaderSource(vertex_shader, 1, &sdf_vertex, 0));
    GL_CHECKED(glShaderSource(fragment_shader, 1, &sdf_fragment, 0));
    GL_CHECKED(glCompileShader(vertex_shader));
    GL_CHECKED(glCompileShader(fragment_shader));

    GL_CHECKED(glAttachShader(sdf_program, vertex_shader));
    GL_CHECKED(glAttachShader(sdf_program, fragment_shader));

    GL_CHECKED(glLinkProgram(sdf_program));

#define BIND_UNIFORM(variable, name) GL_CHECKED((variable) = glGetUniformLocation(sdf_program, (name)))
    BIND_UNIFORM(uniform_texture, "font_tex");
    BIND_UNIFORM(uniform_projection_matrix, "ProjMtx");
    BIND_UNIFORM(uniform_sdf_tex_size, "sdf_tex_size");
    BIND_UNIFORM(uniform_hint_amount, "hint_amount");
    BIND_UNIFORM(uniform_subpixel_amount, "subpixel_amount");
    BIND_UNIFORM(uniform_bg_color, "bg_color");
    BIND_UNIFORM(uniform_font_color, "font_color");

    GL_CHECKED(attrib_position = glGetAttribLocation(sdf_program, "Position"));
    GL_CHECKED(attrib_uv = glGetAttribLocation(sdf_program, "UV"));
    GL_CHECKED(attrib_sdf_size = glGetAttribLocation(sdf_program, "sdf_size"));
#undef BIND_UNIFORM

    GL_CHECKED( glGenBuffers(1, &sdf_vbo) );
    GL_CHECKED( glGenBuffers(1, &sdf_elements) );

    GL_CHECKED( glGenVertexArraysOES(1, &sdf_vao) );
    GL_CHECKED( glBindVertexArrayOES(sdf_vao) );
    GL_CHECKED( glBindBuffer(GL_ARRAY_BUFFER, sdf_vbo) );

    GL_CHECKED( glEnableVertexAttribArray(attrib_position) );
    GL_CHECKED( glEnableVertexAttribArray(attrib_uv) );
    GL_CHECKED( glEnableVertexAttribArray(attrib_sdf_size) );
#define OFFSETOF(TYPE, ELEMENT) ((size_t)&(((TYPE *)0)->ELEMENT))
    GL_CHECKED( glVertexAttribPointer(attrib_position, 2, GL_FLOAT, GL_FALSE, sizeof(SDF_Vert), (GLvoid*) OFFSETOF(SDF_Vert, pos)) );
    GL_CHECKED( glVertexAttribPointer(attrib_uv, 2, GL_FLOAT, GL_FALSE, sizeof(SDF_Vert), (GLvoid*) OFFSETOF(SDF_Vert, uv)) );
    GL_CHECKED( glVertexAttribPointer(attrib_sdf_size, 1, GL_FLOAT, GL_TRUE, sizeof(SDF_Vert), (GLvoid*) OFFSETOF(SDF_Vert, sdf_size)) );
#undef OFFSETOF
}

void render_test_sdf_text(float font_size, const float* orthProjection) {
    glDisable(GL_SCISSOR_TEST);

    float font_color[3] = { 0, 0, 0 };
    float bg_color[3]   = { 1, 1, 1};

    glUseProgram(sdf_program);
    glUniform1i(uniform_texture, 0);
    glUniform1f(uniform_sdf_tex_size, 1024.0f);
    glUniform1f(uniform_hint_amount, 1.0f);
    glUniform1f(uniform_subpixel_amount, 1.0f);
    glUniform3fv(uniform_bg_color, 1, bg_color);
    glUniform3fv(uniform_font_color, 1, font_color);
    glUniformMatrix4fv(uniform_projection_matrix, 1, GL_FALSE, orthProjection);

    glBindVertexArrayOES(sdf_vao);

    SDF_Font roboto{};
    SDF_Font_Metrics font_metrics;
    calculate_font_metrics(roboto, font_metrics, font_size, font_size * 0.2f);

    int text_length = strlen(test_text);

    SDF_Vert* vertices = (SDF_Vert*) MALLOC(sizeof(SDF_Vert) * text_length * 4);
    ImDrawIdx* indices = (ImDrawIdx*) MALLOC(sizeof(ImDrawIdx) * text_length * 6);

    SDF_Vert* vertices_pointer = vertices;
    ImDrawIdx* indices_pointer = indices;

    ImVec2 pos = {2000, 300};

    u32 total_glyphs = 0;
    ImDrawIdx index_offset = 0;

    for (u32 i = 0; i < text_length; i++) {
        SDF_Glyph& glyph = glyphs[test_text[i]];

        if (test_text[i] == '\n') {
            pos.x = 2000.0f;
            pos.y += font_metrics.line_height;
        } else if (test_text[i] == ' ') {
            pos.x += (roboto.space_advance * font_metrics.cap_scale);
        } else {
            pos = prepare_glyph_and_advance(vertices_pointer, indices_pointer, index_offset, pos, roboto, font_metrics, glyph);

            vertices_pointer += 4;
            indices_pointer += 6;
            index_offset += 4;
            total_glyphs++;
        }
    }

    u32 num_vertices = total_glyphs * 4;
    u32 num_indices = total_glyphs * 6;

    glBindBuffer(GL_ARRAY_BUFFER, sdf_vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr) (sizeof(SDF_Vert) * num_vertices), (GLvoid*) (vertices), GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdf_elements);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (sizeof(ImDrawIdx) * num_indices), (GLvoid*) (indices), GL_STREAM_DRAW);

    glBindTexture(
            GL_TEXTURE_2D,
            (GLuint) (intptr_t) sdf_texture_id
    );

    glDrawElements(
            GL_TRIANGLES,
            (GLsizei) num_indices,
            sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
            0
    );

    FREE(vertices);
    FREE(indices);
}