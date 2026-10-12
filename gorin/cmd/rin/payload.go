package main

// كشف الحمولة المُلحَقة من rin_ava (backend go): نفس تخطيط cli/linux/src/rin_ava_payload.h
//
//	v1: [name \0 source]       [u64 len LE] "RINAVAP1"
//	v2: [kind name \0 data]    [u64 len LE] "RINAVAP2"   (kind 'S' = مصدر Rin؛ 'B' بايتكود لا يدعمه محرك Go)
//
// الملف العادي بلا مقطع ختامي فلا يتغيّر سلوكه إطلاقاً.

import (
	"bytes"
	"encoding/binary"
	"os"
)

// selfPayload يرجع (الاسم، المصدر، kind، وُجدت). kind = 'S' أو 'B'.
func selfPayload() (name, src string, kind byte, ok bool) {
	exe, err := os.Executable()
	if err != nil {
		return
	}
	f, err := os.Open(exe)
	if err != nil {
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil || st.Size() < 16 {
		return
	}
	tail := make([]byte, 16)
	if _, err = f.ReadAt(tail, st.Size()-16); err != nil {
		return
	}
	magic := string(tail[8:])
	v2 := magic == "RINAVAP2"
	if !v2 && magic != "RINAVAP1" {
		return
	}
	n := binary.LittleEndian.Uint64(tail[:8])
	if n == 0 || n > uint64(st.Size()-16) {
		return
	}
	buf := make([]byte, n)
	if _, err = f.ReadAt(buf, st.Size()-16-int64(n)); err != nil {
		return
	}
	kind = 'S'
	if v2 {
		kind = buf[0]
		buf = buf[1:]
	}
	z := bytes.IndexByte(buf, 0)
	if z < 0 {
		return
	}
	return string(buf[:z]), string(buf[z+1:]), kind, true
}
