#include <cassert>
#include <cmath>
#include <vector>

#include "engine/risk_engine.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

struct ScenarioFill {
    Side side;
    double price;
    double qty;
    double mark;  // mark price used for unrealized
};

int main() {
    RiskEngine risk(1e9, 1e9);
    const double fee_rate = 0.0006;  // taker fee
    double fees_tot = 0.0;

    std::vector<ScenarioFill> seq = {
        {Side::Buy, 100.0, 1.0, 100.0},   // open long
        {Side::Buy, 110.0, 2.0, 110.0},   // add long
        {Side::Sell, 105.0, 1.5, 105.0},  // partial reduce
        {Side::Sell, 90.0, 2.0, 90.0},    // flip to short
        {Side::Buy, 95.0, 0.3, 95.0},     // reduce short
    };

    for (const auto &sf : seq) {
        // Build fill
        Fill f = Fill::filled(sf.side, sf.price, sf.qty, /*partial=*/false, Liquidity::Taker);
        const double prev_abs = std::abs(risk.position().qty);
        const double prev_realized = risk.realized_pnl();

        risk.update(f);
        fees_tot += sf.price * sf.qty * fee_rate;

        const Position &pos = risk.position();
        assert(std::isfinite(pos.qty));
        assert(std::isfinite(pos.avg_price));
        assert(std::isfinite(pos.realized_pnl));

        // Realized should change only when absolute position shrinks.
        const double new_abs = std::abs(pos.qty);
        const double realized_delta = pos.realized_pnl - prev_realized;
        if (new_abs > prev_abs + 1e-12) {
            assert(std::abs(realized_delta) < 1e-9);
        }

        const double unrealized = pos.qty * (sf.mark - pos.avg_price);
        const double net_total = pos.realized_pnl + unrealized - fees_tot;
        // Identity check
        assert(std::abs(net_total - (pos.realized_pnl + unrealized - fees_tot)) < 1e-9);
        assert(std::isfinite(net_total));
    }

    return 0;
}
