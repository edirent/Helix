package main

import (
	"fmt"
	"time"

	"github.com/helix-lab/helix/gateway/pkg/executor"
	"github.com/helix-lab/helix/gateway/pkg/latency"
	"github.com/helix-lab/helix/gateway/pkg/orderbook"
	"github.com/helix-lab/helix/gateway/pkg/router"
	"github.com/helix-lab/helix/gateway/pkg/transport"
	"github.com/helix-lab/helix/gateway/pkg/ws"
)

func main() {
	wsRouter := ws.NewRouter()
	bookMgr := orderbook.NewManager()
	pub := transport.NewPublisher("tcp://*:6001")
	fees := router.DefaultFees()
	smart := router.NewSmartRouter(fees)
	sender := executor.NewOrderSender(pub, smart)

	wsRouter.Start()
	defer wsRouter.Stop()

	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	actionsSent := 0
	for actionsSent < 5 {
		select {
		case update := <-wsRouter.Updates():
			bookMgr.Apply(update)
			pub.PublishDepth(update)
		case <-ticker.C:
			books := bookMgr.Snapshot()
			if len(books) == 0 {
				continue
			}
			merged := orderbook.MergeBest(books)
			views := make(map[string]router.BookView, len(books))
			for venue, lvl := range books {
				views[venue] = router.BookView{BestBid: lvl.BestBid, BestAsk: lvl.BestAsk}
			}
			action := transport.Action{Symbol: "BTCUSDT", Side: "BUY", Size: 0.01}
			prof := latency.Start("route_and_send")
			sender.Send(action, views)
			prof.Stop()
			fmt.Printf("[Gateway] NBBO bid=%.2f ask=%.2f\n", merged.BestBid, merged.BestAsk)
			actionsSent++
		}
	}
	fmt.Println("Gateway simulation finished.")
}
