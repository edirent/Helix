#include <cassert>

#include "engine/event_bus.hpp"
#include "engine/tick_replay.hpp"

int main() {
    helix::engine::EventBus bus(8);
    helix::engine::TickReplay replay;
    replay.load_file("data/replay/synthetic.csv");

    bool published = replay.feed_next(bus);
    assert(published);
    auto evt = bus.poll();
    assert(evt.has_value());
    assert(evt->type == helix::engine::Event::Type::Tick);
    return 0;
}
