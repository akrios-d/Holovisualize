package hub

import (
	"log"
	"sync"

	"github.com/gorilla/websocket"
)

const sendBufferSize = 4 // frames buffered per consumer before dropping

// consumer wraps a WebSocket connection with a dedicated write goroutine.
// gorilla/websocket does not allow concurrent writes, so all writes go
// through the send channel and are serialised in writePump.
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
			log.Println("write error:", err)
			return
		}
	}
}

func (c *consumer) close() {
	close(c.send)
}

// session holds one optional producer and N consumers.
type session struct {
	mu        sync.RWMutex
	consumers []*consumer
}

func (s *session) addConsumer(c *consumer) {
	s.mu.Lock()
	s.consumers = append(s.consumers, c)
	s.mu.Unlock()
}

func (s *session) removeConsumer(c *consumer) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for i, existing := range s.consumers {
		if existing == c {
			s.consumers = append(s.consumers[:i], s.consumers[i+1:]...)
			return
		}
	}
}

func (s *session) broadcast(data []byte) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	for _, c := range s.consumers {
		select {
		case c.send <- data:
		default:
			// Consumer is too slow — drop frame rather than block the producer.
			log.Println("consumer too slow, dropping frame")
		}
	}
}

// Hub manages all active sessions.
type Hub struct {
	mu       sync.RWMutex
	sessions map[string]*session
}

func New() *Hub {
	return &Hub{sessions: make(map[string]*session)}
}

func (h *Hub) getOrCreate(key string) *session {
	h.mu.Lock()
	defer h.mu.Unlock()
	if s, ok := h.sessions[key]; ok {
		return s
	}
	s := &session{}
	h.sessions[key] = s
	return s
}

// AddConsumer registers a viewer for the given session key.
// It blocks until the WebSocket connection closes.
func (h *Hub) AddConsumer(key string, conn *websocket.Conn) {
	s := h.getOrCreate(key)
	c := newConsumer(conn)
	s.addConsumer(c)
	defer func() {
		s.removeConsumer(c)
		c.close()
	}()

	// Read and discard — we only need to detect disconnection.
	for {
		if _, _, err := conn.ReadMessage(); err != nil {
			break
		}
	}
}

// Broadcast forwards a binary frame to all consumers of the given session.
func (h *Hub) Broadcast(key string, data []byte) {
	h.mu.RLock()
	s, ok := h.sessions[key]
	h.mu.RUnlock()
	if !ok {
		return
	}
	s.broadcast(data)
}
