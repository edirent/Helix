package tests

import (
	"testing"
	"time"

	"github.com/helix-lab/helix/gateway/pkg/ws"
)

func TestRouterEmits(t *testing.T) {
	r := ws.NewRouter()
	r.Start()
	defer r.Stop()

	select {
	case <-r.Updates():
	case <-time.After(time.Second):
		t.Fatal("no updates received")
	}
}
