#include "../src/sprite_data.h"

#include <assert.h>
#include <stdio.h>

static void expect_png(int generation, int national, int width, int height) {
    SpriteAsset asset{};
    assert(sprite_asset(generation, national, &asset));
    assert(asset.data && asset.length > 100);
    assert(asset.data[0] == 0x89 && asset.data[1] == 'P' &&
           asset.data[2] == 'N' && asset.data[3] == 'G');
    assert(asset.data[24] == 8);  // normalized 8-bit RGBA
    assert(asset.data[25] == 6);  // PNG truecolor + alpha
    assert(asset.width == width && asset.height == height);
}

int main() {
    expect_png(1, 25, 40, 40);   // WebUI-matching Red/Blue gray Pikachu
    expect_png(2, 251, 40, 40);  // WebUI-matching Crystal Celebi
    expect_png(3, 386, 64, 64);  // Emerald Deoxys
    SpriteAsset missing{};
    assert(!sprite_asset(1, 152, &missing));
    assert(!sprite_asset(4, 25, &missing));
    puts("sprite asset tests passed");
}
