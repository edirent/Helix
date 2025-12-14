#include <iostream>
#include <string>

#include "engine/decision_engine.hpp"
#include "engine/event_bus.hpp"
#include "engine/feature_engine.hpp"
#include "engine/matching_engine.hpp"
#include "engine/recorder.hpp"
#include "engine/risk_engine.hpp"
#include "engine/tick_replay.hpp"
#include "transport/grpc_server.hpp"
#include "transport/zmq_server.hpp"
#include "utils/logger.hpp"

using namespace helix;

int main(int argc, char **argv) {
    std::string replay_source = "data/replay/synthetic.csv";
    if (argc > 1) {
        replay_source = argv[1];
    }

    engine::EventBus bus(64);
    engine::TickReplay replay;
    replay.load_file(replay_source);

    engine::FeatureEngine feature_engine;
    engine::DecisionEngine decision_engine;
    engine::RiskEngine risk_engine(5.0, 250000.0);
    engine::MatchingEngine matching_engine;
    engine::Recorder recorder("engine_events.log");
    engine::TradeTape tape{100.0, 1.0};

    transport::ZmqServer feature_pub("tcp://*:7001");
    transport::GrpcServer action_pub("0.0.0.0:50051");
    feature_pub.start();
    action_pub.start();

    while (replay.feed_next(bus)) {
        auto evt = bus.poll();
        if (!evt) {
            continue;
        }
        recorder.record(*evt);

        auto feature = feature_engine.compute(replay.current_book(), tape);
        feature_pub.publish({feature, "SIM"});

        auto action = decision_engine.decide(feature);
        if (risk_engine.validate(action, replay.current_book().best_ask)) {
            auto fill = matching_engine.simulate(action, replay.current_book());
            risk_engine.update(fill);
            std::string payload = "fill price=" + std::to_string(fill.price) + " qty=" + std::to_string(fill.qty);
            bus.publish(engine::Event{engine::Event::Type::Fill, payload});
            action_pub.publish({action, "SIM"});
        }
    }

    recorder.flush();
    feature_pub.stop();
    action_pub.stop();
    utils::info("Engine run complete.");
    return 0;
}
