package tests

import (
	"testing"

	"github.com/helix-lab/helix/gateway/pkg/orderbook"
	"github.com/helix-lab/helix/gateway/pkg/transport"
)

func TestOrderbookApply(t *testing.T) {
	mgr := orderbook.NewManager()
	update := transport.DepthUpdate{
		Venue:   "BYBIT",
		Symbol:  "BTCUSDT",
		BestBid: 100.0,
		BestAsk: 100.5,
		BidSize: 10,
		AskSize: 11,
	}
	mgr.Apply(update)
	venue, level := mgr.BestVenue()
	if venue != "BYBIT" {
		t.Fatalf("expected BYBIT, got %s", venue)
	}
	if level.BestAsk != 100.5 {
		t.Fatalf("wrong ask: %f", level.BestAsk)
	}
}
