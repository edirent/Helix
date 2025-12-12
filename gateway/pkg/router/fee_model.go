package router

type FeeModel struct {
	Taker map[string]float64
}

func DefaultFees() FeeModel {
	return FeeModel{
		Taker: map[string]float64{
			"BYBIT":   0.0006,
			"BINANCE": 0.0005,
		},
	}
}

func (f FeeModel) ApplyAsk(venue string, ask float64) float64 {
	return ask * (1 + f.Taker[venue])
}

func (f FeeModel) ApplyBid(venue string, bid float64) float64 {
	return bid * (1 - f.Taker[venue])
}
