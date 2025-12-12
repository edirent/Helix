package ws

import "github.com/helix-lab/helix/gateway/pkg/transport"

// StartBybitPrivate would typically stream fills/acks. Here we keep it simple.
func StartBybitPrivate(out chan<- transport.Fill, quit <-chan struct{}) {
	<-quit
	// no-op in skeleton
	_ = out
}
