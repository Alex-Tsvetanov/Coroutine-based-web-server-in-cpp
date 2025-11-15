#include "http_parser.h"

bool HttpParser::parse(const char* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        char c = data[i];

        switch (state) {
        case HttpParseState::METHOD:
            if (c == ' ') {
                state = HttpParseState::PATH;
                pos = 0;
            } else if (method_len < sizeof(method) - 1) {
                method[method_len++] = c;
            } else {
                state = HttpParseState::ERROR;
                return false;
            }
            break;

        case HttpParseState::PATH:
            if (c == ' ') {
                state = HttpParseState::VERSION;
                pos = 0;
            } else if (path_len < sizeof(path) - 1) {
                path[path_len++] = c;
            } else {
                state = HttpParseState::ERROR;
                return false;
            }
            break;

        case HttpParseState::VERSION:
            if (c == '\r') {
                // Expect \n next
            } else if (c == '\n') {
                state = HttpParseState::HEADER_KEY;
                pos = 0;
            } else if (version_len < sizeof(version) - 1) {
                version[version_len++] = c;
            }
            break;

        case HttpParseState::HEADER_KEY:
            if (c == '\r') {
                // Expect \n for end of headers
            } else if (c == '\n' && pos == 0) {
                // Empty line, end of headers
                if (content_length == 0) {
                    state = HttpParseState::COMPLETE;
                    return true;
                } else {
                    state = HttpParseState::BODY;
                }
            } else if (c == ':') {
                state = HttpParseState::HEADER_VALUE;
                pos = 0;
            } else {
                pos++;
            }
            break;

        case HttpParseState::HEADER_VALUE:
            if (c == '\r') {
                // Expect \n
            } else if (c == '\n') {
                state = HttpParseState::HEADER_KEY;
                pos = 0;
            }
            break;

        case HttpParseState::BODY:
            body_received++;
            if (body_received >= content_length) {
                state = HttpParseState::COMPLETE;
                return true;
            }
            break;

        case HttpParseState::COMPLETE:
        case HttpParseState::ERROR:
            return state == HttpParseState::COMPLETE;
        }
    }

    return state == HttpParseState::COMPLETE;
}
