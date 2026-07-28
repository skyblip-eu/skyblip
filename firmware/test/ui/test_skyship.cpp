// The ownship symbol's placement is a bearing-accuracy property, not decoration:
// bbox-centring it once dragged the wing 4 px forward, which put a target 20 px
// abeam 11 degrees off its true bearing. These pin the hot spot and the symmetry.
#include "doctest/doctest.h"
#include "ui/framebuffer.h"
#include "ui/widgets/skyship.h"

using namespace skyblip::ui;

namespace {
constexpr int kCx = 100;
constexpr int kCy = 100;
constexpr int kSpan = 24;
constexpr int kOriginRow = 5;
}  // namespace

TEST_CASE("skyship: the hot spot lands on the point it was given") {
    Framebuffer fb;
    fb.clear(/*white=*/true);
    draw_skyship(fb, kCx, kCy);

    // The wing's full-span row IS the hot spot, so it must be inked at cy.
    CHECK(fb.get_pixel(kCx, kCy));
    CHECK(fb.get_pixel(kCx - 1, kCy));
    // And nothing may sit above the nose or below the tail.
    for (int x = 0; x < Framebuffer::kW; x++) {
        CHECK_FALSE(fb.get_pixel(x, kCy - kOriginRow - 1));
        CHECK_FALSE(fb.get_pixel(x, kCy - kOriginRow + 16));
    }
}

TEST_CASE("skyship: the sprite is symmetric about the centre pixel pair") {
    Framebuffer fb;
    fb.clear(true);
    draw_skyship(fb, kCx, kCy);

    for (int y = kCy - kOriginRow; y < kCy - kOriginRow + 16; y++)
        for (int d = 0; d < kSpan / 2; d++)
            CHECK(fb.get_pixel(kCx + d, y) == fb.get_pixel(kCx - 1 - d, y));
}

TEST_CASE("skyship: it draws the same aircraft wherever it is asked to") {
    Framebuffer a, b;
    a.clear(true);
    b.clear(true);
    draw_skyship(a, kCx, kCy);
    draw_skyship(b, kCx + 20, kCy + 20);
    CHECK(a.count_black() == b.count_black());
    CHECK(a.count_black() > 40);
}
