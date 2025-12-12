package transport

// ZmqSub is a placeholder subscriber that could be wired to inbound actions/fills.
type ZmqSub struct {
	Endpoint string
}

func NewSubscriber(endpoint string) *ZmqSub {
	return &ZmqSub{Endpoint: endpoint}
}

func (s *ZmqSub) Start() {}

func (s *ZmqSub) Stop() {}
