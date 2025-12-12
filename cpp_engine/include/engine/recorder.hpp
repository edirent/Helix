#pragma once

#include <fstream>
#include <string>

#include "engine/types.hpp"

namespace helix::engine {

class Recorder {
  public:
    explicit Recorder(const std::string &path);
    ~Recorder();

    void record(const Event &event);
    void flush();

  private:
    std::ofstream out_;
};

}  // namespace helix::engine
