package main

import (
	"bufio"
	"context"
	"encoding/csv"
	"encoding/json"
	"flag"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"time"

	"nhooyr.io/websocket"
)

type tradeMsg struct {
	Topic string `json:"topic"`
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
	endpoint := flag.String("endpoint", "wss://stream.bybit.com/v5/public/spot", "Bybit public websocket endpoint")
	out := flag.String("out", "data/replay/bybit_trades.csv", "CSV file to write trades (ts_ms,side,price,size,trade_id)")
	duration := flag.Duration("duration", time.Minute, "How long to record before exiting")
	flag.Parse()

	rootCtx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	ctx, cancel := context.WithTimeout(rootCtx, *duration)
	defer cancel()

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

	conn, _, err := websocket.Dial(ctx, *endpoint, nil)
	if err != nil {
		log.Fatalf("dial: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "done")

	sub := map[string]any{"op": "subscribe", "args": []string{"publicTrade." + *symbol}}
	payload, _ := json.Marshal(sub)
	if err := conn.Write(ctx, websocket.MessageText, payload); err != nil {
		log.Fatalf("subscribe: %v", err)
	}
	log.Printf("recording trades for %s (%s) until %s", *symbol, *endpoint, time.Now().Add(*duration).Format(time.RFC3339))

	total := 0
	for {
		if ctx.Err() != nil {
			break
		}
		_, data, err := conn.Read(ctx)
		if err != nil {
			if ctx.Err() != nil {
				break
			}
			log.Fatalf("read: %v", err)
		}
		var msg tradeMsg
		if err := json.Unmarshal(data, &msg); err != nil {
			continue
		}
		if len(msg.Data) == 0 {
			continue
		}
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
				total++
			}
		}
		w.Flush()
	}
	w.Flush()
	bw.Flush()
	log.Printf("recorded trades=%d, out=%s", total, *out)
}
