// core/util/json_min.h: a tiny, allocation-free reader/writer for FLAT JSON
#ifndef SKYBLIP_CORE_UTIL_JSON_MIN_H
#define SKYBLIP_CORE_UTIL_JSON_MIN_H

#include <cstddef>
#include <cstdint>

namespace skyblip::json {

class Reader {
   public:
    Reader(const char* data, int len) : data_(data), len_(len) {}

    bool get_int(const char* key, long& out) const;
    bool get_bool(const char* key, bool& out) const;
    bool get_str(const char* key, char* buf, int cap) const;
    bool has(const char* key) const;

   private:
    const char* data_;
    int len_;
    int value_offset(const char* key) const;
};

class Writer {
   public:
    Writer(char* buf, int cap) : buf_(buf), cap_(cap) { buf_[n_++] = '{'; }
    void kv_int(const char* key, long v);
    void kv_bool(const char* key, bool v);
    void kv_str(const char* key, const char* v);
    int finish();
    int length() const { return n_; }

   private:
    void sep();
    void raw(const char* s);
    void raw_str(const char* s);
    char* buf_;
    int cap_;
    int n_{0};
    bool first_{true};
};

}

#endif
