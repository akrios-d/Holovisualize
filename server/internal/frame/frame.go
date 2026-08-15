// Package frame defines the binary wire format for point cloud frames.
//
// Format:
//   [4]byte  magic    = "HOLO"
//   uint32   n        = number of points (little-endian)
//   n * 16 bytes per point:
//     float32  X
//     float32  Y
//     float32  Z
//     uint8    R
//     uint8    G
//     uint8    B
//     uint8    _  (padding)
package frame

import (
	"encoding/binary"
	"errors"
	"math"
)

var magic = [4]byte{'H', 'O', 'L', 'O'}

const PointSize = 16
const HeaderSize = 8

// Point is a single coloured 3D point (coordinates in metres).
type Point struct {
	X, Y, Z float32
	R, G, B uint8
}

// Encode serialises a point cloud to the wire format.
func Encode(points []Point) []byte {
	buf := make([]byte, HeaderSize+len(points)*PointSize)
	copy(buf[0:4], magic[:])
	binary.LittleEndian.PutUint32(buf[4:8], uint32(len(points)))

	for i, p := range points {
		off := HeaderSize + i*PointSize
		binary.LittleEndian.PutUint32(buf[off+0:], math.Float32bits(p.X))
		binary.LittleEndian.PutUint32(buf[off+4:], math.Float32bits(p.Y))
		binary.LittleEndian.PutUint32(buf[off+8:], math.Float32bits(p.Z))
		buf[off+12] = p.R
		buf[off+13] = p.G
		buf[off+14] = p.B
		// buf[off+15] = 0 (padding, already zero)
	}
	return buf
}

// Decode deserialises a wire-format frame into a point slice.
func Decode(data []byte) ([]Point, error) {
	if len(data) < HeaderSize {
		return nil, errors.New("frame: too short")
	}
	if [4]byte(data[0:4]) != magic {
		return nil, errors.New("frame: invalid magic")
	}

	n := int(binary.LittleEndian.Uint32(data[4:8]))
	if len(data) < HeaderSize+n*PointSize {
		return nil, errors.New("frame: truncated")
	}

	points := make([]Point, n)
	for i := range points {
		off := HeaderSize + i*PointSize
		points[i] = Point{
			X: math.Float32frombits(binary.LittleEndian.Uint32(data[off+0:])),
			Y: math.Float32frombits(binary.LittleEndian.Uint32(data[off+4:])),
			Z: math.Float32frombits(binary.LittleEndian.Uint32(data[off+8:])),
			R: data[off+12],
			G: data[off+13],
			B: data[off+14],
		}
	}
	return points, nil
}
