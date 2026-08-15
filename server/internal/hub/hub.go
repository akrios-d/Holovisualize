// Package hub manages the map of active sessions.
package hub

import (
	"sync"

	"holovisualize/server/internal/session"

	"github.com/gorilla/websocket"
)

// Hub holds all active holographic sessions keyed by session ID.
type Hub struct {
	mu       sync.Mutex
	sessions map[string]*session.Session
}

func New() *Hub {
	return &Hub{sessions: make(map[string]*session.Session)}
}

func (h *Hub) getOrCreate(key string) *session.Session {
	h.mu.Lock()
	defer h.mu.Unlock()
	if s, ok := h.sessions[key]; ok {
		return s
	}
	s := session.New()
	h.sessions[key] = s
	return s
}

// JoinAsProducer registers a sensor in the session. Blocks until disconnect.
func (h *Hub) JoinAsProducer(sessionKey, sensorID string, conn *websocket.Conn) {
	s := h.getOrCreate(sessionKey)
	s.AddProducer(sensorID, conn)
}

// JoinAsConsumer registers a viewer in the session. Blocks until disconnect.
func (h *Hub) JoinAsConsumer(sessionKey string, conn *websocket.Conn) {
	s := h.getOrCreate(sessionKey)
	s.AddConsumer(conn)
}

// SetTransform stores the calibration transform for a sensor.
func (h *Hub) SetTransform(sessionKey, sensorID string, m [16]float32) {
	s := h.getOrCreate(sessionKey)
	s.SetTransform(sensorID, m)
}
