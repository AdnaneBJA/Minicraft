package ingest

import (
	"strings"
	"testing"
)

func goodEvent() Event {
	return Event{ID: "7-1-0", Type: "ItemCollected", At: 1700000000000, Player: "Alice", Subject: "Wood", Count: 3, Icon: 0}
}

func TestValidate(t *testing.T) {
	many := make([]Event, MaxEventsPerBatch+1)
	for i := range many {
		many[i] = goodEvent()
	}
	cases := []struct {
		name  string
		batch Batch
		ok    bool
	}{
		{"good", Batch{Online: 2, Events: []Event{goodEvent()}}, true},
		{"empty heartbeat", Batch{Online: 0}, true},
		{"unknown type", Batch{Events: []Event{{ID: "1", Type: "Teleported"}}}, false},
		{"name too long", Batch{Events: []Event{{ID: "1", Type: "ChatSent", Player: "ThisNameIsWayTooLong"}}}, false},
		{"name with spaces", Batch{Events: []Event{{ID: "1", Type: "ChatSent", Player: "bad name"}}}, false},
		{"huge count", Batch{Events: []Event{{ID: "1", Type: "ItemCollected", Player: "A", Subject: "Wood", Count: 2_000_000}}}, false},
		{"negative count", Batch{Events: []Event{{ID: "1", Type: "ItemCollected", Player: "A", Subject: "Wood", Count: -1}}}, false},
		{"bad killer kind", Batch{Events: []Event{{ID: "1", Type: "PlayerKilled", Player: "A", KillerKind: "ghost"}}}, false},
		{"missing id", Batch{Events: []Event{{Type: "ChatSent", Player: "A"}}}, false},
		{"long subject", Batch{Events: []Event{{ID: "1", Type: "TileBroken", Player: "A", Subject: strings.Repeat("x", 33)}}}, false},
		{"bad icon", Batch{Events: []Event{{ID: "1", Type: "ItemCollected", Player: "A", Subject: "Wood", Icon: 999}}}, false},
		{"negative online", Batch{Online: -1}, false},
		{"too many events", Batch{Events: many}, false},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			err := Validate(c.batch)
			if c.ok && err != nil {
				t.Fatalf("want ok, got %v", err)
			}
			if !c.ok && err == nil {
				t.Fatal("want an error")
			}
		})
	}
}
