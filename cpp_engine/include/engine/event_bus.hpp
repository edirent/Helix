#pragma once

#include <cstddef>
#include <optional>

#include "engine/types.hpp"
#include "utils/ring_buffer.hpp"

namespace helix::engine {

class EventBus {
  public:
    explicit EventBus(std::size_t capacity = 1024);
    bool publish(const Event &event);
    std::optional<Event> poll();
    std::size_t capacity() const;
    bool empty() const;

  private:
    helix::utils::RingBuffer<Event> buffer_;
};

}  // namespace helix::engine
