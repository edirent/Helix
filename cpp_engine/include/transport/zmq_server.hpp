#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "transport/messages.hpp"

namespace helix::transport {

class ZmqServer {
  public:
    explicit ZmqServer(std::string endpoint);
    ~ZmqServer();

    void start();
    void stop();
    void publish(const FeatureMessage &msg);

  private:
    std::string endpoint_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace helix::transport
