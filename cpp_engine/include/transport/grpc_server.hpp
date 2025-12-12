#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "transport/messages.hpp"

namespace helix::transport {

class GrpcServer {
  public:
    explicit GrpcServer(std::string endpoint);
    ~GrpcServer();

    void start();
    void stop();
    void publish(const ActionMessage &msg);

  private:
    std::string endpoint_;
    std::atomic<bool> running_{false};
    std::thread worker_;
};

}  // namespace helix::transport
