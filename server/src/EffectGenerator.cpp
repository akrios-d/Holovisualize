#include "EffectGenerator.h"
#include <algorithm>
#include <unordered_map>

void EffectGenerator::registerFactory(GestureType type, Factory factory) {
    std::lock_guard<std::mutex> lk(mu_);
    factories_[static_cast<uint8_t>(type)] = std::move(factory);
}

void EffectGenerator::onGestures(const std::vector<GestureEvent>& events) {
    std::lock_guard<std::mutex> lk(mu_);
    for (const auto& ev : events) {
        auto it = factories_.find(static_cast<uint8_t>(ev.type));
        if (it == factories_.end()) continue;

        auto effect = it->second();
        effect->trigger(ev);
        active_.push_back(std::move(effect));
    }
}

void EffectGenerator::update(float dtMs) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& e : active_) e->update(dtMs);
    active_.erase(
        std::remove_if(active_.begin(), active_.end(),
                       [](const auto& e) { return e->isExpired(); }),
        active_.end()
    );
}

std::vector<EffectPoint> EffectGenerator::collect() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<EffectPoint> out;
    for (const auto& e : active_) {
        auto pts = e->getPoints();
        out.insert(out.end(), pts.begin(), pts.end());
    }
    return out;
}

int EffectGenerator::activeCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<int>(active_.size());
}
