#include "core/util/json_min.h"

namespace skyblip::json {

namespace {
bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
int key_len(const char* k) {
    int n = 0;
    while (k[n]) n++;
    return n;
}
}

int Reader::value_offset(const char* key) const {
    int klen = key_len(key);
    int i = 0;
    while (i < len_) {
        if (data_[i] != '"') {
            i++;
            continue;
        }
        int ks = i + 1;
        int ke = ks;
        while (ke < len_ && data_[ke] != '"') ke++;
        if (ke >= len_) break;
        bool match = (ke - ks == klen);
        for (int j = 0; match && j < klen; j++)
            if (data_[ks + j] != key[j]) match = false;
        i = ke + 1;
        while (i < len_ && is_ws(data_[i])) i++;
        if (i < len_ && data_[i] == ':') {
            i++;
            while (i < len_ && is_ws(data_[i])) i++;
            if (match) return i;
        }
    }
    return -1;
}

bool Reader::has(const char* key) const { return value_offset(key) >= 0; }

bool Reader::get_int(const char* key, long& out) const {
    int o = value_offset(key);
    if (o < 0) return false;
    bool neg = false;
    if (data_[o] == '-') {
        neg = true;
        o++;
    }
    if (o >= len_ || data_[o] < '0' || data_[o] > '9') return false;
    long v = 0;
    while (o < len_ && data_[o] >= '0' && data_[o] <= '9') v = v * 10 + (data_[o++] - '0');
    out = neg ? -v : v;
    return true;
}

bool Reader::get_bool(const char* key, bool& out) const {
    int o = value_offset(key);
    if (o < 0) return false;
    if (o + 4 <= len_ && data_[o] == 't') {
        out = true;
        return true;
    }
    if (o + 5 <= len_ && data_[o] == 'f') {
        out = false;
        return true;
    }
    return false;
}

bool Reader::get_str(const char* key, char* buf, int cap) const {
    int o = value_offset(key);
    if (o < 0 || data_[o] != '"') return false;
    o++;
    int n = 0;
    while (o < len_ && data_[o] != '"' && n < cap - 1) buf[n++] = data_[o++];
    buf[n] = 0;
    return true;
}

void Writer::sep() {
    if (!first_ && n_ < cap_ - 1) buf_[n_++] = ',';
    first_ = false;
}
void Writer::raw(const char* s) {
    while (*s && n_ < cap_ - 1) buf_[n_++] = *s++;
}
void Writer::raw_str(const char* s) {
    if (n_ < cap_ - 1) buf_[n_++] = '"';
    while (*s && n_ < cap_ - 1) {
        if ((*s == '"' || *s == '\\') && n_ < cap_ - 2) buf_[n_++] = '\\';
        buf_[n_++] = *s++;
    }
    if (n_ < cap_ - 1) buf_[n_++] = '"';
}
void Writer::kv_int(const char* key, long v) {
    sep();
    raw_str(key);
    if (n_ < cap_ - 1) buf_[n_++] = ':';
    char tmp[16];
    int t = 0;
    bool neg = v < 0;
    unsigned long uv = neg ? static_cast<unsigned long>(-v) : static_cast<unsigned long>(v);
    do {
        tmp[t++] = static_cast<char>('0' + uv % 10);
        uv /= 10;
    } while (uv);
    if (neg && n_ < cap_ - 1) buf_[n_++] = '-';
    while (t > 0 && n_ < cap_ - 1) buf_[n_++] = tmp[--t];
}
void Writer::kv_bool(const char* key, bool v) {
    sep();
    raw_str(key);
    if (n_ < cap_ - 1) buf_[n_++] = ':';
    raw(v ? "true" : "false");
}
void Writer::kv_str(const char* key, const char* v) {
    sep();
    raw_str(key);
    if (n_ < cap_ - 1) buf_[n_++] = ':';
    raw_str(v);
}
int Writer::finish() {
    if (n_ < cap_ - 1) buf_[n_++] = '}';
    buf_[n_] = 0;
    return n_;
}

}
