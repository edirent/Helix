package router

import "math"

type BookView struct {
	BestBid float64
	BestAsk float64
}

type SmartRouter struct {
	fees FeeModel
}

func NewSmartRouter(fees FeeModel) *SmartRouter {
	return &SmartRouter{fees: fees}
}

// Route selects the venue with the best adjusted price for the desired side.
func (r *SmartRouter) Route(action interface{ Side string }, books map[string]BookView) string {
	if len(books) == 0 {
		return "SIM"
	}

	switch action.Side {
	case "BUY":
		bestVenue := ""
		bestPrice := math.MaxFloat64
		for venue, book := range books {
			ask := r.fees.ApplyAsk(venue, book.BestAsk)
			if ask < bestPrice {
				bestPrice = ask
				bestVenue = venue
			}
		}
		if bestVenue == "" {
			bestVenue = "SIM"
		}
		return bestVenue
	case "SELL":
		bestVenue := ""
		bestPrice := 0.0
		for venue, book := range books {
			bid := r.fees.ApplyBid(venue, book.BestBid)
			if bid > bestPrice {
				bestPrice = bid
				bestVenue = venue
			}
		}
		if bestVenue == "" {
			bestVenue = "SIM"
		}
		return bestVenue
	default:
		return "SIM"
	}
}
