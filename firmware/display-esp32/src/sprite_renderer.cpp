#include "sprite_renderer.h"

#include "sprite_data.h"

#include <PNGdec.h>
#include <TFT_eSPI.h>

namespace {
struct RenderContext {
    TFT_eSPI *tft;
    int x;
    int y;
    int size;
    int source_width;
    int source_height;
    uint32_t background_rgb;
};

PNG decoder;
uint16_t source_line[96];
uint16_t scaled_line[96];

uint32_t rgb565_to_png_background(uint16_t color) {
    uint32_t r = (color >> 11) & 0x1F;
    uint32_t g = (color >> 5) & 0x3F;
    uint32_t b = color & 0x1F;
    // PNGdec expects 00BBGGRR, not the conventional 00RRGGBB ordering.
    return ((b * 255 / 31) << 16) | ((g * 255 / 63) << 8) | (r * 255 / 31);
}

int draw_line(PNGDRAW *line) {
    auto *ctx = static_cast<RenderContext *>(line->pUser);
    decoder.getLineAsRGB565(line, source_line, PNG_RGB565_LITTLE_ENDIAN,
                            ctx->background_rgb);
    int dest_y0 = line->y * ctx->size / ctx->source_height;
    int dest_y1 = (line->y + 1) * ctx->size / ctx->source_height;
    if (dest_y1 <= dest_y0) return 1;
    for (int x = 0; x < ctx->size; ++x) {
        int source_x = x * ctx->source_width / ctx->size;
        scaled_line[x] = source_line[source_x];
    }
    for (int y = dest_y0; y < dest_y1; ++y)
        ctx->tft->pushImage(ctx->x, ctx->y + y, ctx->size, 1, scaled_line);
    return 1;
}
}  // namespace

bool draw_pokemon_sprite(TFT_eSPI &tft, int generation, int national,
                         int x, int y, int size, uint16_t background) {
    if (size < 1 || size > 96) return false;
    SpriteAsset asset{};
    if (!sprite_asset(generation, national, &asset)) return false;
    tft.fillRect(x, y, size, size, background);
    RenderContext context{&tft, x, y, size, asset.width, asset.height,
                          rgb565_to_png_background(background)};
    bool previous_swap = tft.getSwapBytes();
    tft.setSwapBytes(true); // PNGdec returns host-endian RGB565; TFT wants MSB first.
    int rc = decoder.openFLASH(const_cast<uint8_t *>(asset.data),
                               (int)asset.length, draw_line);
    if (rc != PNG_SUCCESS) {
        tft.setSwapBytes(previous_swap);
        return false;
    }
    rc = decoder.decode(&context, 0);
    decoder.close();
    tft.setSwapBytes(previous_swap);
    return rc == PNG_SUCCESS;
}
