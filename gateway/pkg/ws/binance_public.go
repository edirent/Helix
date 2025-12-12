package ws

import (
	"time"

	"github.com/helix-lab/helix/gateway/pkg/transport"
)

func StartBinancePublic(out chan<- transport.DepthUpdate, quit <-chan struct{}) {
	ticker := time.NewTicker(220 * time.Millisecond)
	price := 99.8
	for {
		select {
		case <-quit:
			ticker.Stop()
			return
		case <-ticker.C:
			price += 0.03
			out <- transport.DepthUpdate{
				Venue:   "BINANCE",
				Symbol:  "BTCUSDT",
				BestBid: price,
				BestAsk: price + 0.35,
				BidSize: 9.0,
				AskSize: 10.5,
			}
		}
	}
}
