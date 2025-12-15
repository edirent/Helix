package main

import (
	"bufio"
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
	progVersion = "bybit_recorder/1.1"

	// Reliability knobs
	readTimeout  = 30 * time.Second
	pingInterval = 15 * time.Second
	pingTimeout  = 5 * time.Second

	// Reconnect backoff
	backoffBase = 250 * time.Millisecond
	backoffMax  = 8 * time.Second

	// Writer performance knobs
	rowChanSize     = 8192
	bufioSize       = 1 << 20 // 1MB
	flushEveryN     = 200
	flushEveryDur   = 500 * time.Millisecond
	closeReasonDone = "done"
	closeReasonRetry = "reconnect"
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

// 传给 writer 的最小数据结构：全部用原始 string，避免 float/format 成本
type csvRow struct {
	tsMs   int64
	bidPx  string
	askPx  string
	bidQty string
	askQty string
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

	runCtx, cancel := context.WithDeadline(rootCtx, endWall)
	defer cancel()

	// Ensure output dir exists
	outDir := filepath.Dir(*out)
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		log.Fatalf("mkdir output dir: %v", err)
	}

	// Prepare meta sidecar path + write meta once
	metaPath := sidecarMetaPath(*out)
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

	// Channel: reader -> writer
	rowCh := make(chan csvRow, rowChanSize)

	// Start writer goroutine
	var rowsWritten uint64
	writerDone := make(chan struct{})
	go func() {
		defer close(writerDone)
		n := writerLoop(runCtx, f, rowCh)
		atomic.StoreUint64(&rowsWritten, n)
	}()

	log.Printf("recording %s (%s), depth=%d, out=%s",
		*symbol, *endpoint, *depth, *out)

	// Start reader loop (handles reconnect + subscribe)
	readLoop(runCtx, *endpoint, topic, rowCh)

	// Reader is done => close channel so writer can drain and exit
	close(rowCh)
	<-writerDone

	elapsed := time.Since(startWall).Truncate(time.Second)
	log.Printf("recorded %s, rows=%d, csv=%s, meta=%s",
		elapsed, atomic.LoadUint64(&rowsWritten), *out, metaPath)
}

// 读/解析 + 重连：只做网络和 JSON，写盘完全交给 writer
func readLoop(ctx context.Context, endpoint, topic string, out chan<- csvRow) {
	rng := rand.New(rand.NewSource(time.Now().UnixNano()))
	attempt := 0

	for {
		if ctx.Err() != nil {
			return
		}

		conn, err := dialAndSubscribe(ctx, endpoint, topic, attempt, rng)
		if err != nil {
			if ctx.Err() != nil {
				return
			}
			attempt++
			continue
		}
		attempt = 0

		// Heartbeat ping loop
		pingCtx, pingCancel := context.WithCancel(ctx)
		go pingLoop(pingCtx, conn)

		for {
			if ctx.Err() != nil {
				pingCancel()
				_ = conn.Close(websocket.StatusNormalClosure, closeReasonDone)
				return
			}

			readCtx, cancel := context.WithTimeout(ctx, readTimeout)
			_, data, err := conn.Read(readCtx)
			cancel()

			if err != nil {
				// reconnect
				pingCancel()
				_ = conn.Close(websocket.StatusNormalClosure, closeReasonRetry)
				break
			}

			var msg orderbookMsg
			if err := json.Unmarshal(data, &msg); err != nil {
				continue
			}
			if len(msg.Data.Bids) == 0 || len(msg.Data.Asks) == 0 {
				continue
			}
			// top-of-book requires [price, size]
			if len(msg.Data.Bids[0]) < 2 || len(msg.Data.Asks[0]) < 2 {
				continue
			}

			ts := msg.Ts
			if ts == 0 {
				ts = time.Now().UnixNano() / int64(time.Millisecond)
			}

			row := csvRow{
				tsMs:   ts,
				bidPx:  msg.Data.Bids[0][0], // 原始字符串
				bidQty: msg.Data.Bids[0][1],
				askPx:  msg.Data.Asks[0][0],
				askQty: msg.Data.Asks[0][1],
			}

			// 如果 writer 来不及写：这里会 backpressure（不会悄悄丢数据）
			select {
			case out <- row:
			case <-ctx.Done():
				pingCancel()
				_ = conn.Close(websocket.StatusNormalClosure, closeReasonDone)
				return
			}
		}
	}
}

// writer：只负责写盘 + 批量 flush
func writerLoop(ctx context.Context, f *os.File, rows <-chan csvRow) uint64 {
	bw := bufio.NewWriterSize(f, bufioSize)
	defer bw.Flush()

	w := csv.NewWriter(bw)
	defer w.Flush()

	if err := w.Write([]string{"ts_ms", "best_bid", "best_ask", "bid_size", "ask_size"}); err != nil {
		log.Fatalf("write header: %v", err)
	}
	w.Flush()
	if err := w.Error(); err != nil {
		log.Fatalf("flush header: %v", err)
	}

	ticker := time.NewTicker(flushEveryDur)
	defer ticker.Stop()

	var n uint64
	sinceFlush := 0

	// 复用 slice，避免每行分配 []string
	rec := make([]string, 5)

	flush := func() {
		w.Flush()
		if err := w.Error(); err != nil {
			log.Fatalf("flush csv: %v", err)
		}
		// bufio flush 由 w.Flush() 触发写入到 bw；最后再 bw.Flush() 确保落盘
		if err := bw.Flush(); err != nil {
			log.Fatalf("flush bufio: %v", err)
		}
		sinceFlush = 0
	}

	for {
		select {
		case <-ctx.Done():
			// drain? 这里不 drain，退出由 rowCh close + writerDone 控制
			flush()
			return n
		case <-ticker.C:
			if sinceFlush > 0 {
				flush()
			}
		case row, ok := <-rows:
			if !ok {
				flush()
				return n
			}

			rec[0] = strconv.FormatInt(row.tsMs, 10)
			rec[1] = row.bidPx
			rec[2] = row.askPx
			rec[3] = row.bidQty
			rec[4] = row.askQty

			if err := w.Write(rec); err != nil {
				log.Fatalf("write row: %v", err)
			}

			n++
			sinceFlush++
			if sinceFlush >= flushEveryN {
				flush()
			}
		}
	}
}

func dialAndSubscribe(ctx context.Context, endpoint, topic string, attempt int, rng *rand.Rand) (*websocket.Conn, error) {
	if attempt > 0 {
		delay := computeBackoff(attempt, rng)
		timer := time.NewTimer(delay)
		defer timer.Stop()
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-timer.C:
		}
	}

	dialCtx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()

	conn, _, err := websocket.Dial(dialCtx, endpoint, nil)
	if err != nil {
		return nil, fmt.Errorf("dial: %w", err)
	}

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
				return
			}
		}
	}
}

func computeBackoff(attempt int, rng *rand.Rand) time.Duration {
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
	return os.WriteFile(path, b, 0o644)
}
