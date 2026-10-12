package rin

// Env: نطاق متغيّرات. النطاقات الصغيرة مسح خطّي على شريحتين (أسرع من map)،
// وتتحوّل إلى فهرس تجزئة حين تتجاوز 12 اسماً (البيئة العامة والمدمجات).
type Env struct {
	names  []string
	vals   []Value
	m      map[string]int
	parent *Env
}

func newEnv(parent *Env) *Env { return &Env{parent: parent} }

func (e *Env) index(name string) int {
	if e.m != nil {
		if i, ok := e.m[name]; ok {
			return i
		}
		return -1
	}
	for i := range e.names {
		if e.names[i] == name {
			return i
		}
	}
	return -1
}

func (e *Env) define(name string, v Value) {
	if i := e.index(name); i >= 0 {
		e.vals[i] = v
		return
	}
	e.names = append(e.names, name)
	e.vals = append(e.vals, v)
	if e.m != nil {
		e.m[name] = len(e.names) - 1
	} else if len(e.names) > 12 {
		e.m = make(map[string]int, len(e.names)*2)
		for i, n := range e.names {
			e.m[n] = i
		}
	}
}

func (e *Env) get(name string) (Value, bool) {
	for env := e; env != nil; env = env.parent {
		if i := env.index(name); i >= 0 {
			return env.vals[i], true
		}
	}
	return nil, false
}

func (e *Env) assign(name string, v Value) bool {
	for env := e; env != nil; env = env.parent {
		if i := env.index(name); i >= 0 {
			env.vals[i] = v
			return true
		}
	}
	return false
}
