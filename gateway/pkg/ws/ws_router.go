package ws

import (
	"context"

	"github.com/helix-lab/helix/gateway/pkg/transport"
)

type Router struct {
	updates chan transport.DepthUpdate
	quit    context.CancelFunc
	ctx     context.Context
}

func NewRouter() *Router {
	ctx, cancel := context.WithCancel(context.Background())
	return &Router{
		updates: make(chan transport.DepthUpdate, 32),
		quit:    cancel,
		ctx:     ctx,
	}
}

func (r *Router) Start() {
	go StartBybitPublic(r.updates, r.ctx.Done())
	go StartBinancePublic(r.updates, r.ctx.Done())
}

func (r *Router) Updates() <-chan transport.DepthUpdate {
	return r.updates
}

func (r *Router) Stop() {
	r.quit()
}
