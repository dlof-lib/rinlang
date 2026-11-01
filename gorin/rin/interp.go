package rin

import (
	"bufio"
	"io"
	"math"
	"math/rand"
)

type flow int

const (
	fNone flow = iota
	fBreak
	fContinue
	fReturn
)

// Interp مفسّر Rin. return/break/continue تنتقل كقيمة flow (لا panic)؛
// panic فقط لـ throw والأخطاء، ويلتقطها try/catch وRun.
type Interp struct {
	Out      *bufio.Writer
	MaxDepth int   // حد عمق الاستدعاء (الافتراضي 10000)
	Budget   int64 // سقف عدد العبارات؛ 0 = بلا حد
	steps    int64
	depth    int
	builtins *Env
	global   *Env
	rng      *rand.Rand
}

const Version = "0.1.0-go"

func New(out io.Writer) *Interp {
	in := &Interp{
		Out:      bufio.NewWriterSize(out, 64*1024),
		MaxDepth: 10000,
		rng:      rand.New(rand.NewSource(1)),
	}
	in.builtins = newEnv(nil)
	in.global = newEnv(in.builtins)
	in.registerBuiltins()
	return in
}

// SetProgramArgs يعرّف المتغير العام `args` (مصفوفة نصوص) كما يفعل المحرك الأصلي لوسائط سطر الأوامر
// بعد اسم الملف. يُستدعى قبل Run.
func (in *Interp) SetProgramArgs(a []string) {
	items := make([]Value, len(a))
	for i, x := range a {
		items[i] = x
	}
	in.global.define("args", &Array{Items: items})
}

// Check يحلّل المصدر فقط (بلا تنفيذ). يعيد *RinError عند خطأ لغوي/نحوي أو ميزة لا يدعمها هذا المحرك.
func Check(src string) (err error) {
	defer func() {
		if r := recover(); r != nil {
			if x, ok := r.(*RinError); ok {
				err = x
				return
			}
			panic(r)
		}
	}()
	Parse(src)
	return nil
}

// Run يحلّل وينفّذ المصدر. يعيد *RinError عند أي خطأ (تحليل أو تشغيل).
func (in *Interp) Run(src string) (err error) {
	defer func() {
		if r := recover(); r != nil {
			switch x := r.(type) {
			case *RinError:
				err = x
			case *Thrown:
				err = &RinError{Code: "E0035", Msg: "unhandled exception: " + display(x.Val), Line: x.Line}
			default:
				in.Out.Flush()
				panic(r)
			}
		}
		in.Out.Flush()
	}()
	prog := Parse(src)
	for _, s := range prog.Stmts {
		if fl, _ := in.exec(s, in.global); fl == fReturn {
			break
		}
	}
	return nil
}

func (in *Interp) execBlock(b *Block, env *Env) (flow, Value) {
	if b.NeedsEnv {
		env = newEnv(env)
	}
	for _, s := range b.Stmts {
		if fl, v := in.exec(s, env); fl != fNone {
			return fl, v
		}
	}
	return fNone, nil
}

