// Package protocol is the game's wire format in Go, mirroring net-common/protocol.* byte for byte: each message is
// one binary WebSocket message, starting with its type, then little-endian fields (strings as an i32 length and
// the bytes). The fixtures in testdata are written by the C++ side; the tests keep the two in step.
package protocol

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
)

// MsgType is a message's first byte.
type MsgType uint8

// Message types, in the C++ order.
const (
	Hello MsgType = iota // client -> server
	Input
	CommandMessage
	Chat
	StateHash
	Welcome // server -> client
	Joined
	Tick
	ChatLine
	Error
	Observe   // client -> server (monitoring): watch the world without playing
	ProbePing // client -> server (observers only)
	ProbePong // server -> client: the answer, after the next tick
)

// Limits, as in the C++ decoder.
const (
	MaxNameLength     = 12
	MaxChatLength     = 80
	maxChatLineText   = MaxChatLength * 2
	maxErrorLength    = 200
	maxTurnsPerTick   = 64
	maxCommandsInTurn = 18
	maxHistory        = 60 * 60 * 60 * 6
	commandKinds      = 9
)

// Keys are the keys a player holds, packed as on the wire.
type Keys uint8

// The key bits.
const (
	KeyLeft Keys = 1 << iota
	KeyRight
	KeyUp
	KeyDown
	KeyAttack
	KeyPressed // attack went down since the last tick
)

// Command kinds used here (PlayerCommand::Kind in C++).
const (
	CommandJoin  uint8 = 0
	CommandCraft uint8 = 3
)

// Command is a PlayerCommand: something a player did once.
type Command struct {
	Kind       uint8
	A, B, C, D int32
	Text       string
}

// Turn is what one player did during a tick.
type Turn struct {
	PlayerID int32
	Keys     Keys
	Commands []Command
}

// TickMsg is one tick: every player's turn.
type TickMsg struct {
	Tick  int32
	Turns []Turn
}

// Message is any decoded message; the fields its type uses are set.
type Message struct {
	Type       MsgType
	PlayerID   int32   // Welcome
	Seed       uint32  // Joined
	HistoryLen int     // Joined: the ticks are skipped, not kept
	Tick       TickMsg // Tick
	Keys       Keys    // Input
	From       string  // ChatLine
	Text       string  // Hello (the name), Chat, ChatLine, Error
	PingID     int32   // ProbePing, ProbePong
}

// --- Encoding

type writer struct{ b []byte }

func (w *writer) u8(v uint8)   { w.b = append(w.b, v) }
func (w *writer) i32(v int32)  { w.b = binary.LittleEndian.AppendUint32(w.b, uint32(v)) }
func (w *writer) str(s string) { w.i32(int32(len(s))); w.b = append(w.b, s...) }

func (w *writer) tick(t TickMsg) {
	w.i32(t.Tick)
	w.i32(int32(len(t.Turns)))
	for _, turn := range t.Turns {
		w.i32(turn.PlayerID)
		w.u8(uint8(turn.Keys))
		w.i32(int32(len(turn.Commands)))
		for _, c := range turn.Commands {
			w.u8(c.Kind)
			w.i32(c.A)
			w.i32(c.B)
			w.i32(c.C)
			w.i32(c.D)
			w.str(c.Text)
		}
	}
}

func start(t MsgType) *writer { return &writer{b: []byte{byte(t)}} }

// EncodeHello is the first message a client sends: its name.
func EncodeHello(name string) []byte { w := start(Hello); w.str(name); return w.b }

// EncodeInput sends the keys a player holds.
func EncodeInput(k Keys) []byte { w := start(Input); w.u8(uint8(k)); return w.b }

// EncodeChat sends a chat line.
func EncodeChat(text string) []byte { w := start(Chat); w.str(text); return w.b }

// EncodeWelcome gives a client its player id.
func EncodeWelcome(id int32) []byte { w := start(Welcome); w.i32(id); return w.b }

// EncodeJoined sends the world's seed and history.
func EncodeJoined(seed uint32, history []TickMsg) []byte {
	w := start(Joined)
	w.i32(int32(seed))
	w.i32(int32(len(history)))
	for _, t := range history {
		w.tick(t)
	}
	return w.b
}

// EncodeTick sends one tick.
func EncodeTick(t TickMsg) []byte { w := start(Tick); w.tick(t); return w.b }

// EncodeChatLine sends a chat line to players ("" from = the server).
func EncodeChatLine(from, text string) []byte {
	w := start(ChatLine)
	w.str(from)
	w.str(text)
	return w.b
}

// EncodeError sends an error.
func EncodeError(text string) []byte { w := start(Error); w.str(text); return w.b }

// EncodeObserve asks to watch the world without playing (monitoring probes).
func EncodeObserve() []byte { return start(Observe).b }

// EncodeProbePing asks for an answer with the next tick.
func EncodeProbePing(id int32) []byte { w := start(ProbePing); w.i32(id); return w.b }

// EncodeProbePong answers a ping.
func EncodeProbePong(id int32) []byte { w := start(ProbePong); w.i32(id); return w.b }

// --- Decoding

// ErrMalformed: the bytes aren't a valid message (too short, too long, or values out of range).
var ErrMalformed = errors.New("malformed message")

// reader fails once and stays failed, like the C++ ByteReader: later reads return zero values.
type reader struct {
	b   []byte
	pos int
	bad bool
}

func (r *reader) has(n int) bool {
	if r.bad || n < 0 || len(r.b)-r.pos < n {
		r.bad = true
		return false
	}
	return true
}

func (r *reader) u8() uint8 {
	if !r.has(1) {
		return 0
	}
	v := r.b[r.pos]
	r.pos++
	return v
}

