// Package session manages one holographic session:
// N producers (sensors) → merge → M consumers (viewers), broadcast at 30 fps.
package session

import (
	"log"
	"sync"
	"time"

	"holovisualize/server/internal/frame"

	"github.com/gorilla/websocket"
)

const (
	fps             = 30
	sendBufferSize  = 2 // frames queued per consumer before dropping
)

// identity is a 4x4 row-major identity matrix.
var identity = [16]float32{
	1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 1, 0,
	0, 0, 0, 1,
}

// applyTransform applies a 4x4 row-major matrix to a point (translation + rotation).
func applyTransform(p frame.Point, m [16]float32) frame.Point {
	return frame.Point{
		X: m[0]*p.X + m[1]*p.Y + m[2]*p.Z + m[3],
		Y: m[4]*p.X + m[5]*p.Y + m[6]*p.Z + m[7],
		Z: m[8]*p.X + m[9]*p.Y + m[10]*p.Z + m[11],
		R: p.R, G: p.G, B: p.B,
	}
}

// producer holds the latest decoded point cloud from one sensor.
type producer struct {
	mu        sync.Mutex
	transform [16]float32
	latest    []frame.Point
}

func (p *producer) update(points []frame.Point) {
	p.mu.Lock()
	p.latest = points
	p.mu.Unlock()
}

func (p *producer) snapshot() []frame.Point {
	p.mu.Lock()
	defer p.mu.Unlock()
	if p.latest == nil {
		return nil
	}
	cp := make([]frame.Point, len(p.latest))
	copy(cp, p.latest)
	return cp
}

// consumer wraps a WebSocket with a dedicated write goroutine.
type consumer struct {
	conn *websocket.Conn
	send chan []byte
}

func newConsumer(conn *websocket.Conn) *consumer {
	c := &consumer{conn: conn, send: make(chan []byte, sendBufferSize)}
	go c.writePump()
	return c
}

func (c *consumer) writePump() {
	defer c.conn.Close()
	for data := range c.send {
		if err := c.conn.WriteMessage(websocket.BinaryMessage, data); err != nil {
			return
		}
	}
}

func (c *consumer) close() { close(c.send) }

// Session is one holographic call (N sensors → M viewers).
type Session struct {
	mu        sync.RWMutex
	producers map[string]*producer
	consumers []*consumer
	done      chan struct{}
}

func New() *Session {
	s := &Session{
		producers: make(map[string]*producer),
		done:      make(chan struct{}),
	}
	go s.broadcastLoop()
	return s
}

// broadcastLoop merges all sensor frames and broadcasts at a fixed rate.
func (s *Session) broadcastLoop() {
	ticker := time.NewTicker(time.Second / fps)
	defer ticker.Stop()

	for {
		select {
		case <-s.done:
			return
		case <-ticker.C:
			s.mergeAndBroadcast()
		}
	}
}

func (s *Session) mergeAndBroadcast() {
	s.mu.RLock()
	producers := make([]*producer, 0, len(s.producers))
	for _, p := range s.producers {
		producers = append(producers, p)
	}
	consumers := make([]*consumer, len(s.consumers))
	copy(consumers, s.consumers)
	s.mu.RUnlock()

	if len(consumers) == 0 {
		return // nobody watching, skip merge
	}

	// Merge: transform each sensor's latest cloud into world space, concatenate.
	merged := make([]frame.Point, 0)
	for _, p := range producers {
		points := p.snapshot()
		for _, pt := range points {
			merged = append(merged, applyTransform(pt, p.transform))
		}
	}

	if len(merged) == 0 {
		return
	}

	encoded := frame.Encode(merged)

	for _, c := range consumers {
		select {
		case c.send <- encoded:
		default:
			log.Println("session: consumer too slow, dropping frame")
		}
	}
}

// AddProducer registers a sensor. Blocks until the WebSocket closes.
func (s *Session) AddProducer(sensorID string, conn *websocket.Conn) {
	p := &producer{transform: identity}

	s.mu.Lock()
	s.producers[sensorID] = p
	s.mu.Unlock()

	log.Printf("session: producer %q connected\n", sensorID)

	for {
		msgType, data, err := conn.ReadMessage()
		if err != nil {
			break
		}
		if msgType != websocket.BinaryMessage {
			continue
		}
		points, err := frame.Decode(data)
		if err != nil {
			log.Printf("session: bad frame from %q: %v\n", sensorID, err)
			continue
		}
		p.update(points)
	}

	s.mu.Lock()
	delete(s.producers, sensorID)
	s.mu.Unlock()

	log.Printf("session: producer %q disconnected\n", sensorID)
}

// SetTransform sets the calibration transform for a sensor (row-major 4x4).
// Call this after AddProducer registers the sensor.
func (s *Session) SetTransform(sensorID string, m [16]float32) {
	s.mu.RLock()
	p, ok := s.producers[sensorID]
	s.mu.RUnlock()
	if ok {
		p.mu.Lock()
		p.transform = m
		p.mu.Unlock()
	}
}

// AddConsumer registers a viewer. Blocks until the WebSocket closes.
func (s *Session) AddConsumer(conn *websocket.Conn) {
	c := newConsumer(conn)

	s.mu.Lock()
	s.consumers = append(s.consumers, c)
	s.mu.Unlock()

	log.Printf("session: consumer connected (%d total)\n", len(s.consumers))

	// Read and discard — only needed to detect disconnection.
	for {
		if _, _, err := conn.ReadMessage(); err != nil {
			break
		}
	}

	s.mu.Lock()
	for i, existing := range s.consumers {
		if existing == c {
			s.consumers = append(s.consumers[:i], s.consumers[i+1:]...)
			break
		}
	}
	s.mu.Unlock()

	c.close()
	log.Printf("session: consumer disconnected (%d remaining)\n", len(s.consumers))
}

func (s *Session) Close() {
	close(s.done)
}
