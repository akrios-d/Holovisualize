package main

import (
	"encoding/json"
	"io"
	"log"
	"math"
	"net/http"

	"holovisualize/server/internal/hub"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	CheckOrigin: func(r *http.Request) bool { return true },
}

var h = hub.New()

// WebSocket endpoint.
//
// Producers (capture clients):
//   ws://host:8080/ws?session=KEY&role=producer&sensor=ID
//
// Consumers (viewers):
//   ws://host:8080/ws?session=KEY&role=consumer
func wsHandler(w http.ResponseWriter, r *http.Request) {
	q := r.URL.Query()
	sessionKey := q.Get("session")
	role := q.Get("role")

	if sessionKey == "" {
		http.Error(w, "missing ?session", http.StatusBadRequest)
		return
	}
	if role != "producer" && role != "consumer" {
		http.Error(w, "?role must be 'producer' or 'consumer'", http.StatusBadRequest)
		return
	}
	if role == "producer" && q.Get("sensor") == "" {
		http.Error(w, "producers must provide ?sensor=ID", http.StatusBadRequest)
		return
	}

	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Println("upgrade error:", err)
		return
	}
	defer conn.Close()

	if role == "producer" {
		h.JoinAsProducer(sessionKey, q.Get("sensor"), conn)
	} else {
		h.JoinAsConsumer(sessionKey, conn)
	}
}

// Calibration endpoint.
//
// POST /calibrate?session=KEY&sensor=ID
// Body: {"transform": [m00,m01,...,m33]}  — 16 float32, 4x4 row-major
func calibrateHandler(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "POST only", http.StatusMethodNotAllowed)
		return
	}

	q := r.URL.Query()
	sessionKey := q.Get("session")
	sensorID := q.Get("sensor")
	if sessionKey == "" || sensorID == "" {
		http.Error(w, "missing ?session or ?sensor", http.StatusBadRequest)
		return
	}

	body, err := io.ReadAll(io.LimitReader(r.Body, 1024))
	if err != nil {
		http.Error(w, "read error", http.StatusBadRequest)
		return
	}

	var payload struct {
		Transform []float64 `json:"transform"`
	}
	if err := json.Unmarshal(body, &payload); err != nil || len(payload.Transform) != 16 {
		http.Error(w, "body must be {\"transform\":[...16 floats...]}", http.StatusBadRequest)
		return
	}

	var m [16]float32
	for i, v := range payload.Transform {
		if math.IsNaN(v) || math.IsInf(v, 0) {
			http.Error(w, "transform contains NaN or Inf", http.StatusBadRequest)
			return
		}
		m[i] = float32(v)
	}

	h.SetTransform(sessionKey, sensorID, m)
	log.Printf("[%s] calibration stored for sensor %q\n", sessionKey, sensorID)
	w.WriteHeader(http.StatusOK)
}

func main() {
	http.HandleFunc("/ws", wsHandler)
	http.HandleFunc("/calibrate", calibrateHandler)
	log.Println("Holo visualize server listening on :8080")
	log.Fatal(http.ListenAndServe(":8080", nil))
}
