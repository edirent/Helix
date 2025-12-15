package main

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"nhooyr.io/websocket"
)

const (
	progVersion = "bybit_recorder/1.0"

	// Reliability knobs
	readTimeout  = 30 * time.Second // read deadline (no msg for this long => treat as stalled)
	pingInterval = 15 * time.Second // keepalive
	pingTimeout  = 5 * time.Second

	// Reconnect backoff
	backoffBase = 250 * time.Millisecond
	backoffMax  = 8 * time.Second

	// CSV flush policy (not required by A, but avoids data loss without flushing every row)
	flushEveryN       = 200
	flushEveryDur     = 500 * time.Millisecond
	closeReasonNormal = "done"
	closeReasonRetry  = "reconnect"
)

type orderbookMsg struct {
	Topic string `json:"topic"`
	Type  string `json:"type"`
	Ts    int64  `json:"ts"`
	Data  struct {
		Symbol string     `json:"s"`
		Bids   [][]string `json:"b"`
		Asks   [][]string `json:"a"`
	} `json:"data"`
}

type pxLevel struct {
	price float64
	size  float64
}

type metaInfo struct {
	Version    string `json:"version"`
	Symbol     string `json:"symbol"`
	Endpoint   string `json:"endpoint"`
	Depth      int    `json:"depth"`
	Topic      string `json:"topic"`
	StartTime  string `json:"start_time"`
	OutputCSV  string `json:"output_csv"`
	OutputMeta string `json:"output_meta"`
}

func main() {
	symbol := flag.String("symbol", "BTCUSDT", "Bybit symbol, e.g. BTCUSDT")
	endpoint := flag.String("endpoint", "wss://stream.bybit.com/v5/public/linear", "Bybit public websocket endpoint")
	depth := flag.Int("depth", 1, "Orderbook depth to subscribe (1 or 50)")
	out := flag.String("out", "data/replay/bybit_ws.csv", "CSV file to write top-of-book snapshots")
	duration := flag.Duration("duration", time.Minute, "How long to record before exiting")
	flag.Parse()

	// Ctrl+C support
	rootCtx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	startWall := time.Now()
	endWall := startWall.Add(*duration)

	// Ensure output dir exists
	outDir := filepath.Dir(*out)
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		log.Fatalf("mkdir output dir: %v", err)
	}

	// Prepare meta sidecar path
	metaPath := sidecarMetaPath(*out)

	// Write meta JSON once (start-of-run)
	topic := fmt.Sprintf("orderbook.%d.%s", *depth, *symbol)
	if err := writeMeta(metaPath, metaInfo{
		Version:    progVersion,
		Symbol:     *symbol,
		Endpoint:   *endpoint,
		Depth:      *depth,
		Topic:      topic,
		StartTime:  startWall.Format(time.RFC3339Nano),
		OutputCSV:  *out,
		OutputMeta: metaPath,
	}); err != nil {
		log.Fatalf("write meta: %v", err)
	}
	log.Printf("meta written: %s", metaPath)

	// Open CSV (create/truncate once per run)
	f, err := os.Create(*out)
	if err != nil {
		log.Fatalf("open output csv: %v", err)
	}
	defer f.Close()

	w := csv.NewWriter(f)
	if err := w.Write([]string{"ts_ms", "best_bid", "best_ask", "bid_size", "ask_size"}); err != nil {
		log.Fatalf("write header: %v", err)
	}
	w.Flush()
	if err := w.Error(); err != nil {
		log.Fatalf("flush header: %v", err)
	}

	log.Printf("recording %s (%s), depth=%d, out=%s",
		*symbol, *endpoint, *depth, *out)

	var rows uint64
	lastFlush := time.Now()
	rowsSinceFlush := 0

	// Outer loop: (re)connect until time is up / cancelled
	rng := rand.New(rand.NewSource(time.Now().UnixNano()))
	attempt := 0

	for {
		// stop conditions
		if rootCtx.Err() != nil || time.Now().After(endWall) {
			break
		}

		// dial + subscribe (with backoff)
		conn, err := dialAndSubscribe(rootCtx, *endpoint, topic, attempt, rng)
		if err != nil {
			log.Printf("dial/subscribe failed: %v", err)
			// if context cancelled or time's up, exit
			if rootCtx.Err() != nil || time.Now().After(endWall) {
				break
			}
			attempt++
			continue
		}
		attempt = 0 // reset backoff after a successful connect

		// Heartbeat ping loop
		pingCtx, pingCancel := context.WithCancel(rootCtx)
		go pingLoop(pingCtx, conn)

		// Read loop
		for {
			if rootCtx.Err() != nil || time.Now().After(endWall) {
				pingCancel()
				_ = conn.Close(websocket.StatusNormalClosure, closeReasonNormal)
				break
			}

			// Read deadline via context timeout
			readCtx, cancel := context.WithTimeout(rootCtx, readTimeout)
			_, data, err := conn.Read(readCtx)
			cancel()

			if err != nil {
				// If read timed out or any read error => reconnect
				pingCancel()
				_ = conn.Close(websocket.StatusNormalClosure, closeReasonRetry)

				// Normal closure can still happen; if time remains, we reconnect anyway
				log.Printf("read error (will reconnect): %v", err)
				break
			}

			var msg orderbookMsg
			if err := json.Unmarshal(data, &msg); err != nil {
				continue
			}
			if len(msg.Data.Bids) == 0 || len(msg.Data.Asks) == 0 {
				continue
			}

			bestBid, err1 := parsePx(msg.Data.Bids[0])
			bestAsk, err2 := parsePx(msg.Data.Asks[0])
			if err1 != nil || err2 != nil {
				continue
			}

			ts := msg.Ts
			if ts == 0 {
				ts = time.Now().UnixNano() / int64(time.Millisecond)
			}

			record := []string{
				strconv.FormatInt(ts, 10),
				fmt.Sprintf("%.10f", bestBid.price),
				fmt.Sprintf("%.10f", bestAsk.price),
				fmt.Sprintf("%.6f", bestBid.size),
				fmt.Sprintf("%.6f", bestAsk.size),
			}
			if err := w.Write(record); err != nil {
				// disk/write error is fatal in practice
				log.Fatalf("write row: %v", err)
			}

			atomic.AddUint64(&rows, 1)
			rowsSinceFlush++

			// periodic flush (avoid flushing every row)
			if rowsSinceFlush >= flushEveryN || time.Since(lastFlush) >= flushEveryDur {
				w.Flush()
				if err := w.Error(); err != nil {
					log.Fatalf("flush csv: %v", err)
				}
				lastFlush = time.Now()
				rowsSinceFlush = 0
			}
		}
	}

	// Final flush
	w.Flush()
	if err := w.Error(); err != nil {
		log.Printf("final flush error: %v", err)
	}

	elapsed := time.Since(startWall).Truncate(time.Second)
	log.Printf("recorded %s, rows=%d, csv=%s, meta=%s",
		elapsed, atomic.LoadUint64(&rows), *out, metaPath)
}

