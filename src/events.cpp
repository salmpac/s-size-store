#include "events.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace ssize {

bool EventQueue::push(Event ev) {
    std::lock_guard<std::mutex> lk(mu_);
    if (pending_.size() >= max_pending_) {
        ++dropped_;
        return false;
    }
    pending_.push_back(std::move(ev));
    return true;
}

void EventQueue::drain(std::vector<Event>& out) {
    out.clear();
    std::lock_guard<std::mutex> lk(mu_);
    pending_.swap(out);
}

std::size_t EventQueue::dropped() const noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    return dropped_;
}

std::vector<std::uint8_t> hash_ip(std::string_view ip, std::string_view secret) {
    std::array<std::uint8_t, crypto_generichash_BYTES_MIN> out{};
    crypto_generichash(out.data(), out.size(),
                       reinterpret_cast<unsigned char const*>(ip.data()), ip.size(),
                       reinterpret_cast<unsigned char const*>(secret.data()), secret.size());
    return {out.begin(), out.end()};
}

std::string classify_ua(std::string_view user_agent) {
    std::string ua;
    ua.reserve(user_agent.size());
    std::transform(user_agent.begin(), user_agent.end(), std::back_inserter(ua),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    static constexpr std::string_view kBotMarkers[] = {
        "bot", "crawler", "spider", "slurp", "curl", "wget", "python-requests",
        "headlesschrome", "yandex.com/bots", "facebookexternalhit", "preview",
    };
    for (auto marker : kBotMarkers) {
        if (ua.find(marker) != std::string::npos) return "bot";
    }

    static constexpr std::string_view kMobileMarkers[] = {
        "mobile", "android", "iphone", "ipad", "ipod", "opera mini",
    };
    for (auto marker : kMobileMarkers) {
        if (ua.find(marker) != std::string::npos) return "mobile";
    }

    if (ua.empty()) return "bot";  // no UA at all is never a real browser
    return "desktop";
}

}  // namespace ssize
