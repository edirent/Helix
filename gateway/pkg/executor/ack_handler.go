package executor

import "fmt"

type AckHandler struct{}

func (AckHandler) Handle(orderID string) {
	fmt.Printf("[AckHandler] ack for order %s\n", orderID)
}
