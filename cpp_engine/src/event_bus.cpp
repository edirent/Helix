#include "engine/event_bus.hpp"

namespace helix::engine {

EventBus::EventBus(std::size_t capacity) : buffer_(capacity) {}

bool EventBus::publish(const Event &event) { return buffer_.push(event); }

std::optional<Event> EventBus::poll() { return buffer_.pop(); }

std::size_t EventBus::capacity() const { return buffer_.capacity(); }

bool EventBus::empty() const { return buffer_.empty(); }

}  // namespace helix::engine
