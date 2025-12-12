package transport

// DepthUpdate represents a top-of-book change from an exchange.
type DepthUpdate struct {
	Venue   string
	Symbol  string
	BestBid float64
	BestAsk float64
	BidSize float64
	AskSize float64
}

type Action struct {
	Symbol string
	Side   string
	Size   float64
	Venue  string
}

type Fill struct {
	Venue string
	Price float64
	Qty   float64
}
