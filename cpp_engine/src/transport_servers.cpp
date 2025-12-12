#include "transport/grpc_server.hpp"
#include "transport/zmq_server.hpp"

#include <chrono>
#include <iostream>
#include <thread>

namespace helix::transport {

ZmqServer::ZmqServer(std::string endpoint) : endpoint_(std::move(endpoint)) {}

ZmqServer::~ZmqServer() { stop(); }

void ZmqServer::start() {
    if (running_) {
        return;
    }
    running_ = true;
    worker_ = std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void ZmqServer::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ZmqServer::publish(const FeatureMessage &msg) {
    if (!running_) {
        return;
    }
    std::cout << "[ZMQ @" << endpoint_ << "] feature " << msg.to_string() << std::endl;
}

GrpcServer::GrpcServer(std::string endpoint) : endpoint_(std::move(endpoint)) {}

GrpcServer::~GrpcServer() { stop(); }

void GrpcServer::start() {
    if (running_) {
        return;
    }
    running_ = true;
    worker_ = std::thread([this]() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void GrpcServer::stop() {
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void GrpcServer::publish(const ActionMessage &msg) {
    if (!running_) {
        return;
    }
    std::cout << "[gRPC @" << endpoint_ << "] action " << msg.to_string() << std::endl;
}

}  // namespace helix::transport
