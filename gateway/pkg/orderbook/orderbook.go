package orderbook

import (
	"sync"

	"github.com/helix-lab/helix/gateway/pkg/transport"
)

type Level struct {
	BestBid float64
	BestAsk float64
	BidSize float64
	AskSize float64
}

type Manager struct {
	mu    sync.RWMutex
	books map[string]Level
}

func NewManager() *Manager {
	return &Manager{books: make(map[string]Level)}
}

func (m *Manager) Apply(update transport.DepthUpdate) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.books[update.Venue] = Level{
		BestBid: update.BestBid,
		BestAsk: update.BestAsk,
		BidSize: update.BidSize,
		AskSize: update.AskSize,
	}
}

func (m *Manager) Snapshot() map[string]Level {
	m.mu.RLock()
	defer m.mu.RUnlock()
	cp := make(map[string]Level, len(m.books))
	for k, v := range m.books {
		cp[k] = v
	}
	return cp
}

func (m *Manager) BestVenue() (string, Level) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	bestVenue := ""
	var best Level
	for venue, lvl := range m.books {
		if bestVenue == "" || lvl.BestAsk < best.BestAsk {
			bestVenue = venue
			best = lvl
		}
	}
	return bestVenue, best
}
