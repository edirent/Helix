package latency

import (
	"fmt"
	"time"
)

type Profiler struct {
	start time.Time
	label string
}

func Start(label string) Profiler {
	return Profiler{start: time.Now(), label: label}
}

func (p Profiler) Stop() {
	elapsed := time.Since(p.start)
	fmt.Printf("[Profiler] %s took %s\n", p.label, elapsed)
}
