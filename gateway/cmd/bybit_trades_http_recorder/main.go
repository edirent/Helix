package main

import (
	"bufio"
	"context"
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"time"
)

// Minimal HTTP recorder for Bybit recent trades. Polls the public REST API
// periodically, dedups by execId, and writes a deterministic CSV suitable for
// Gate9 maker testing when websocket capture is unreliable.

const (
	defaultEndpoint = "https://api.bybit.com/v5/market/recent-trade"
)

type trade struct {
	ExecID string `json:"execId"`
	Price  string `json:"price"`
	Size   string `json:"size"`
	Side   string `json:"side"`
	Time   string `json:"time"` // ms since epoch, string
	Seq    string `json:"seq"`
}

type tradeResult struct {
	List []trade `json:"list"`
}

type tradeResponse struct {
	RetCode int         `json:"retCode"`
	RetMsg  string      `json:"retMsg"`
	Result  tradeResult `json:"result"`
	Time    int64       `json:"time"`
}

func main() {
	symbol := flag.String("symbol", "BTCUSDT", "Bybit symbol, e.g. BTCUSDT")
	category := flag.String("category", "linear", "Bybit category (linear, spot, inverse)")
	out := flag.String("out", "data/replay/btc_trades.csv", "Output CSV path")
	duration := flag.Duration("duration", 10*time.Minute, "How long to record before exiting")
	interval := flag.Duration("interval", 250*time.Millisecond, "Polling interval")
	endpoint := flag.String("endpoint", defaultEndpoint, "Bybit recent-trade endpoint")
	flag.Parse()

	start := time.Now()
	end := start.Add(*duration)
	startMs := start.UnixNano() / int64(time.Millisecond)
	endMs := end.UnixNano() / int64(time.Millisecond)

	if err := os.MkdirAll(filepath.Dir(*out), 0o755); err != nil {
		log.Fatalf("mkdir output: %v", err)
	}
	f, err := os.Create(*out)
	if err != nil {
		log.Fatalf("open output: %v", err)
	}
	defer f.Close()

	bw := bufio.NewWriterSize(f, 1<<20)
	w := csv.NewWriter(bw)
	if err := w.Write([]string{"ts_ms", "side", "price", "size", "exec_id", "seq", "recv_ts_ms"}); err != nil {
		log.Fatalf("write header: %v", err)
	}

	client := &http.Client{Timeout: 8 * time.Second}

	seen := make(map[string]struct{}, 1<<15)
	total := 0
	dups := 0
	droppedBefore := 0
	droppedAfter := 0
	polls := 0

	for time.Now().Before(end) {
		now := time.Now()
		polls++
		n, dup, db, da, err := pollOnce(client, *endpoint, *category, *symbol, w, seen, startMs, endMs)
		if err != nil {
			log.Printf("poll error: %v", err)
		}
		total += n
		dups += dup
		droppedBefore += db
		droppedAfter += da
		w.Flush()
		bw.Flush()

		sleep := *interval - time.Since(now)
		if sleep > 0 {
			time.Sleep(sleep)
		}
	}

	log.Printf("recorded trades unique=%d dups=%d dropped_before=%d dropped_after=%d polls=%d window_ms=[%d,%d] out=%s",
		total, dups, droppedBefore, droppedAfter, polls, startMs, endMs, *out)
}

func pollOnce(client *http.Client, endpoint, category, symbol string, w *csv.Writer, seen map[string]struct{}, startMs, endMs int64) (int, int, int, int, error) {
	req, err := http.NewRequestWithContext(context.Background(), http.MethodGet, endpoint, nil)
	if err != nil {
		return 0, 0, 0, 0, err
	}
	q := req.URL.Query()
	q.Set("category", category)
	q.Set("symbol", symbol)
	q.Set("limit", "1000")
	req.URL.RawQuery = q.Encode()

	resp, err := client.Do(req)
	if err != nil {
		return 0, 0, 0, 0, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return 0, 0, 0, 0, fmt.Errorf("status %s", resp.Status)
	}

	var body tradeResponse
	if err := json.NewDecoder(resp.Body).Decode(&body); err != nil {
		return 0, 0, 0, 0, err
	}
	if body.RetCode != 0 {
		return 0, 0, 0, 0, fmt.Errorf("retCode %d retMsg %s", body.RetCode, body.RetMsg)
	}

	nowMs := time.Now().UnixNano() / int64(time.Millisecond)
	newCount := 0
	dupCount := 0
	dropBefore := 0
	dropAfter := 0
	for _, t := range body.Result.List {
		if _, ok := seen[t.ExecID]; ok {
			dupCount++
			continue
		}
		seen[t.ExecID] = struct{}{}
		tsMs, err := strconv.ParseInt(t.Time, 10, 64)
		if err != nil {
			// Skip malformed rows; this should not happen on Bybit
			continue
		}
		if tsMs < startMs {
			dropBefore++
			continue
		}
		if tsMs > endMs {
			dropAfter++
			continue
		}
		rec := []string{
			strconv.FormatInt(tsMs, 10),
			t.Side,
			t.Price,
			t.Size,
			t.ExecID,
			t.Seq,
			strconv.FormatInt(nowMs, 10),
		}
		if err := w.Write(rec); err != nil {
			return newCount, dupCount, dropBefore, dropAfter, err
		}
		newCount++
	}
	return newCount, dupCount, dropBefore, dropAfter, nil
}
