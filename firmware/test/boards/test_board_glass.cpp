// Where a pixel the product draws lands on the glass each platform is fitted with.
#include "boards/lilygo/t_echo_plus/board.h"
#include "core/bus/bus.h"
#include "doctest/doctest.h"
#include "hardware/platform/host/platform.h"
#include "ui/framebuffer.h"

using namespace skyblip;

TEST_CASE("board: the host glass is unturned, so the simulator shows the framebuffer as drawn") {
    platform::host::Platform platform;
    bus::Bus bus;
    boards::TEchoPlus<platform::host::Platform> board{platform, bus};
    REQUIRE(board.begin() == Status::Ok);

    ui::Framebuffer fb;
    fb.clear(true);
    fb.set_pixel(4, 12, true);
    fb.set_pixel(150, 3, true);
    board.display().present(fb, hal::Refresh::Full, 0);

    const ui::Framebuffer& glass = platform.chips().epd.framebuffer();
    int mismatches = 0;
    for (int y = 0; y < ui::Framebuffer::kH; y++)
        for (int x = 0; x < ui::Framebuffer::kW; x++)
            if (glass.get_pixel(x, y) != fb.get_pixel(x, y)) mismatches++;
    CHECK(mismatches == 0);
}
