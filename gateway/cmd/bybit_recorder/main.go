package main

import (
	"context"
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strconv"
	"time"

	"nhooyr.io/websocket"
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

func main() {
	symbol := flag.String("symbol", "BTCUSDT", "Bybit symbol, e.g. BTCUSDT")
	endpoint := flag.String("endpoint", "wss://stream.bybit.com/v5/public/linear", "Bybit public websocket endpoint")
	depth := flag.Int("depth", 1, "Orderbook depth to subscribe (1 or 50)")
	out := flag.String("out", "data/replay/bybit_ws.csv", "CSV file to write top-of-book snapshots")
	duration := flag.Duration("duration", time.Minute, "How long to record before exiting")
	flag.Parse()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()

	ctx, cancel := context.WithTimeout(ctx, *duration+5*time.Second)
	defer cancel()

	conn, _, err := websocket.Dial(ctx, *endpoint, nil)
	if err != nil {
		log.Fatalf("dial bybit websocket: %v", err)
	}
	defer conn.Close(websocket.StatusNormalClosure, "done")

	sub := map[string]any{
		"op":   "subscribe",
		"args": []string{fmt.Sprintf("orderbook.%d.%s", *depth, *symbol)},
	}
	subPayload, _ := json.Marshal(sub)
	if err := conn.Write(ctx, websocket.MessageText, subPayload); err != nil {
		log.Fatalf("subscribe: %v", err)
	}
	log.Printf("subscribed to %s (%s), writing to %s", sub["args"], *endpoint, *out)

	file, err := os.Create(*out)
	if err != nil {
		log.Fatalf("open output: %v", err)
	}
	defer file.Close()
	writer := csv.NewWriter(file)
	if err := writer.Write([]string{"ts_ms", "best_bid", "best_ask", "bid_size", "ask_size"}); err != nil {
		log.Fatalf("write header: %v", err)
	}
	writer.Flush()

	start := time.Now()
	for {
		if time.Since(start) > *duration {
			break
		}
		_, data, err := conn.Read(ctx)
		if err != nil {
			if websocket.CloseStatus(err) == websocket.StatusNormalClosure {
				break
			}
			log.Printf("read error: %v", err)
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
		if err := writer.Write(record); err != nil {
			log.Printf("write row: %v", err)
		}
		writer.Flush()
	}
	log.Printf("recorded %s of orderbook data to %s", time.Since(start).Truncate(time.Second), *out)
}

type pxLevel struct {
	price float64
	size  float64
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
