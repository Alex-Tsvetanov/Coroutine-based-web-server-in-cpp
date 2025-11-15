#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

enum class HttpParseState {
    METHOD,
    PATH,
    VERSION,
    HEADER_KEY,
    HEADER_VALUE,
    BODY,
    COMPLETE,
    ERROR
};

class HttpParser {
public:
    HttpParser() { reset(); }

    void reset() {
        state = HttpParseState::METHOD;
        method_len = 0;
        path_len = 0;
        version_len = 0;
        content_length = 0;
        body_received = 0;
        pos = 0;
    }

    bool parse(const char* data, size_t len);
    bool is_complete() const { return state == HttpParseState::COMPLETE; }
    bool is_error() const { return state == HttpParseState::ERROR; }
    
    std::string_view get_method() const { return std::string_view(method, method_len); }
    std::string_view get_path() const { return std::string_view(path, path_len); }

private:
    HttpParseState state;
    char method[16];
    char path[2048];
    char version[16];
    size_t method_len;
    size_t path_len;
    size_t version_len;
    size_t content_length;
    size_t body_received;
    size_t pos;
};