func dialAndSubscribe(ctx context.Context, endpoint, topic string, attempt int, rng *rand.Rand) (*websocket.Conn, error) {
	// Backoff if attempt > 0
	if attempt > 0 {
		delay := computeBackoff(attempt, rng)
		log.Printf("reconnect backoff: %s (attempt=%d)", delay, attempt)
		timer := time.NewTimer(delay)
		defer timer.Stop()
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-timer.C:
		}
	}

	// Dial with a short timeout so we don't hang forever
	dialCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()

	conn, _, err := websocket.Dial(dialCtx, endpoint, nil)
	if err != nil {
		return nil, fmt.Errorf("dial: %w", err)
	}

	// Subscribe with timeout
	sub := map[string]any{
		"op":   "subscribe",
		"args": []string{topic},
	}
	payload, _ := json.Marshal(sub)

	writeCtx, wcancel := context.WithTimeout(ctx, 5*time.Second)
	defer wcancel()

	if err := conn.Write(writeCtx, websocket.MessageText, payload); err != nil {
		_ = conn.Close(websocket.StatusNormalClosure, "subscribe failed")
		return nil, fmt.Errorf("subscribe write: %w", err)
	}

	log.Printf("subscribed to %s (%s)", topic, endpoint)
	return conn, nil
}

func pingLoop(ctx context.Context, conn *websocket.Conn) {
	t := time.NewTicker(pingInterval)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			pctx, cancel := context.WithTimeout(ctx, pingTimeout)
			err := conn.Ping(pctx)
			cancel()
			if err != nil {
				// Ping failure will be noticed by read loop soon; we just exit ping goroutine.
				return
			}
		}
	}
}

func computeBackoff(attempt int, rng *rand.Rand) time.Duration {
	// exp backoff with cap + small jitter
	// delay = min(max, base * 2^(attempt-1)) + jitter(0..150ms)
	exp := attempt - 1
	if exp > 10 {
		exp = 10
	}
	delay := backoffBase * time.Duration(1<<exp)
	if delay > backoffMax {
		delay = backoffMax
	}
	jitter := time.Duration(rng.Intn(150)) * time.Millisecond
	return delay + jitter
}

func parsePx(level []string) (pxLevel, error) {
	if len(level) < 2 {
		return pxLevel{}, fmt.Errorf("bad level")
	}
	p, err := strconv.ParseFloat(level[0], 64)
	if err != nil {
		return pxLevel{}, err
	}
	q, err := strconv.ParseFloat(level[1], 64)
	if err != nil {
		return pxLevel{}, err
	}
	return pxLevel{price: p, size: q}, nil
}

func sidecarMetaPath(csvPath string) string {
	dir := filepath.Dir(csvPath)
	base := filepath.Base(csvPath)
	ext := filepath.Ext(base)
	name := strings.TrimSuffix(base, ext)
	return filepath.Join(dir, name+".meta.json")
}

func writeMeta(path string, meta metaInfo) error {
	b, err := json.MarshalIndent(meta, "", "  ")
	if err != nil {
		return err
	}
	// create/truncate
	return os.WriteFile(path, b, 0o644)
}

