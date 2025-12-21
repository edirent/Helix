#include <cassert>

#include "engine/order_manager.hpp"
#include "engine/types.hpp"

using namespace helix::engine;

void test_cancel_idempotent() {
    OrderManager om;
    Action a;
    a.side = Side::Buy;
    a.size = 1.0;
    a.limit_price = 100.0;
    auto ord = om.place(a, 0, 1000);
    auto res1 = om.cancel(ord.order_id, 10);
    assert(res1.success);
    auto res2 = om.cancel(ord.order_id, 20);
    assert(res2.noop);
    const auto &o = om.orders().at(ord.order_id);
    assert(o.status == OrderStatus::Cancelled);
    assert(om.metrics().orders_cancelled == 1);
    assert(om.metrics().orders_cancel_noop == 1);
}

void test_expire_prevents_fill() {
    OrderManager om;
    Action a;
    a.side = Side::Sell;
    a.size = 2.0;
    a.limit_price = 101.0;
    auto ord = om.place(a, 0, 5);
    om.expire_orders(10);
    const auto &o = om.orders().at(ord.order_id);
    assert(o.status == OrderStatus::Expired);
    Fill f = Fill::filled(Side::Sell, 100.5, 1.0, false, Liquidity::Taker);
    f.order_id = ord.order_id;
    bool ok = om.apply_fill(f, 12);
    assert(!ok);
    assert(om.has_error());
}

void test_replace_semantics() {
    OrderManager om;
    Action a;
    a.side = Side::Buy;
    a.size = 1.5;
    a.limit_price = 99.5;
    auto ord = om.place(a, 0, 1000);
    auto rep = om.replace(ord.order_id, 100.0, 2.0, 50, 2000);
    assert(rep.success);
    assert(rep.new_order.order_id != ord.order_id);
    const auto &old_order = om.orders().at(ord.order_id);
    assert(old_order.status == OrderStatus::Replaced);
    assert(old_order.replaced_by == rep.new_order.order_id);
    const auto &new_order = om.orders().at(rep.new_order.order_id);
    assert(new_order.replaced_from == ord.order_id);

    Fill f = Fill::filled(Side::Buy, 100.0, 2.0, false, Liquidity::Taker);
    f.order_id = rep.new_order.order_id;
    bool ok = om.apply_fill(f, 60);
    assert(ok);
    assert(!om.has_error());
    assert(om.orders().at(rep.new_order.order_id).status == OrderStatus::Filled);
}

void test_illegal_state_transitions() {
    OrderManager om;
    Action a;
    a.side = Side::Buy;
    a.size = 1.0;
    a.limit_price = 100.0;
    auto ord = om.place(a, 0, 1000);
    om.cancel(ord.order_id, 5);
    Fill f = Fill::filled(Side::Buy, 100.0, 1.0, false, Liquidity::Taker);
    f.order_id = ord.order_id;
    bool ok = om.apply_fill(f, 10);
    assert(!ok);
    assert(om.has_error());
}

int main() {
    test_cancel_idempotent();
    test_expire_prevents_fill();
    test_replace_semantics();
    test_illegal_state_transitions();
    return 0;
}
