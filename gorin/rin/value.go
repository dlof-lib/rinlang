// Package rin هو محرك Rin الثاني المكتوب بلغة Go (gorin).
// يقرأ نفس لغة Rin (انظر docs/syntax.md) وينفّذها بمفسّر يمشي على الشجرة.
package rin

import (
	"fmt"
	"math"
	"strconv"
	"strings"
)

// Value: nil | float64 | string | bool | *Array | *Map | *Func | *Native | *Instance | *Class | *Bound
type Value interface{}

type Array struct{ Items []Value }

// Map قاموس بترتيب الإدخال + فهرس تجزئة للمفاتيح البدائية (O(1)).
type Map struct {
	Keys []Value
	Vals []Value
	idx  map[mapKey]int
}

type mapKey struct {
	kind byte // 0 nil, 1 bool, 2 number, 3 string
	n    float64
	s    string
}

func primKey(v Value) (mapKey, bool) {
	switch x := v.(type) {
	case nil:
		return mapKey{kind: 0}, true
	case bool:
		if x {
			return mapKey{kind: 1, n: 1}, true
		}
		return mapKey{kind: 1}, true
	case float64:
		if math.IsNaN(x) {
			return mapKey{}, false
		}
		if x == 0 {
			x = 0
		}
		return mapKey{kind: 2, n: x}, true
	case string:
		return mapKey{kind: 3, s: x}, true
	}
	return mapKey{}, false
}

func NewMap() *Map { return &Map{idx: map[mapKey]int{}} }

func (m *Map) Len() int { return len(m.Keys) }

func (m *Map) find(k Value) int {
	if pk, ok := primKey(k); ok {
		if i, found := m.idx[pk]; found {
			return i
		}
		return -1
	}
	for i, x := range m.Keys {
		if valuesEqual(x, k) {
			return i
		}
	}
	return -1
}

func (m *Map) Get(k Value) (Value, bool) {
	if i := m.find(k); i >= 0 {
		return m.Vals[i], true
	}
	return nil, false
}

func (m *Map) Put(k, v Value) {
	if i := m.find(k); i >= 0 {
		m.Vals[i] = v
		return
	}
	m.Keys = append(m.Keys, k)
	m.Vals = append(m.Vals, v)
	if pk, ok := primKey(k); ok {
		m.idx[pk] = len(m.Keys) - 1
	}
}

func (m *Map) Delete(k Value) bool {
	i := m.find(k)
	if i < 0 {
		return false
	}
	m.Keys = append(m.Keys[:i], m.Keys[i+1:]...)
	m.Vals = append(m.Vals[:i], m.Vals[i+1:]...)
	m.idx = make(map[mapKey]int, len(m.Keys))
	for j, key := range m.Keys {
		if pk, ok := primKey(key); ok {
			if _, dup := m.idx[pk]; !dup {
				m.idx[pk] = j
			}
		}
	}
	return true
}

type Func struct {
	Decl    *FuncDecl
	Closure *Env
	Owner   *Class // للطرق: الصنف الذي عُرِّفت فيه (لـ super مستقبلاً)
}

type Native struct {
	Name string
	Fn   func(in *Interp, args []Value, line int) Value
}

type Class struct {
	Name    string
	Base    *Class
	Fields  []*LetStmt // مُهيِّئات الحقول
	Methods map[string]*Func
	env     *Env
}

func (c *Class) findMethod(name string) *Func {
	for k := c; k != nil; k = k.Base {
		if f, ok := k.Methods[name]; ok {
			return f
		}
	}
	return nil
}

type Instance struct {
	Class  *Class
	Fields *Map
}

type Bound struct {
	Fn   *Func
	Self *Instance
}

func typeName(v Value) string {
	switch x := v.(type) {
	case nil:
		return "nil"
	case float64:
		return "number"
	case string:
		return "string"
	case bool:
		return "bool"
	case *Array:
		return "array"
	case *Map:
		return "map"
	case *Func, *Native, *Bound:
		return "function"
	case *Class:
		return "class"
	case *Instance:
		return x.Class.Name
	}
	return "unknown"
}

func truthy(v Value) bool {
	switch x := v.(type) {
	case nil:
		return false
	case bool:
		return x
	case float64:
		return x != 0
	}
	return true
}

func formatNumber(f float64) string {
	if f == math.Trunc(f) && math.Abs(f) < 9.2e18 {
		return strconv.FormatInt(int64(f), 10)
	}
	if math.IsNaN(f) {
		return "nan"
	}
	if math.IsInf(f, 1) {
		return "inf"
	}
	if math.IsInf(f, -1) {
		return "-inf"
	}
	return strconv.FormatFloat(f, 'g', 6, 64) // نفس ostream الافتراضي في المحرك الأصلي
}

// display: كما toDisplayString في C++ (النص الخام بلا اقتباس على المستوى الأعلى).
func display(v Value) string {
	switch x := v.(type) {
	case nil:
		return "nil"
	case bool:
		if x {
			return "true"
		}
		return "false"
	case string:
		return x
	case float64:
		return formatNumber(x)
	case *Array:
		var sb strings.Builder
		sb.WriteByte('[')
		for i, it := range x.Items {
			if i > 0 {
				sb.WriteString(", ")
			}
			sb.WriteString(repr(it))
		}
		sb.WriteByte(']')
		return sb.String()
	case *Map:
		var sb strings.Builder
		sb.WriteByte('{')
		for i := range x.Keys {
			if i > 0 {
				sb.WriteString(", ")
			}
			sb.WriteString(repr(x.Keys[i]))
			sb.WriteString(": ")
			sb.WriteString(repr(x.Vals[i]))
		}
		sb.WriteByte('}')
		return sb.String()
	case *Func, *Native, *Bound:
		return "<function>"
	case *Class:
		return "<class " + x.Name + ">"
	case *Instance:
		return "<" + x.Class.Name + " instance>"
	}
	return fmt.Sprint(v)
}

// repr: داخل المصفوفات والقواميس تُقتبس النصوص.
func repr(v Value) string {
	if s, ok := v.(string); ok {
		return strconv.Quote(s)
	}
	return display(v)
}

func valuesEqual(a, b Value) bool {
	switch x := a.(type) {
	case nil:
		return b == nil
	case float64:
		y, ok := b.(float64)
		return ok && x == y
	case string:
		y, ok := b.(string)
		return ok && x == y
	case bool:
		y, ok := b.(bool)
		return ok && x == y
	case *Array:
		y, ok := b.(*Array)
		if !ok {
			return false
		}
		if x == y {
			return true
		}
		if len(x.Items) != len(y.Items) {
			return false
		}
		for i := range x.Items {
			if !valuesEqual(x.Items[i], y.Items[i]) {
				return false
			}
		}
		return true
	case *Map:
		y, ok := b.(*Map)
		if !ok {
			return false
		}
		if x == y {
			return true
		}
		if x.Len() != y.Len() {
			return false
		}
		for i, k := range x.Keys {
			v2, found := y.Get(k)
			if !found || !valuesEqual(x.Vals[i], v2) {
				return false
			}
		}
		return true
	}
	return a == b // مؤشرات: هوية
}
