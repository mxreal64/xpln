#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <mdspan>
#include <numeric>
#include <string>
#include <variant>
#include <vector>

namespace xpln::runtime {

using PliValue = std::variant<std::monostate, int64_t, double, std::string>;

class PliArray {
public:
    PliArray(std::vector<std::pair<int, int>> bounds, PliValue default_val = int64_t{0})
        : bounds_(std::move(bounds)), default_val_(std::move(default_val)) {
        std::size_t total_size = 1;
        for (const auto& [low, high] : bounds_) {
            dims_.push_back(high - low + 1);
            total_size *= dims_.back();
        }
        data_.resize(total_size, default_val_);
    }

    std::size_t get_flat_index(const std::vector<int>& indices) const {
        if (indices.size() != bounds_.size()) {
            throw std::out_of_range(std::format("Expected {} dimensions, got {}", bounds_.size(), indices.size()));
        }
        std::size_t flat = 0;
        for (std::size_t i = 0; i < indices.size(); ++i) {
            int idx = indices[i];
            auto [low, high] = bounds_[i];
            if (idx < low || idx > high) {
                throw std::out_of_range(std::format("Subscript {} out of range [{}:{}]", idx, low, high));
            }
            std::size_t offset = idx - low;
            flat = flat * dims_[i] + offset;
        }
        return flat;
    }

    PliValue& operator[](const std::vector<int>& indices) { return data_[get_flat_index(indices)]; }
    const PliValue& operator[](const std::vector<int>& indices) const { return data_[get_flat_index(indices)]; }

private:
    std::vector<std::pair<int, int>> bounds_;
    std::vector<std::size_t> dims_;
    PliValue default_val_;
    std::vector<PliValue> data_;
};

inline std::string pli_concat(const std::string& a, const std::string& b) { return a + b; }

inline std::string pli_substr(const std::string& s, int start, int length = -1) {
    int start_idx = start - 1;
    if (start_idx < 0) start_idx = 0;
    if (static_cast<std::size_t>(start_idx) >= s.size()) return "";
    if (length < 0) return s.substr(start_idx);
    return s.substr(start_idx, length);
}

inline std::string pli_set_substr(std::string s, int start, int length, std::string replacement) {
    int start_idx = start - 1;
    if (start_idx < 0) start_idx = 0;
    if (length >= 0) {
        if (static_cast<int>(replacement.size()) > length) replacement = replacement.substr(0, length);
        else if (static_cast<int>(replacement.size()) < length) replacement.append(length - replacement.size(), ' ');
    }
    if (static_cast<size_t>(start_idx) > s.size()) s.append(start_idx - s.size(), ' ');
    s.replace(start_idx, replacement.size(), replacement);
    return s;
}

inline int pli_index(const std::string& s, const std::string& sub) {
    auto pos = s.find(sub);
    return (pos != std::string::npos) ? static_cast<int>(pos + 1) : 0;
}

inline int pli_length(const std::string& s) { return static_cast<int>(s.size()); }

inline int pli_verify(const std::string& s, const std::string& chars) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (chars.find(s[i]) == std::string::npos) return static_cast<int>(i + 1);
    }
    return 0;
}

inline std::string pli_trim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

inline std::string pli_date() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%y%m%d}", now);
}

inline std::string pli_time() {
    auto now = std::chrono::system_clock::now();
    return std::format("{:%H%M%S}", now);
}

} // namespace xpln::runtime
