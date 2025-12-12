package orderbook

// MergeBest consolidates multiple venue levels into a synthetic NBBO-like level.
func MergeBest(levels map[string]Level) Level {
	best := Level{}
	for _, lvl := range levels {
		if lvl.BestBid > best.BestBid {
			best.BestBid = lvl.BestBid
			best.BidSize = lvl.BidSize
		}
		if best.BestAsk == 0 || (lvl.BestAsk > 0 && lvl.BestAsk < best.BestAsk) {
			best.BestAsk = lvl.BestAsk
			best.AskSize = lvl.AskSize
		}
	}
	return best
}
