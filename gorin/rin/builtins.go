package rin

import (
	"math"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"
)

func need(name string, args []Value, n int, line int) {
	if len(args) != n {
		panic(rtErr("E0007", line, "'%s' expects %d argument(s) but got %d", name, n, len(args)))
	}
}

func asStr(name string, v Value, line int) string {
	s, ok := v.(string)
	if !ok {
		panic(rtErr("E0004", line, "'%s' expects a string but got %s", name, typeName(v)))
	}
	return s
}

func asNum(name string, v Value, line int) float64 {
	f, ok := v.(float64)
	if !ok {
		panic(rtErr("E0004", line, "'%s' expects a number but got %s", name, typeName(v)))
	}
	return f
}

func asArr(name string, v Value, line int) *Array {
	a, ok := v.(*Array)
	if !ok {
		panic(rtErr("E0004", line, "'%s' expects an array but got %s", name, typeName(v)))
	}
	return a
}

func asMap(name string, v Value, line int) *Map {
	m, ok := v.(*Map)
	if !ok {
		panic(rtErr("E0004", line, "'%s' expects a map but got %s", name, typeName(v)))
	}
	return m
}

func numArray(name string, v Value, line int) []float64 {
	a := asArr(name, v, line)
	out := make([]float64, len(a.Items))
	for i, x := range a.Items {
		f, ok := x.(float64)
		if !ok {
			panic(rtErr("E0004", line, "'%s' expects an array of numbers", name))
		}
		out[i] = f
	}
	return out
}

func (in *Interp) reg(name string, fn func(in *Interp, a []Value, line int) Value) {
	in.builtins.define(name, &Native{Name: name, Fn: fn})
}

// math1 يسجّل دالة رياضية بوسيط رقمي واحد.
func (in *Interp) math1(name string, f func(float64) float64) {
	in.reg(name, func(in *Interp, a []Value, line int) Value {
		need(name, a, 1, line)
		return f(asNum(name, a[0], line))
	})
}

