package executor

import (
	"fmt"

	"github.com/helix-lab/helix/gateway/pkg/transport"
)

type FillHandler struct{}

func (FillHandler) Handle(fill transport.Fill) {
	fmt.Printf("[FillHandler] fill from %s qty=%.2f price=%.2f\n", fill.Venue, fill.Qty, fill.Price)
}