func (r *reader) i32() int32 {
	if !r.has(4) {
		return 0
	}
	v := int32(binary.LittleEndian.Uint32(r.b[r.pos:]))
	r.pos += 4
	return v
}

// count reads a list length in [0, max].
func (r *reader) count(max int) int {
	n := r.i32()
	if n < 0 || int(n) > max {
		r.bad = true
		return 0
	}
	return int(n)
}

func (r *reader) str(maxLength int) string {
	n := r.count(maxLength)
	if !r.has(n) {
		return ""
	}
	s := string(r.b[r.pos : r.pos+n])
	r.pos += n
	return s
}

// tick reads one tick; with keep false it only walks over it (a long history isn't kept in memory).
func (r *reader) tick(keep bool) TickMsg {
	var t TickMsg
	t.Tick = r.i32()
	turns := r.count(maxTurnsPerTick)
	for i := 0; i < turns && !r.bad; i++ {
		turn := Turn{PlayerID: r.i32(), Keys: Keys(r.u8())}
		commands := r.count(maxCommandsInTurn)
		for c := 0; c < commands && !r.bad; c++ {
			kind := r.u8()
			if kind >= commandKinds {
				r.bad = true
				break
			}
			cmd := Command{Kind: kind, A: r.i32(), B: r.i32(), C: r.i32(), D: r.i32(), Text: r.str(MaxNameLength)}
			if keep {
				turn.Commands = append(turn.Commands, cmd)
			}
		}
		if keep {
			t.Turns = append(t.Turns, turn)
		}
	}
	return t
}

// Decode reads one message. A Joined message's history is checked and counted but not kept (see
// DecodeJoinedHistory).
func Decode(b []byte) (Message, error) {
	if len(b) == 0 {
		return Message{}, fmt.Errorf("%w: empty", ErrMalformed)
	}
	r := &reader{b: b, pos: 1}
	m := Message{Type: MsgType(b[0])}
	switch m.Type {
	case Hello:
		m.Text = r.str(MaxNameLength)
	case Input:
		m.Keys = Keys(r.u8())
	case Chat:
		m.Text = r.str(MaxChatLength)
	case Welcome:
		m.PlayerID = r.i32()
	case Joined:
		m.Seed = uint32(r.i32())
		m.HistoryLen = r.count(maxHistory)
		for i := 0; i < m.HistoryLen && !r.bad; i++ {
			r.tick(false)
		}
	case Tick:
		m.Tick = r.tick(true)
	case ChatLine:
		m.From = r.str(MaxNameLength)
		m.Text = r.str(maxChatLineText)
	case Error:
		m.Text = r.str(maxErrorLength)
	case Observe:
	case ProbePing, ProbePong:
		m.PingID = r.i32()
	default:
		return Message{}, fmt.Errorf("%w: type %d", ErrMalformed, b[0])
	}
	if r.bad || r.pos != len(b) {
		return Message{}, fmt.Errorf("%w: %v", ErrMalformed, m.Type)
	}
	return m, nil
}

// DecodeJoinedHistory reads a Joined message, keeping its history.
func DecodeJoinedHistory(b []byte) (uint32, []TickMsg, error) {
	if len(b) == 0 || MsgType(b[0]) != Joined {
		return 0, nil, fmt.Errorf("%w: not Joined", ErrMalformed)
	}
	r := &reader{b: b, pos: 1}
	seed := uint32(r.i32())
	n := r.count(maxHistory)
	history := make([]TickMsg, 0, min(n, 1<<16))
	for i := 0; i < n && !r.bad; i++ {
		history = append(history, r.tick(true))
	}
	if r.bad || r.pos != len(b) {
		return 0, nil, fmt.Errorf("%w: Joined", ErrMalformed)
	}
	return seed, history, nil
}

// ReadMessage reads one message from a WebSocket message reader, and how many bytes it was. The world's history
// (Joined) is streamed: its header is read and the rest only counted, so a history of any size never sits in
// memory. Every other message must fit in maxMessage.
func ReadMessage(r io.Reader, maxMessage int64) (Message, int64, error) {
	var header [9]byte // type, then Joined's seed and tick count
	n, err := io.ReadFull(r, header[:1])
	if err != nil {
		return Message{}, 0, err
	}
	if MsgType(header[0]) == Joined {
		if _, err := io.ReadFull(r, header[1:]); err != nil {
			return Message{}, 0, err
		}
		rest, err := io.Copy(io.Discard, r)
		if err != nil {
			return Message{}, 0, err
		}
		count := int32(binary.LittleEndian.Uint32(header[5:]))
		if count < 0 {
			return Message{}, 0, fmt.Errorf("%w: Joined history count %d", ErrMalformed, count)
		}
		return Message{Type: Joined, Seed: binary.LittleEndian.Uint32(header[1:]), HistoryLen: int(count)},
			int64(len(header)) + rest, nil
	}
	body, err := io.ReadAll(io.LimitReader(r, maxMessage))
	if err != nil {
		return Message{}, 0, err
	}
	if extra, _ := io.Copy(io.Discard, r); extra > 0 {
		return Message{}, 0, fmt.Errorf("message over %d bytes", maxMessage)
	}
	data := append(header[:n:n], body...)
	msg, err := Decode(data)
	return msg, int64(len(data)), err
}

func (t MsgType) String() string {
	names := [...]string{"Hello", "Input", "Command", "Chat", "StateHash", "Welcome", "Joined", "Tick", "ChatLine", "Error", "Observe", "ProbePing", "ProbePong"}
	if int(t) < len(names) {
		return names[t]
	}
	return fmt.Sprintf("MsgType(%d)", uint8(t))
}
