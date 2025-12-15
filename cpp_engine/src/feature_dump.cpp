#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "engine/feature_engine.hpp"
#include "engine/types.hpp"
#include "utils/logger.hpp"

using helix::engine::FeatureEngine;
using helix::engine::OrderbookSnapshot;
using helix::engine::TradeTape;

namespace {

struct Row {
    int64_t ts_ms{0};
    double bid{0.0};
    double ask{0.0};
    double bid_size{0.0};
    double ask_size{0.0};
};

bool parse_row(const std::string &line, Row &row) {
    std::stringstream ss(line);
    std::string cell;
    std::vector<std::string> fields;
    while (std::getline(ss, cell, ',')) {
        fields.push_back(cell);
    }
    if (fields.size() < 5) {
        return false;
    }
    const std::size_t n = fields.size();
    try {
        row.ts_ms = std::stoll(fields[n - 5]);
        row.bid = std::stod(fields[n - 4]);
        row.ask = std::stod(fields[n - 3]);
        row.bid_size = std::stod(fields[n - 2]);
        row.ask_size = std::stod(fields[n - 1]);
    } catch (const std::exception &) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: feature_dump <input.csv> [output.csv]" << std::endl;
        return 1;
    }
    const std::filesystem::path input_path(argv[1]);
    if (!std::filesystem::exists(input_path)) {
        std::cerr << "input file not found: " << input_path << std::endl;
        return 1;
    }

    std::ofstream fout;
    std::ostream *out = &std::cout;
    if (argc > 2) {
        fout.open(argv[2]);
        if (!fout.is_open()) {
            std::cerr << "cannot open output file: " << argv[2] << std::endl;
            return 1;
        }
        out = &fout;
    }

    std::ifstream in(input_path);
    if (!in.is_open()) {
        std::cerr << "cannot open input file: " << input_path << std::endl;
        return 1;
    }

    FeatureEngine engine;
    *out << "ts_ms,best_bid,best_ask,bid_size,ask_size,mid,imbalance,microprice,pressure_bid,pressure_ask,sweep_signal,trend_strength\n";

    std::string line;
    bool header_checked = false;
    while (std::getline(in, line)) {
        if (line.empty()) {
            continue;
        }
        if (!header_checked) {
            header_checked = true;
            if (line.find("best") != std::string::npos) {
                continue;  // skip header
            }
        }

        Row row;
        if (!parse_row(line, row)) {
            helix::utils::warn("feature_dump skipped malformed row: " + line);
            continue;
        }

        const double spread = std::max(0.0, row.ask - row.bid);
        const double mid = (spread > 0.0) ? row.bid + spread / 2.0 : row.bid;
        const TradeTape tape{mid, 0.0};  // no trade info; neutral trend/sweep
        const auto feat = engine.compute(OrderbookSnapshot{row.bid, row.ask, row.bid_size, row.ask_size}, tape);

        *out << row.ts_ms << "," << row.bid << "," << row.ask << "," << row.bid_size << "," << row.ask_size << ",";
        *out << mid << "," << feat.imbalance << "," << feat.microprice << "," << feat.pressure_bid << ","
             << feat.pressure_ask << "," << feat.sweep_signal << "," << feat.trend_strength << "\n";
    }

    return 0;
}
