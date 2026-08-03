// products/skyblip_go/features.h: what this product claims to do, as a bitset a
// service is constructed with.
//
// It lives apart from products/skyblip_go/product.h so a service can be handed
// the claim it implements without including the product that owns it. A feature
// with no reader is a line of marketing inside a header: the list said
// CompanionLink for as long as the tree had a NMEA characteristic nothing ever
// wrote to, and nothing anywhere could tell.
#ifndef SKYBLIP_PRODUCTS_SKYBLIP_GO_FEATURES_H
#define SKYBLIP_PRODUCTS_SKYBLIP_GO_FEATURES_H

#include <cstdint>

namespace skyblip::go {

enum class Feature : uint32_t {
    None = 0,
    AdslRx = 1u << 0,
    UplinkRx = 1u << 1,
    AdslTx = 1u << 6,
    // Receive only: the same M-band dwell as AdslRx, framed by the sync window
    // the two systems share (core/protocol/air.h). We never transmit it.
    AlptasRx = 1u << 7,
    Radar = 1u << 2,
    Alarms = 1u << 3,
    Instruments = 1u << 4,
    CompanionLink = 1u << 5,
};

constexpr Feature kFeatures = static_cast<Feature>(
    static_cast<uint32_t>(Feature::AdslRx) | static_cast<uint32_t>(Feature::UplinkRx) |
    static_cast<uint32_t>(Feature::Radar) | static_cast<uint32_t>(Feature::Alarms) |
    static_cast<uint32_t>(Feature::Instruments) | static_cast<uint32_t>(Feature::CompanionLink) |
    static_cast<uint32_t>(Feature::AdslTx) | static_cast<uint32_t>(Feature::AlptasRx));

// Named apart from hal::has so a service reading both cannot pick the wrong one.
constexpr bool has_feature(Feature declared, Feature one) {
    return (static_cast<uint32_t>(declared) & static_cast<uint32_t>(one)) != 0;
}

}  // namespace skyblip::go

#endif
