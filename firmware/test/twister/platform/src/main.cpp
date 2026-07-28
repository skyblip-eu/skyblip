// What the pure-host suite cannot reach: the Zephyr-backed adapters themselves.
// Runs under native_sim in CI and on the board when one is attached.
#include <zephyr/ztest.h>

#include "hardware/platform/zephyr/kvstore.h"

using namespace skyblip;

ZTEST_SUITE(platform_adapters, NULL, NULL, NULL, NULL, NULL);

ZTEST(platform_adapters, test_kvstore_round_trips_through_nvs) {
    platform::zephyr::KvStore kv;
    zassert_equal(kv.begin(), Status::Ok, "NVS did not come up");

    const uint8_t written[] = {1, 2, 3, 4};
    zassert_equal(kv.write("settings", written, sizeof(written)), Status::Ok);

    uint8_t read[8] = {0};
    size_t len = 0;
    zassert_equal(kv.read("settings", read, sizeof(read), len), Status::Ok);
    zassert_equal(len, sizeof(written));
    zassert_mem_equal(read, written, sizeof(written));
}

ZTEST(platform_adapters, test_missing_key_is_not_found) {
    platform::zephyr::KvStore kv;
    zassert_equal(kv.begin(), Status::Ok);
    uint8_t read[4] = {0};
    size_t len = 0;
    zassert_equal(kv.read("nope", read, sizeof(read), len), Status::NotFound);
}
