#pragma once

#include <string>
#include <string_view>
#include <map>

class HttpParser {
public:
    enum class State {
        REQUEST_LINE,
        HEADERS,
        BODY,
        COMPLETE,
        ERROR
    };
    
    HttpParser() : state_(State::REQUEST_LINE), content_length_(0), body_received_(0) {}
    
    // Feed data to parser, returns true if complete request parsed
    bool parse(const uint8_t* data, size_t len) {
        buffer_.append(reinterpret_cast<const char*>(data), len);
        
        while (state_ != State::COMPLETE && state_ != State::ERROR) {
            if (state_ == State::REQUEST_LINE) {
                if (!parse_request_line()) break;
            } else if (state_ == State::HEADERS) {
                if (!parse_headers()) break;
            } else if (state_ == State::BODY) {
                if (!parse_body()) break;
            }
        }
        
        return state_ == State::COMPLETE;
    }
    
    bool is_complete() const { return state_ == State::COMPLETE; }
    bool has_error() const { return state_ == State::ERROR; }
    
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::map<std::string, std::string>& headers() const { return headers_; }
    const std::string& body() const { return body_; }
    
    void reset() {
        state_ = State::REQUEST_LINE;
        buffer_.clear();
        method_.clear();
        path_.clear();
        headers_.clear();
        body_.clear();
        content_length_ = 0;
        body_received_ = 0;
    }
    
private:
    bool parse_request_line() {
        size_t pos = buffer_.find("\r\n");
        if (pos == std::string::npos) return false;
        
        std::string line = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 2);
        
        // Parse "METHOD PATH HTTP/1.1"
        size_t first_space = line.find(' ');
        size_t second_space = line.find(' ', first_space + 1);
        
        if (first_space == std::string::npos || second_space == std::string::npos) {
            state_ = State::ERROR;
            return false;
        }
        
        method_ = line.substr(0, first_space);
        path_ = line.substr(first_space + 1, second_space - first_space - 1);
        
        state_ = State::HEADERS;
        return true;
    }
    
    bool parse_headers() {
        while (true) {
            size_t pos = buffer_.find("\r\n");
            if (pos == std::string::npos) return false;
            
            if (pos == 0) {
                // Empty line, headers complete
                buffer_.erase(0, 2);
                
                auto it = headers_.find("Content-Length");
                if (it != headers_.end()) {
                    content_length_ = std::stoul(it->second);
                    state_ = State::BODY;
                } else {
                    state_ = State::COMPLETE;
                }
                return true;
            }
            
            std::string line = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 2);
            
            size_t colon = line.find(':');
            if (colon == std::string::npos) {
                state_ = State::ERROR;
                return false;
            }
            
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            
            // Trim whitespace from value
            size_t start = value.find_first_not_of(" \t");
            if (start != std::string::npos) {
                value = value.substr(start);
            }
            
            headers_[key] = value;
        }
    }
    
    bool parse_body() {
        if (buffer_.size() < content_length_) {
            return false;
        }
        
        body_ = buffer_.substr(0, content_length_);
        buffer_.erase(0, content_length_);
        state_ = State::COMPLETE;
        return true;
    }
    
    State state_;
    std::string buffer_;
    std::string method_;
    std::string path_;
    std::map<std::string, std::string> headers_;
    std::string body_;
    size_t content_length_;
    size_t body_received_;
};