func (in *Interp) exec(s Stmt, env *Env) (flow, Value) {
	if in.Budget > 0 {
		in.steps++
		if in.steps > in.Budget {
			panic(rtErr("E0035", 0, "Execution limit exceeded\nPossible infinite loop"))
		}
	}
	switch n := s.(type) {
	case *ExprStmt:
		in.eval(n.E, env)
		return fNone, nil
	case *LetStmt:
		var v Value
		if n.Init != nil {
			v = in.eval(n.Init, env)
		}
		env.define(n.Name, v)
		return fNone, nil
	case *PrintStmt:
		for i, a := range n.Args {
			if i > 0 {
				in.Out.WriteByte(' ')
			}
			in.Out.WriteString(display(in.eval(a, env)))
		}
		in.Out.WriteByte('\n')
		return fNone, nil
	case *Block:
		return in.execBlock(n, env)
	case *IfStmt:
		if truthy(in.eval(n.Cond, env)) {
			return in.execBlock(n.Then, env)
		}
		switch e := n.Else.(type) {
		case *Block:
			return in.execBlock(e, env)
		case *IfStmt:
			return in.exec(e, env)
		}
		return fNone, nil
	case *WhileStmt:
		for truthy(in.eval(n.Cond, env)) {
			fl, v := in.execBlock(n.Body, env)
			if fl == fBreak {
				break
			}
			if fl == fReturn {
				return fl, v
			}
			if in.Budget > 0 {
				in.steps++
				if in.steps > in.Budget {
					panic(rtErr("E0035", 0, "Execution limit exceeded\nPossible infinite loop"))
				}
			}
		}
		return fNone, nil
	case *ForStmt:
		loop := newEnv(env)
		if n.Init != nil {
			in.exec(n.Init, loop)
		}
		for n.Cond == nil || truthy(in.eval(n.Cond, loop)) {
			fl, v := in.execBlock(n.Body, loop)
			if fl == fBreak {
				break
			}
			if fl == fReturn {
				return fl, v
			}
			if n.Incr != nil {
				in.eval(n.Incr, loop)
			}
			if in.Budget > 0 {
				in.steps++
				if in.steps > in.Budget {
					panic(rtErr("E0035", 0, "Execution limit exceeded\nPossible infinite loop"))
				}
			}
		}
		return fNone, nil
	case *ForInStmt:
		return in.execForIn(n, env)
	case *BreakStmt:
		return fBreak, nil
	case *ContinueStmt:
		return fContinue, nil
	case *ReturnStmt:
		var v Value
		if n.Value != nil {
			v = in.eval(n.Value, env)
		}
		return fReturn, v
	case *TryStmt:
		return in.execTry(n, env)
	case *ThrowStmt:
		panic(&Thrown{Val: in.eval(n.Value, env), Line: n.Line})
	case *FuncStmt:
		env.define(n.Fn.Name, &Func{Decl: n.Fn, Closure: env})
		return fNone, nil
	case *ClassStmt:
		in.defineClass(n, env)
		return fNone, nil
	}
	panic(rtErr("E0035", 0, "internal: unknown statement %T", s))
}

func (in *Interp) execForIn(n *ForInStmt, env *Env) (flow, Value) {
	it := in.eval(n.Iter, env)
	var items []Value
	switch c := it.(type) {
	case *Array:
		items = c.Items[:len(c.Items):len(c.Items)]
	case *Map:
		items = append([]Value(nil), c.Keys...)
	case string:
		for _, r := range c {
			items = append(items, string(r))
		}
	default:
		panic(rtErr("E0004", n.Line, "cannot iterate over a `%s`", typeName(it)))
	}
	for _, x := range items {
		iterEnv := env
		if n.Declare {
			iterEnv = newEnv(env)
			iterEnv.define(n.Var, x)
		} else if !env.assign(n.Var, x) {
			panic(rtErr("E0001", n.Line, "undefined variable `%s`", n.Var))
		}
		fl, v := in.execBlock(n.Body, iterEnv)
		if fl == fBreak {
			break
		}
		if fl == fReturn {
			return fl, v
		}
	}
	return fNone, nil
}

func (in *Interp) execTry(n *TryStmt, env *Env) (fl flow, rv Value) {
	var caught Value
	didCatch := false
	savedDepth := in.depth
	func() {
		defer func() {
			if r := recover(); r != nil {
				switch x := r.(type) {
				case *Thrown:
					// نفس شكل المحرك الأصلي: {value, message, line}
					m := NewMap()
					m.Put("value", x.Val)
					m.Put("message", display(x.Val))
					m.Put("line", float64(x.Line))
					caught, didCatch = m, true
				case *RinError:
					// {message, line, code}
					m := NewMap()
					m.Put("message", "["+x.Code+"] "+x.Msg)
					m.Put("line", float64(x.Line))
					m.Put("code", x.Code)
					caught, didCatch = m, true
				default:
					panic(r)
				}
				in.depth = savedDepth
			}
		}()
		fl, rv = in.execBlock(n.Body, env)
	}()
	if didCatch {
		cenv := newEnv(env)
		if n.Var != "" {
			cenv.define(n.Var, caught)
		}
		return in.execBlock(n.Catch, cenv)
	}
	return fl, rv
}

func (in *Interp) defineClass(n *ClassStmt, env *Env) {
	cls := &Class{Name: n.Name, Fields: n.Fields, Methods: map[string]*Func{}, env: env}
	if n.Base != "" {
		bv, ok := env.get(n.Base)
		base, isCls := bv.(*Class)
		if !ok || !isCls {
			panic(rtErr("E0001", n.Line, "undefined base class `%s`", n.Base))
		}
		cls.Base = base
	}
	for _, m := range n.Methods {
		cls.Methods[m.Name] = &Func{Decl: m, Closure: env, Owner: cls}
	}
	env.define(n.Name, cls)
}

