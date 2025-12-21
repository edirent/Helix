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
	"time"

	"nhooyr.io/websocket"
)

const (
	pingInterval = 10 * time.Second
	readTimeout  = 15 * time.Second
	maxSilence   = 5 * time.Second
	backoffBase  = 250 * time.Millisecond
	backoffMax   = 8 * time.Second
)

type tradeMsg struct {
	Topic string `json:"topic"`
	Type  string `json:"type"`
	Ts    int64  `json:"ts"`
	Data  []struct {
		Ts     int64  `json:"T"`
		Symbol string `json:"s"`
		Side   string `json:"S"`
		Price  string `json:"p"`
		Size   string `json:"v"`
		ID     string `json:"i"`
	} `json:"data"`
}

func main() {
	symbol := flag.String("symbol", "BTCUSDT", "Bybit symbol, e.g. BTCUSDT")
	endpoint := flag.String("endpoint", "wss://stream.bybit.com/v5/public/linear", "Bybit public websocket endpoint")
	out := flag.String("out", "data/replay/bybit_trades.csv", "CSV file to write trades (ts_ms,side,price,size,trade_id)")
	duration := flag.Duration("duration", time.Minute, "How long to record before exiting")
	flag.Parse()

	debug := os.Getenv("DEBUG_TRADE_RECORDER") != ""

	rootCtx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	start := time.Now()
	end := start.Add(*duration)

	if err := os.MkdirAll(filepath.Dir(*out), 0o755); err != nil {
		log.Fatalf("mkdir output: %v", err)
	}
	f, err := os.Create(*out)
	if err != nil {
		log.Fatalf("open out: %v", err)
	}
	defer f.Close()

	bw := bufio.NewWriterSize(f, 1<<20)
	w := csv.NewWriter(bw)
	if err := w.Write([]string{"ts_ms", "side", "price", "size", "trade_id"}); err != nil {
		log.Fatalf("csv header: %v", err)
	}

	backoff := backoffBase
	total := 0
	attempt := 0
	for time.Now().Before(end) {
		ctx, cancel := context.WithDeadline(rootCtx, end)
		conn, _, err := websocket.Dial(ctx, *endpoint, nil)
		if err != nil {
			cancel()
			log.Printf("dial error, retrying: %v", err)
			sleepBackoff(&backoff)
			continue
		}
		backoff = backoffBase
		attempt = 0

		if err := subscribe(ctx, conn, *symbol); err != nil {
			cancel()
			conn.Close(websocket.StatusInternalError, "subscribe failed")
			log.Printf("subscribe error, retrying: %v", err)
			sleepBackoff(&backoff)
			continue
		}
		log.Printf("recording trades for %s (%s) until %s", *symbol, *endpoint, end.Format(time.RFC3339))

		n, err := readLoop(ctx, conn, w, end, debug)
		total += n
		cancel()
		if err != nil && ctx.Err() == nil {
			conn.Close(websocket.StatusGoingAway, "read error")
			log.Printf("read error, reconnecting: %v", err)
			attempt++
			sleepBackoff(&backoff)
			continue
		}
		conn.Close(websocket.StatusNormalClosure, "done")
	}

	w.Flush()
	bw.Flush()
	log.Printf("recorded trades=%d, out=%s", total, *out)
}

func subscribe(ctx context.Context, c *websocket.Conn, symbol string) error {
	sub := map[string]any{"op": "subscribe", "args": []string{"publicTrade." + symbol}}
	payload, _ := json.Marshal(sub)
	return c.Write(ctx, websocket.MessageText, payload)
}

func readLoop(ctx context.Context, c *websocket.Conn, w *csv.Writer, end time.Time, debug bool) (int, error) {
	ping := time.NewTicker(pingInterval)
	defer ping.Stop()

	n := 0
	msgs := 0
	lastData := time.Now()
	for {
		if time.Now().After(end) {
			w.Flush()
			return n, context.DeadlineExceeded
		}
		select {
		case <-ctx.Done():
			w.Flush()
			return n, ctx.Err()
		case <-ping.C:
			_ = c.Ping(ctx)
		default:
		}
		if time.Since(lastData) > maxSilence {
			return n, fmt.Errorf("stale connection (no trades for %v)", time.Since(lastData).Truncate(time.Millisecond))
		}
		readCtx, cancel := context.WithTimeout(ctx, readTimeout)
		_, data, err := c.Read(readCtx)
		cancel()
		if err != nil {
			return n, err
		}
		var msg tradeMsg
		if err := json.Unmarshal(data, &msg); err != nil {
			continue
		}
		if len(msg.Data) == 0 {
			continue
		}
		lastData = time.Now()
		msgs++
		for _, t := range msg.Data {
			rec := []string{
				strconv.FormatInt(t.Ts, 10),
				t.Side,
				t.Price,
				t.Size,
				t.ID,
			}
			if err := w.Write(rec); err != nil {
				log.Printf("write err: %v", err)
			} else {
				n++
			}
		}
		w.Flush()
		if debug {
			log.Printf("debug: msg=%d trades_total=%d msg_trades=%d last_ts=%d type=%s", msgs, n, len(msg.Data), msg.Ts, msg.Type)
		}
	}
}

func sleepBackoff(backoff *time.Duration) {
	jitter := time.Duration(rand.Int63n(int64(*backoff / 2)))
	time.Sleep(*backoff + jitter)
	next := *backoff * 2
	if next > backoffMax {
		next = backoffMax
	}
	*backoff = next
}
