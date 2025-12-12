package transport

import (
	"fmt"
)

type Publisher struct {
	Endpoint string
}

func NewPublisher(endpoint string) *Publisher {
	return &Publisher{Endpoint: endpoint}
}

func (p *Publisher) PublishDepth(update DepthUpdate) {
	fmt.Printf("[ZMQ pub %s] depth %s bid=%.2f ask=%.2f\n", p.Endpoint, update.Venue, update.BestBid, update.BestAsk)
}

func (p *Publisher) PublishAction(action Action) {
	fmt.Printf("[ZMQ pub %s] action %+v\n", p.Endpoint, action)
}
