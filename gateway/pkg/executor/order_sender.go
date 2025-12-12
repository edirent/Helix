package executor

import (
	"fmt"

	"github.com/helix-lab/helix/gateway/pkg/router"
	"github.com/helix-lab/helix/gateway/pkg/transport"
)

type OrderSender struct {
	pub    *transport.Publisher
	router *router.SmartRouter
}

func NewOrderSender(pub *transport.Publisher, r *router.SmartRouter) *OrderSender {
	return &OrderSender{pub: pub, router: r}
}

func (s *OrderSender) Send(action transport.Action, books map[string]router.BookView) {
	venue := s.router.Route(action, books)
	action.Venue = venue
	fmt.Printf("[OrderSender] routed action to %s\n", venue)
	s.pub.PublishAction(action)
}
