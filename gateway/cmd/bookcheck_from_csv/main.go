package main

import (
	"encoding/csv"
	"errors"
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"strconv"
	"strings"
)

type delta struct {
	seq      int64
	prevSeq  int64
	snapshot bool
	tsMs     int64
	side     rune // 'b' or 'a'
	price    float64
	qty      float64
}

type bookState struct {
	bids               map[float64]float64
	asks               map[float64]float64
	lastSeq            int64
	lastTsMs           int64
	snapshotInProgress bool
	counter            int
	bestBid            float64
	bestAsk            float64
	bidSize            float64
	askSize            float64
}

func newState() *bookState {
	return &bookState{
		bids:     make(map[float64]float64),
		asks:     make(map[float64]float64),
		lastSeq:  -1,
		lastTsMs: 0,
	}
}

func containsAlpha(fields []string) bool {
	for _, f := range fields {
		for _, c := range f {
			if ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') {
				return true
			}
		}
	}
	return false
}

func trim(s string) string {
	return strings.TrimSpace(s)
}

func parseDelta(fields []string, header map[string]int, headerKnown bool) (delta, bool, error) {
	var d delta

	getIndex := func(name string) int {
		if !headerKnown {
			return -1
		}
		if idx, ok := header[strings.ToLower(name)]; ok {
			return idx
		}
		return -1
	}
	getInt64 := func(idx int, def int64) int64 {
		if idx < 0 || idx >= len(fields) {
			return def
		}
		v, err := strconv.ParseInt(trim(fields[idx]), 10, 64)
		if err != nil {
			return def
		}
		return v
	}
	getFloat := func(idx int, def float64) float64 {
		if idx < 0 || idx >= len(fields) {
			return def
		}
		v, err := strconv.ParseFloat(trim(fields[idx]), 64)
		if err != nil {
			return def
		}
		return v
	}

	// Positional fallbacks when no header.
	posTS, posSeq, posPrev, posType, posSide, posPrice, posSize := 0, 1, 2, 3, 4, 5, 6
	usePositional := !headerKnown

	tsIdx := getIndex("ts_ms")
	seqIdx := getIndex("seq")
	prevIdx := getIndex("prev_seq")
	typeIdx := getIndex("type")
	sideIdx := getIndex("book_side")
	if sideIdx < 0 {
		sideIdx = getIndex("side")
	}
	priceIdx := getIndex("price")
	sizeIdx := getIndex("size")

	if usePositional {
		if len(fields) <= posSeq {
			return d, true, nil
		}
	}

	n := len(fields)
	if usePositional {
		if n > posTS {
			d.tsMs = getInt64(posTS, 0)
		}
		if n > posSeq {
			d.seq = getInt64(posSeq, 0)
		}
		if n > posPrev {
			d.prevSeq = getInt64(posPrev, -1)
		}
		if n > posType {
			t := strings.ToLower(trim(fields[posType]))
			d.snapshot = t == "snapshot" || t == "snap" || t == "full"
		}
		if n > posSide {
			side := trim(fields[posSide])
			if side != "" {
				c := rune(strings.ToLower(side)[0])
				if c == 'b' || c == 'a' {
					d.side = c
				}
			}
		}
		if n > posPrice {
			d.price = getFloat(posPrice, 0)
		}
		if n > posSize {
			d.qty = getFloat(posSize, 0)
		}
	} else {
		d.tsMs = getInt64(tsIdx, 0)
		d.seq = getInt64(seqIdx, 0)
		d.prevSeq = getInt64(prevIdx, -1)
		t := strings.ToLower(trim(getField(fields, typeIdx)))
		d.snapshot = t == "snapshot" || t == "snap" || t == "full"
		side := trim(getField(fields, sideIdx))
		if side != "" {
			c := rune(strings.ToLower(side)[0])
			if c == 'b' || c == 'a' {
				d.side = c
			}
		}
		d.price = getFloat(priceIdx, 0)
		d.qty = getFloat(sizeIdx, 0)
	}

	if d.side != 'b' && d.side != 'a' {
		return d, true, nil // skip invalid side rows
	}
	return d, false, nil
}

func getField(fields []string, idx int) string {
	if idx < 0 || idx >= len(fields) {
		return ""
	}
	return fields[idx]
}

