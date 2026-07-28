#ifndef SKYBLIP_CORE_UTIL_RESULT_H
#define SKYBLIP_CORE_UTIL_RESULT_H

#include <cstdint>

namespace skyblip {

enum class Status : uint8_t {
    Ok = 0,
    Invalid,
    OutOfRange,
    Crc,
    Unsupported,
    WouldBlock,
    Down,
    Timeout,
    Full,
    Empty,
    NotFound,
};

constexpr bool is_ok(Status s) { return s == Status::Ok; }
constexpr const char* to_string(Status s) {
    switch (s) {
        case Status::Ok: return "Ok";
        case Status::Invalid: return "Invalid";
        case Status::OutOfRange: return "OutOfRange";
        case Status::Crc: return "Crc";
        case Status::Unsupported: return "Unsupported";
        case Status::WouldBlock: return "WouldBlock";
        case Status::Down: return "Down";
        case Status::Timeout: return "Timeout";
        case Status::Full: return "Full";
        case Status::Empty: return "Empty";
        case Status::NotFound: return "NotFound";
    }
    return "?";
}

template <typename T>
class Result {
   public:
    constexpr Result(T value) : value_(value), status_(Status::Ok) {}
    constexpr Result(Status err) : value_{}, status_(err) {}

    constexpr bool ok() const { return status_ == Status::Ok; }
    constexpr Status status() const { return status_; }
    constexpr const T& value() const { return value_; }
    constexpr T value_or(T fallback) const { return ok() ? value_ : fallback; }

   private:
    T value_;
    Status status_;
};

}

#endif
