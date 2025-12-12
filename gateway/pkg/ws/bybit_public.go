package ws

import (
	"time"

	"github.com/helix-lab/helix/gateway/pkg/transport"
)

func StartBybitPublic(out chan<- transport.DepthUpdate, quit <-chan struct{}) {
	ticker := time.NewTicker(200 * time.Millisecond)
	price := 100.0
	for {
		select {
		case <-quit:
			ticker.Stop()
			return
		case <-ticker.C:
			price += 0.02
			out <- transport.DepthUpdate{
				Venue:   "BYBIT",
				Symbol:  "BTCUSDT",
				BestBid: price,
				BestAsk: price + 0.4,
				BidSize: 10.0,
				AskSize: 11.0,
			}
		}
	}
}