func (s *bookState) apply(d delta, every int, outWriter *csv.Writer) error {
	const eps = 1e-9
	implicitSnapshot := !d.snapshot && d.prevSeq == 0
	if d.snapshot || implicitSnapshot {
		for k := range s.bids {
			delete(s.bids, k)
		}
		for k := range s.asks {
			delete(s.asks, k)
		}
		s.snapshotInProgress = true
	} else {
		if s.lastSeq >= 0 && d.prevSeq != s.lastSeq {
			return fmt.Errorf("seq gap: prev=%d next_prev=%d", s.lastSeq, d.prevSeq)
		}
		if s.lastSeq >= 0 && d.seq <= s.lastSeq {
			return fmt.Errorf("seq rollback: prev=%d next_seq=%d", s.lastSeq, d.seq)
		}
	}

	s.lastSeq = d.seq
	if d.tsMs > 0 {
		s.lastTsMs = d.tsMs
	} else {
		s.lastTsMs++
	}

	if d.qty < 0 {
		return fmt.Errorf("negative qty delta at seq=%d", d.seq)
	}

	if d.side == 'b' {
		if math.Abs(d.qty) < eps {
			delete(s.bids, d.price)
		} else {
			s.bids[d.price] = d.qty
		}
	} else {
		if math.Abs(d.qty) < eps {
			delete(s.asks, d.price)
		} else {
			s.asks[d.price] = d.qty
		}
	}

	s.rebuild()

	if s.snapshotInProgress && s.bestBid > 0 && s.bestAsk > 0 {
		s.snapshotInProgress = false
	}

	if !s.snapshotInProgress {
		if !(s.bestBid > 0 && s.bestAsk > 0 && s.bestBid < s.bestAsk) {
			return errors.New("best_bid/best_ask invalid")
		}
		if !(s.bidSize > 0 && s.askSize > 0) {
			return errors.New("top sizes non-positive")
		}
		mid := (s.bestBid + s.bestAsk) / 2
		if !(mid > 0) || math.IsNaN(mid) || math.IsInf(mid, 0) {
			return errors.New("mid invalid")
		}
	}

	s.counter++
	if every > 0 && (s.counter%every) == 0 && outWriter != nil {
		record := []string{
			strconv.FormatInt(s.lastTsMs, 10),
			strconv.FormatInt(s.lastSeq, 10),
			fmt.Sprintf("%.10g", s.bestBid),
			fmt.Sprintf("%.10g", s.bestAsk),
			fmt.Sprintf("%.10g", s.bidSize),
			fmt.Sprintf("%.10g", s.askSize),
		}
		if err := outWriter.Write(record); err != nil {
			return err
		}
	}

	return nil
}

func (s *bookState) rebuild() {
	s.bestBid, s.bidSize = 0, 0
	s.bestAsk, s.askSize = 0, 0
	for px, qty := range s.bids {
		if qty <= 0 {
			continue
		}
		if s.bestBid == 0 || px > s.bestBid {
			s.bestBid = px
			s.bidSize = qty
		}
	}
	for px, qty := range s.asks {
		if qty <= 0 {
			continue
		}
		if s.bestAsk == 0 || px < s.bestAsk {
			s.bestAsk = px
			s.askSize = qty
		}
	}
}

func main() {
	inPath := flag.String("in", "data/replay/bybit_l2.csv", "input CSV path")
	outPath := flag.String("out", "go_bookcheck.csv", "output CSV path")
	every := flag.Int("every", 100, "bookcheck stride")
	flag.Parse()

	in, err := os.Open(*inPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to open input: %v\n", err)
		os.Exit(1)
	}
	defer in.Close()

	out, err := os.Create(*outPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "failed to create output: %v\n", err)
		os.Exit(1)
	}
	defer out.Close()

	writer := csv.NewWriter(out)
	if err := writer.Write([]string{"ts_ms", "seq", "best_bid", "best_ask", "bid_size", "ask_size"}); err != nil {
		fmt.Fprintf(os.Stderr, "failed to write header: %v\n", err)
		os.Exit(1)
	}

	reader := csv.NewReader(in)
	reader.FieldsPerRecord = -1
	header := make(map[string]int)
	headerKnown := false

	state := newState()

	for {
		fields, err := reader.Read()
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			if errors.Is(err, csv.ErrFieldCount) {
				continue
			}
			fmt.Fprintf(os.Stderr, "read error: %v\n", err)
			os.Exit(1)
		}
		if len(fields) == 0 {
			continue
		}
		if !headerKnown {
			if containsAlpha(fields) {
				headerKnown = true
				for i, name := range fields {
					header[trim(strings.ToLower(name))] = i
				}
				continue
			}
		}

		d, skip, err := parseDelta(fields, header, headerKnown)
		if err != nil {
			fmt.Fprintf(os.Stderr, "parse error: %v\n", err)
			os.Exit(1)
		}
		if skip {
			continue
		}
		if err := state.apply(d, *every, writer); err != nil {
			fmt.Fprintf(os.Stderr, "fatal: %v\n", err)
			os.Exit(1)
		}
	}

	writer.Flush()
	if err := writer.Error(); err != nil {
		fmt.Fprintf(os.Stderr, "flush error: %v\n", err)
		os.Exit(1)
	}
}