func (in *Interp) instantiate(c *Class, args []Value, line int) Value {
	inst := &Instance{Class: c, Fields: NewMap()}
	var chain []*Class
	for k := c; k != nil; k = k.Base {
		chain = append([]*Class{k}, chain...) // الأب أولاً
	}
	for _, k := range chain {
		fenv := newEnv(k.env)
		fenv.define("self", inst)
		for _, f := range k.Fields {
			var v Value
			if f.Init != nil {
				v = in.eval(f.Init, fenv)
			}
			inst.Fields.Put(f.Name, v)
		}
	}
	if ctor := c.findMethod("init"); ctor != nil {
		in.callFunc(ctor, inst, args, line)
	} else if len(args) > 0 {
		panic(rtErr("E0007", line, "`%s` has no `init` and takes no arguments, but got %d", c.Name, len(args)))
	}
	return inst
}

func (in *Interp) callValue(callee Value, args []Value, line int) Value {
	switch f := callee.(type) {
	case *Func:
		return in.callFunc(f, nil, args, line)
	case *Bound:
		return in.callFunc(f.Fn, f.Self, args, line)
	case *Native:
		return f.Fn(in, args, line)
	case *Class:
		return in.instantiate(f, args, line)
	}
	panic(rtErr("E0004", line, "a value of type `%s` is not callable", typeName(callee)))
}

func (in *Interp) callFunc(f *Func, self *Instance, args []Value, line int) Value {
	if len(args) != len(f.Decl.Params) {
		panic(rtErr("E0007", line, "'%s' expects %d argument(s) but got %d", f.Decl.Name, len(f.Decl.Params), len(args)))
	}
	if in.depth >= in.MaxDepth {
		panic(rtErr("E0035", line, "maximum function call depth exceeded (%d)", in.MaxDepth))
	}
	in.depth++
	env := &Env{parent: f.Closure}
	env.names = make([]string, 0, len(args)+1)
	env.vals = make([]Value, 0, len(args)+1)
	if self != nil {
		env.names = append(env.names, "self")
		env.vals = append(env.vals, self)
	}
	for i, p := range f.Decl.Params {
		env.names = append(env.names, p)
		env.vals = append(env.vals, args[i])
	}
	var result Value
	for _, s := range f.Decl.Body.Stmts {
		if fl, v := in.exec(s, env); fl == fReturn {
			result = v
			break
		}
	}
	in.depth--
	return result
}

// ---------------- التعابير ----------------

func (in *Interp) eval(e Expr, env *Env) Value {
	switch n := e.(type) {
	case *NumLit:
		return n.V
	case *Ident:
		if v, ok := env.get(n.Name); ok {
			return v
		}
		panic(rtErr("E0001", n.Line, "undefined variable `%s`", n.Name))
	case *Binary:
		return in.evalBinary(n, env)
	case *Call:
		callee := in.eval(n.Callee, env)
		args := make([]Value, len(n.Args))
		for i, a := range n.Args {
			args[i] = in.eval(a, env)
		}
		return in.callValue(callee, args, n.Line)
	case *StrLit:
		return n.V
	case *BoolLit:
		return n.V
	case *NilLit:
		return nil
	case *Assign:
		return in.evalAssign(n, env)
	case *Index:
		return in.evalIndex(n, env)
	case *Member:
		return in.getMember(in.eval(n.Obj, env), n.Name, n.Line)
	case *Logical:
		l := truthy(in.eval(n.L, env))
		if n.And {
			return l && truthy(in.eval(n.R, env))
		}
		return l || truthy(in.eval(n.R, env))
	case *Unary:
		x := in.eval(n.X, env)
		if n.Op == tBang {
			return !truthy(x)
		}
		f, ok := x.(float64)
		if !ok {
			panic(rtErr("E0004", n.Line, "unary `-` expects a number, found `%s`", typeName(x)))
		}
		return -f
	case *Template:
		var b []byte
		for _, p := range n.Parts {
			if p.E == nil {
				b = append(b, p.Text...)
			} else {
				b = append(b, display(in.eval(p.E, env))...)
			}
		}
		return string(b)
	case *ArrayLit:
		items := make([]Value, len(n.Elems))
		for i, x := range n.Elems {
			items[i] = in.eval(x, env)
		}
		return &Array{Items: items}
	case *MapLit:
		m := NewMap()
		for i := range n.Keys {
			m.Put(in.eval(n.Keys[i], env), in.eval(n.Vals[i], env))
		}
		return m
	case *Lambda:
		return &Func{Decl: n.Fn, Closure: env}
	}
	panic(rtErr("E0035", 0, "internal: unknown expression %T", e))
}