func (in *Interp) registerBuiltins() {
	in.reg("len", func(in *Interp, a []Value, line int) Value {
		need("len", a, 1, line)
		switch x := a[0].(type) {
		case string:
			return float64(len(x))
		case *Array:
			return float64(len(x.Items))
		case *Map:
			return float64(x.Len())
		}
		panic(rtErr("E0004", line, "'len' expects a string, array or map but got %s", typeName(a[0])))
	})
	in.reg("type", func(in *Interp, a []Value, line int) Value {
		need("type", a, 1, line)
		return typeName(a[0])
	})
	toStr := func(in *Interp, a []Value, line int) Value {
		need("toString", a, 1, line)
		return display(a[0])
	}
	in.reg("toString", toStr)
	in.reg("str", toStr)
	in.reg("toNumber", func(in *Interp, a []Value, line int) Value {
		need("toNumber", a, 1, line)
		if f, ok := a[0].(float64); ok {
			return f
		}
		s := asStr("toNumber", a[0], line)
		f, err := strconv.ParseFloat(strings.TrimSpace(s), 64)
		if err != nil {
			panic(rtErr("E0035", line, "'toNumber': \"%s\" is not a valid number", s))
		}
		return f
	})
	in.reg("toBool", func(in *Interp, a []Value, line int) Value {
		need("toBool", a, 1, line)
		return truthy(a[0])
	})
	in.reg("rinVersion", func(in *Interp, a []Value, line int) Value {
		need("rinVersion", a, 0, line)
		return Version
	})

	// ---- نصوص ----
	in.reg("upper", func(in *Interp, a []Value, line int) Value {
		need("upper", a, 1, line)
		return strings.ToUpper(asStr("upper", a[0], line))
	})
	in.reg("lower", func(in *Interp, a []Value, line int) Value {
		need("lower", a, 1, line)
		return strings.ToLower(asStr("lower", a[0], line))
	})
	in.reg("trim", func(in *Interp, a []Value, line int) Value {
		need("trim", a, 1, line)
		return strings.TrimSpace(asStr("trim", a[0], line))
	})
	in.reg("substr", func(in *Interp, a []Value, line int) Value {
		if len(a) != 2 && len(a) != 3 {
			panic(rtErr("E0007", line, "'substr' expects 2 or 3 argument(s) but got %d", len(a)))
		}
		s := asStr("substr", a[0], line)
		start := int(asNum("substr", a[1], line))
		if start < 0 {
			start = 0
		}
		if start > len(s) {
			start = len(s)
		}
		n := len(s) - start
		if len(a) == 3 {
			n = int(asNum("substr", a[2], line))
		}
		if n < 0 {
			n = 0
		}
		if n > len(s)-start {
			n = len(s) - start
		}
		return s[start : start+n]
	})
	in.reg("split", func(in *Interp, a []Value, line int) Value {
		need("split", a, 2, line)
		s := asStr("split", a[0], line)
		sep := asStr("split", a[1], line)
		var parts []string
		if sep == "" {
			for _, r := range s {
				parts = append(parts, string(r))
			}
		} else {
			parts = strings.Split(s, sep)
		}
		out := make([]Value, len(parts))
		for i, p := range parts {
			out[i] = p
		}
		return &Array{Items: out}
	})
	in.reg("join", func(in *Interp, a []Value, line int) Value {
		need("join", a, 2, line)
		arr := asArr("join", a[0], line)
		sep := asStr("join", a[1], line)
		var sb strings.Builder
		for i, x := range arr.Items {
			if i > 0 {
				sb.WriteString(sep)
			}
			sb.WriteString(display(x))
		}
		return sb.String()
	})
	in.reg("indexOf", func(in *Interp, a []Value, line int) Value {
		need("indexOf", a, 2, line)
		return float64(strings.Index(asStr("indexOf", a[0], line), asStr("indexOf", a[1], line)))
	})
	in.reg("replace", func(in *Interp, a []Value, line int) Value {
		need("replace", a, 3, line)
		s := asStr("replace", a[0], line)
		from := asStr("replace", a[1], line)
		if from == "" {
			return s
		}
		return strings.ReplaceAll(s, from, asStr("replace", a[2], line))
	})
	in.reg("contains", func(in *Interp, a []Value, line int) Value {
		need("contains", a, 2, line)
		switch x := a[0].(type) {
		case string:
			return strings.Contains(x, asStr("contains", a[1], line))
		case *Array:
			for _, it := range x.Items {
				if valuesEqual(it, a[1]) {
					return true
				}
			}
			return false
		}
		panic(rtErr("E0004", line, "'contains' expects a string or array as the first argument"))
	})
	in.reg("charAt", func(in *Interp, a []Value, line int) Value {
		need("charAt", a, 2, line)
		s := asStr("charAt", a[0], line)
		i := int(asNum("charAt", a[1], line))
		if i < 0 || i >= len(s) {
			panic(rtErr("E0035", line, "'charAt': index out of range"))
		}
		return s[i : i+1]
	})
	in.reg("chr", func(in *Interp, a []Value, line int) Value {
		need("chr", a, 1, line)
		return string(rune(int(asNum("chr", a[0], line))))
	})
	in.reg("ord", func(in *Interp, a []Value, line int) Value {
		need("ord", a, 1, line)
		s := asStr("ord", a[0], line)
		if s == "" {
			panic(rtErr("E0035", line, "'ord': empty string"))
		}
		r, _ := utf8.DecodeRuneInString(s)
		return float64(r)
	})

	// ---- مصفوفات وقواميس ----
	in.reg("push", func(in *Interp, a []Value, line int) Value {
		need("push", a, 2, line)
		arr := asArr("push", a[0], line)
		arr.Items = append(arr.Items, a[1])
		return float64(len(arr.Items))
	})
	in.reg("pop", func(in *Interp, a []Value, line int) Value {
		need("pop", a, 1, line)
		arr := asArr("pop", a[0], line)
		if len(arr.Items) == 0 {
			panic(rtErr("E0004", line, "'pop': the array is empty"))
		}
		v := arr.Items[len(arr.Items)-1]
		arr.Items = arr.Items[:len(arr.Items)-1]
		return v
	})
	in.reg("sort", func(in *Interp, a []Value, line int) Value {
		need("sort", a, 1, line)
		arr := asArr("sort", a[0], line)
		sort.SliceStable(arr.Items, func(i, j int) bool {
			switch x := arr.Items[i].(type) {
			case float64:
				if y, ok := arr.Items[j].(float64); ok {
					return x < y
				}
			case string:
				if y, ok := arr.Items[j].(string); ok {
					return x < y
				}
			}
			panic(rtErr("E0035", line, "'sort' requires all elements to be numbers or all strings"))
		})
		return arr
	})
	in.reg("keys", func(in *Interp, a []Value, line int) Value {
		need("keys", a, 1, line)
		m := asMap("keys", a[0], line)
		return &Array{Items: append([]Value(nil), m.Keys...)}
	})
	in.reg("values", func(in *Interp, a []Value, line int) Value {
		need("values", a, 1, line)
		m := asMap("values", a[0], line)
		return &Array{Items: append([]Value(nil), m.Vals...)}
	})
	in.reg("has", func(in *Interp, a []Value, line int) Value {
		need("has", a, 2, line)
		switch x := a[0].(type) {
		case *Map:
			_, ok := x.Get(a[1])
			return ok
		case *Array:
			for _, it := range x.Items {
				if valuesEqual(it, a[1]) {
					return true
				}
			}
			return false
		case string:
			return strings.Contains(x, asStr("has", a[1], line))
		}
		panic(rtErr("E0004", line, "'has' expects a map, array or string but got %s", typeName(a[0])))
	})
	in.reg("remove", func(in *Interp, a []Value, line int) Value {
		need("remove", a, 2, line)
		return asMap("remove", a[0], line).Delete(a[1])
	})

	// ---- رياضيات ----
	in.math1("abs", math.Abs)
	in.math1("sqrt", math.Sqrt)
	in.math1("floor", math.Floor)
	in.math1("ceil", math.Ceil)
	in.math1("round", math.Round)
	in.math1("trunc", math.Trunc)
	in.math1("sin", math.Sin)
	in.math1("cos", math.Cos)
	in.math1("tan", math.Tan)
	in.math1("exp", math.Exp)
	in.math1("log", math.Log)
	in.math1("log10", math.Log10)
	in.reg("pow", func(in *Interp, a []Value, line int) Value {
		need("pow", a, 2, line)
		return math.Pow(asNum("pow", a[0], line), asNum("pow", a[1], line))
	})
	in.reg("min", func(in *Interp, a []Value, line int) Value {
		need("min", a, 2, line)
		return math.Min(asNum("min", a[0], line), asNum("min", a[1], line))
	})
	in.reg("max", func(in *Interp, a []Value, line int) Value {
		need("max", a, 2, line)
		return math.Max(asNum("max", a[0], line), asNum("max", a[1], line))
	})
	in.reg("random", func(in *Interp, a []Value, line int) Value {
		need("random", a, 0, line)
		return in.rng.Float64()
	})
	in.reg("sum", func(in *Interp, a []Value, line int) Value {
		need("sum", a, 1, line)
		t := 0.0
		for _, f := range numArray("sum", a[0], line) {
			t += f
		}
		return t
	})
	in.reg("mean", func(in *Interp, a []Value, line int) Value {
		need("mean", a, 1, line)
		nums := numArray("mean", a[0], line)
		if len(nums) == 0 {
			panic(rtErr("E0035", line, "'mean' of an empty array"))
		}
		t := 0.0
		for _, f := range nums {
			t += f
		}
		return t / float64(len(nums))
	})
}
