package protocol

import (
	"bytes"
	"encoding/binary"
	"os"
	"path/filepath"
	"reflect"
	"runtime"
	"testing"
)

func fixture(t testing.TB, name string) []byte {
	t.Helper()
	b, err := os.ReadFile(filepath.Join("testdata", name+".bin"))
	if err != nil {
		t.Fatalf("fixture %s: %v (generate with WRITE_PROTOCOL_FIXTURES=1 server_tests)", name, err)
	}
	return b
}

// The examples the C++ side encoded into testdata (server/tests/protocol_fixtures.cpp).
var (
	exampleTick = TickMsg{Tick: 42, Turns: []Turn{
		{PlayerID: 3, Keys: KeyLeft | KeyAttack, Commands: []Command{{Kind: CommandCraft, A: -1, B: 2}}},
		{PlayerID: 7},
	}}
	exampleHistory = []TickMsg{
		{Tick: 1, Turns: []Turn{{PlayerID: 7, Commands: []Command{{Kind: CommandJoin, Text: "Alice"}}}}},
		{Tick: 2, Turns: []Turn{{PlayerID: 7, Keys: KeyRight}}},
	}
)

func TestFixturesDecode(t *testing.T) {
	cases := []struct {
		name string
		want Message
	}{
		{"hello", Message{Type: Hello, Text: "Alice"}},
		{"input", Message{Type: Input, Keys: KeyRight | KeyUp | KeyAttack | KeyPressed}},
		{"chat", Message{Type: Chat, Text: "hi there"}},
		{"welcome", Message{Type: Welcome, PlayerID: 7}},
		{"joined", Message{Type: Joined, Seed: 99, HistoryLen: 2}},
		{"tick", Message{Type: Tick, Tick: exampleTick}},
		{"chatline", Message{Type: ChatLine, From: "Alice", Text: "hello"}},
		{"error", Message{Type: Error, Text: "Name already in use"}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			got, err := Decode(fixture(t, c.name))
			if err != nil {
				t.Fatal(err)
			}
			if !reflect.DeepEqual(got, c.want) {
				t.Errorf("got %+v\nwant %+v", got, c.want)
			}
		})
	}
	seed, history, err := DecodeJoinedHistory(fixture(t, "joined"))
	if err != nil || seed != 99 || !reflect.DeepEqual(history, exampleHistory) {
		t.Errorf("joined history: %d %+v %v", seed, history, err)
	}
}

func TestFixturesReencode(t *testing.T) {
	cases := map[string][]byte{
		"hello":    EncodeHello("Alice"),
		"input":    EncodeInput(KeyRight | KeyUp | KeyAttack | KeyPressed),
		"chat":     EncodeChat("hi there"),
		"welcome":  EncodeWelcome(7),
		"joined":   EncodeJoined(99, exampleHistory),
		"tick":     EncodeTick(exampleTick),
		"chatline": EncodeChatLine("Alice", "hello"),
		"error":    EncodeError("Name already in use"),
	}
	for name, got := range cases {
		if want := fixture(t, name); !bytes.Equal(got, want) {
			t.Errorf("%s: encoded % x\nC++ wrote  % x", name, got, want)
		}
	}
}

func le32(v int32) []byte { return binary.LittleEndian.AppendUint32(nil, uint32(v)) }

func cat(parts ...[]byte) []byte { return bytes.Join(parts, nil) }

func TestDecodeRejectsMalformed(t *testing.T) {
	tickHeader := func(turns int32) []byte { return cat([]byte{byte(Tick)}, le32(1), le32(turns)) }
	cases := map[string][]byte{
		"empty":               {},
		"unknown type":        {10},
		"truncated i32":       {byte(Welcome), 1, 2},
		"negative string":     cat([]byte{byte(Hello)}, le32(-1)),
		"name too long":       cat([]byte{byte(Hello)}, le32(13), []byte("abcdefghijklm")),
		"too many turns":      tickHeader(65),
		"too many commands":   cat(tickHeader(1), le32(3), []byte{0}, le32(19)),
		"trailing byte":       cat(EncodeWelcome(1), []byte{0}),
		"unknown command":     cat(tickHeader(1), le32(3), []byte{0}, le32(1), []byte{9}),
		"negative history":    cat([]byte{byte(Joined)}, le32(1), le32(-1)),
		"string past the end": cat([]byte{byte(Chat)}, le32(50), []byte("short")),
	}
	for name, b := range cases {
		t.Run(name, func(t *testing.T) {
			if _, err := Decode(b); err == nil {
				t.Error("want an error")
			}
		})
	}
}

func TestDecodeJoinedLargeHistory(t *testing.T) {
	history := make([]TickMsg, 100_000)
	for i := range history {
		history[i] = TickMsg{Tick: int32(i + 1), Turns: []Turn{{PlayerID: 1, Keys: KeyRight}, {PlayerID: 2, Keys: KeyAttack}}}
	}
	encoded := EncodeJoined(5, history)
	history = nil
	runtime.GC()
	var before, after runtime.MemStats
	runtime.ReadMemStats(&before)
	msg, err := Decode(encoded)
	runtime.ReadMemStats(&after)
	if err != nil || msg.HistoryLen != 100_000 || msg.Seed != 5 {
		t.Fatalf("decode: %+v %v", msg, err)
	}
	if grew := after.TotalAlloc - before.TotalAlloc; grew > 10<<20 {
		t.Errorf("decoding the history allocated %d bytes; it should skip the ticks, not keep them", grew)
	}
}

func FuzzDecode(f *testing.F) {
	for _, name := range []string{"hello", "input", "chat", "welcome", "joined", "tick", "chatline", "error"} {
		f.Add(fixture(f, name))
	}
	f.Fuzz(func(t *testing.T, b []byte) {
		_, _ = Decode(b) // must not panic
	})
}
