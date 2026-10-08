// loadtest: bot players against a minicraft-server, measuring what players feel.
//
//	go run ./cmd/loadtest --url ws://localhost:7777 --bots 32 --ramp 30s --duration 3m
//	go run ./cmd/loadtest --mode ramp --bots 40 --step 4 --every 15s
//	go run ./cmd/loadtest --mode soak --bots 16 --duration 30m --join-probe-every 1m
//
// See loadtest/README.md.
package main

import (
	"context"
	"flag"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/AdnaneBJA/Minicraft/loadtest/report"
	"github.com/AdnaneBJA/Minicraft/loadtest/run"
)

func main() {
	var o run.Options
	var serverPID int
	var out string
	var failOnP99 time.Duration
	var failOnErrors bool
	flag.StringVar(&o.URL, "url", "ws://localhost:7777", "the server's WebSocket URL")
	flag.IntVar(&o.Bots, "bots", 8, "bots to run (ramp: the most to try)")
	flag.DurationVar(&o.Ramp, "ramp", 10*time.Second, "steady/soak: time taken to add all the bots")
	flag.DurationVar(&o.Duration, "duration", time.Minute, "steady/soak: how long to hold once all bots are in")
	flag.StringVar(&o.Mode, "mode", "steady", "steady, ramp or soak")
	flag.IntVar(&o.Step, "step", 4, "ramp: bots added per step")
	flag.DurationVar(&o.Every, "every", 15*time.Second, "ramp: time per step")
	flag.DurationVar(&o.MaxP99, "max-p99", 150*time.Millisecond, "ramp: p99 input latency that counts as breaking")
	flag.DurationVar(&o.ProbeEvery, "join-probe-every", time.Minute, "soak: how often a fresh player joins, to time it")
	flag.IntVar(&serverPID, "server-pid", 0, "the server's process id, to sample its CPU and memory (local runs)")
	flag.Int64Var(&o.Seed, "seed", 1, "seed for the bots' behaviour")
	flag.BoolVar(&o.AllowRemote, "i-know-this-is-not-local", false, "allow a target that isn't local or private")
	flag.StringVar(&out, "out", "report", "report files: <out>.json and <out>.md")
	flag.DurationVar(&o.JoinTimeout, "join-timeout", 10*time.Second, "a join taking longer counts as failed")
	flag.DurationVar(&failOnP99, "fail-on-p99", 0, "exit 1 if p99 input latency reaches this (CI)")
	flag.BoolVar(&failOnErrors, "fail-on-errors", false,
		"exit 1 on any refused connection, join, drop or overtaken input (CI)")
	flag.Parse()
	o.ServerPID = int32(serverPID)

	// Ctrl+C stops the bots and still writes the report.
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	fmt.Printf("loadtest: %d bots, %s mode, against %s\n", o.Bots, o.Mode, o.URL)
	result, err := run.Run(ctx, o, os.Stdout)
	if err != nil {
		fmt.Fprintln(os.Stderr, "loadtest:", err)
		os.Exit(2)
	}
	if err := report.WriteFiles(result, out); err != nil {
		fmt.Fprintln(os.Stderr, "loadtest: writing the report:", err)
		os.Exit(2)
	}
	fmt.Println()
	fmt.Print(report.Markdown(result))
	fmt.Printf("Report written to %s.md and %s.json\n", out, out)
	if err := report.Check(result, failOnP99, failOnErrors); err != nil {
		fmt.Fprintln(os.Stderr, "loadtest: FAILED:", err)
		os.Exit(1)
	}
}