func opName(t TokType) string {
	switch t {
	case tPlus:
		return "+"
	case tMinus:
		return "-"
	case tStar:
		return "*"
	case tSlash:
		return "/"
	case tPercent:
		return "%"
	case tGt:
		return ">"
	case tGe:
		return ">="
	case tLt:
		return "<"
	case tLe:
		return "<="
	}
	return "?"
}

func (in *Interp) evalBinary(n *Binary, env *Env) Value {
	l := in.eval(n.L, env)
	r := in.eval(n.R, env)
	lf, lok := l.(float64)
	rf, rok := r.(float64)
	switch n.Op {
	case tPlus:
		if lok && rok {
			return lf + rf
		}
		_, ls := l.(string)
		_, rs := r.(string)
		if ls || rs {
			return display(l) + display(r)
		}
		panic(rtErr("E0004", n.Line, "`+` operands must be numbers or strings, found `%s` and `%s`", typeName(l), typeName(r)))
	case tEqEq:
		return valuesEqual(l, r)
	case tBangEq:
		return !valuesEqual(l, r)
	}
	if !lok || !rok {
		panic(rtErr("E0004", n.Line, "`%s` operands must be numbers, found `%s` and `%s`", opName(n.Op), typeName(l), typeName(r)))
	}
	switch n.Op {
	case tMinus:
		return lf - rf
	case tStar:
		return lf * rf
	case tSlash:
		if rf == 0 {
			panic(rtErr("E0035", n.Line, "division by zero (the right-hand side of `/` evaluated to 0)"))
		}
		return lf / rf
	case tPercent:
		if rf == 0 {
			panic(rtErr("E0035", n.Line, "division by zero (the right-hand side of `%%` evaluated to 0)"))
		}
		return math.Mod(lf, rf)
	case tGt:
		return lf > rf
	case tGe:
		return lf >= rf
	case tLt:
		return lf < rf
	case tLe:
		return lf <= rf
	}
	panic(rtErr("E0035", n.Line, "internal: bad binary operator"))
}

func toIndex(v Value, size int, line int) int {
	f, ok := v.(float64)
	if !ok || f != math.Trunc(f) {
		panic(rtErr("E0021", line, "index must be a whole number, found `%s`", display(v)))
	}
	if f < 0 || f >= float64(size) {
		panic(rtErr("E0021", line, "index %s is out of range (size %d)", display(v), size))
	}
	return int(f)
}

func (in *Interp) evalIndex(n *Index, env *Env) Value {
	obj := in.eval(n.Obj, env)
	idx := in.eval(n.Idx, env)
	switch o := obj.(type) {
	case *Array:
		return o.Items[toIndex(idx, len(o.Items), n.Line)]
	case *Map:
		v, _ := o.Get(idx)
		return v
	case string:
		i := toIndex(idx, len(o), n.Line)
		return o[i : i+1]
	}
	panic(rtErr("E0004", n.Line, "cannot index a `%s`", typeName(obj)))
}

func (in *Interp) getMember(obj Value, name string, line int) Value {
	if o, ok := obj.(*Instance); ok {
		if v, found := o.Fields.Get(name); found {
			return v
		}
		if m := o.Class.findMethod(name); m != nil {
			return &Bound{Fn: m, Self: o}
		}
		panic(rtErr("E0004", line, "`%s` has no field or method `%s`", o.Class.Name, name))
	}
	panic(rtErr("E0004", line, "cannot read field `%s` of a `%s`", name, typeName(obj)))
}

func (in *Interp) evalAssign(n *Assign, env *Env) Value {
	switch t := n.Target.(type) {
	case *Ident:
		v := in.eval(n.Value, env)
		if !env.assign(t.Name, v) {
			panic(rtErr("E0001", n.Line, "undefined variable `%s`", t.Name))
		}
		return v
	case *Index:
		obj := in.eval(t.Obj, env)
		idx := in.eval(t.Idx, env)
		v := in.eval(n.Value, env)
		switch o := obj.(type) {
		case *Array:
			o.Items[toIndex(idx, len(o.Items), t.Line)] = v
		case *Map:
			o.Put(idx, v)
		default:
			panic(rtErr("E0004", t.Line, "cannot assign by index into a `%s`", typeName(obj)))
		}
		return v
	case *Member:
		obj := in.eval(t.Obj, env)
		v := in.eval(n.Value, env)
		inst, ok := obj.(*Instance)
		if !ok {
			panic(rtErr("E0004", t.Line, "cannot set field `%s` on a `%s`", t.Name, typeName(obj)))
		}
		inst.Fields.Put(t.Name, v)
		return v
	}
	panic(rtErr("E0012", n.Line, "Invalid assignment target"))
}
