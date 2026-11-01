package rin

import "strings"

type parser struct {
	toks []Token
	pos  int
	file string
}

// Parse يحوّل المصدر إلى برنامج؛ يرمي *RinError عند الخطأ (يلتقطه Run).
func Parse(src string) *Block {
	p := &parser{toks: Lex(src)}
	prog := &Block{}
	for !p.at(tEOF) {
		if s := p.statement(); s != nil {
			prog.Stmts = append(prog.Stmts, s)
		}
	}
	prog.NeedsEnv = false // البرنامج يعمل في البيئة العامة مباشرة
	return prog
}

func (p *parser) cur() Token  { return p.toks[p.pos] }
func (p *parser) at(t TokType) bool { return p.toks[p.pos].T == t }
func (p *parser) next() Token {
	t := p.toks[p.pos]
	if t.T != tEOF {
		p.pos++
	}
	return t
}
func (p *parser) match(t TokType) bool {
	if p.at(t) {
		p.pos++
		return true
	}
	return false
}

func (p *parser) fail(msg string) {
	t := p.cur()
	e := rtErr("E0012", t.Line, "%s", msg)
	e.Col = t.Col
	panic(e)
}

func (p *parser) expect(t TokType, msg string) Token {
	if !p.at(t) {
		p.fail(msg)
	}
	return p.next()
}

func (p *parser) ident(msg string) string {
	return p.expect(tIdent, msg).Lex
}

func (p *parser) skipType() {
	// تلميحات الأنواع الاختيارية: `: Number` أو `: Number?` — تُتجاهل في هذا المحرك.
	if p.match(tColon) {
		p.expect(tIdent, "Expected a type name after ':'")
	}
}

func (p *parser) block() *Block {
	p.expect(tLBrace, "Expected '{'")
	b := &Block{}
	for !p.at(tRBrace) {
		if p.at(tEOF) {
			p.fail("Expected '}' before end of file")
		}
		if s := p.statement(); s != nil {
			b.Stmts = append(b.Stmts, s)
			switch s.(type) {
			case *LetStmt, *FuncStmt, *ClassStmt:
				b.NeedsEnv = true
			}
		}
	}
	p.expect(tRBrace, "Expected '}'")
	return b
}

func (p *parser) statement() Stmt {
	t := p.cur()
	switch t.T {
	case tSemi:
		p.next()
		return nil
	case tLBrace:
		return p.block()
	case tLet:
		return p.letStmt()
	case tPrint:
		p.next()
		ps := &PrintStmt{Line: t.Line}
		ps.Args = append(ps.Args, p.expr())
		for p.match(tComma) {
			ps.Args = append(ps.Args, p.expr())
		}
		p.expect(tSemi, "Expected ';' after print statement")
		return ps
	case tFun:
		if p.toks[p.pos+1].T == tIdent {
			p.next()
			return &FuncStmt{Fn: p.funcRest(p.ident("Expected function name"), t.Line)}
		}
	case tIf, tWhen:
		return p.ifStmt()
	case tWhile:
		p.next()
		p.expect(tLParen, "Expected '(' after 'while'")
		c := p.expr()
		p.expect(tRParen, "Expected ')' after condition")
		return &WhileStmt{Cond: c, Body: p.block()}
	case tFor:
		return p.forStmt()
	case tBreak:
		p.next()
		p.expect(tSemi, "Expected ';' after 'break'")
		return &BreakStmt{}
	case tContinue:
		p.next()
		p.expect(tSemi, "Expected ';' after 'continue'")
		return &ContinueStmt{}
	case tReturn:
		p.next()
		r := &ReturnStmt{}
		if !p.at(tSemi) {
			r.Value = p.expr()
		}
		p.expect(tSemi, "Expected ';' after return")
		return r
	case tTry:
		p.next()
		body := p.block()
		p.expect(tCatch, "Expected 'catch' after try block")
		name := ""
		if p.match(tLParen) {
			name = p.ident("Expected a name for the caught error")
			p.expect(tRParen, "Expected ')'")
		}
		return &TryStmt{Body: body, Var: name, Catch: p.block()}
	case tThrow:
		p.next()
		v := p.expr()
		p.expect(tSemi, "Expected ';' after throw")
		return &ThrowStmt{Value: v, Line: t.Line}
	case tIdent:
		if t.Lex == "class" && p.toks[p.pos+1].T == tIdent {
			return p.classStmt()
		}
	}
	e := p.expr()
	p.expect(tSemi, "Expected ';' after expression")
	return &ExprStmt{E: e}
}

func (p *parser) letStmt() Stmt {
	t := p.next() // let
	name := p.ident("Expected variable name after 'let'")
	p.skipType()
	var initExpr Expr
	if p.match(tEq) {
		initExpr = p.expr()
	}
	p.expect(tSemi, "Expected ';' after variable declaration")
	return &LetStmt{Name: name, Init: initExpr, Line: t.Line}
}

func (p *parser) ifStmt() Stmt {
	p.next() // if | when
	p.expect(tLParen, "Expected '(' after 'if'")
	c := p.expr()
	p.expect(tRParen, "Expected ')' after condition")
	s := &IfStmt{Cond: c, Then: p.block()}
	if p.match(tElse) || p.match(tOtherwise) {
		if p.at(tIf) || p.at(tWhen) {
			s.Else = p.ifStmt()
		} else {
			s.Else = p.block()
		}
	}
	return s
}

func (p *parser) forStmt() Stmt {
	t := p.next() // for
	p.expect(tLParen, "Expected '(' after 'for'")
	// for (let x in expr)  |  for (x in expr)
	save := p.pos
	declare := p.match(tLet)
	if p.at(tIdent) && p.toks[p.pos+1].T == tIn {
		name := p.next().Lex
		p.next() // in
		it := p.expr()
		p.expect(tRParen, "Expected ')' after for-in")
		return &ForInStmt{Var: name, Declare: declare, Iter: it, Body: p.block(), Line: t.Line}
	}
	p.pos = save
	fs := &ForStmt{}
	switch {
	case p.at(tSemi):
		p.next()
	case p.at(tLet):
		fs.Init = p.letStmt()
	default:
		e := p.expr()
		p.expect(tSemi, "Expected ';' in for header")
		fs.Init = &ExprStmt{E: e}
	}
	if !p.at(tSemi) {
		fs.Cond = p.expr()
	}
	p.expect(tSemi, "Expected ';' in for header")
	if !p.at(tRParen) {
		fs.Incr = p.expr()
	}
	p.expect(tRParen, "Expected ')' after for header")
	fs.Body = p.block()
	return fs
}

func (p *parser) funcRest(name string, line int) *FuncDecl {
	p.expect(tLParen, "Expected '(' after function name")
	fd := &FuncDecl{Name: name, Line: line}
	if !p.at(tRParen) {
		for {
			fd.Params = append(fd.Params, p.ident("Expected parameter name"))
			p.skipType()
			if !p.match(tComma) {
				break
			}
		}
	}
	p.expect(tRParen, "Expected ')' after parameters")
	p.skipType() // نوع الإرجاع
	fd.Body = p.block()
	return fd
}

func (p *parser) classStmt() Stmt {
	t := p.next() // class
	cs := &ClassStmt{Name: p.ident("Expected class name"), Line: t.Line}
	if p.match(tExtends) {
		cs.Base = p.ident("Expected base class name")
	}
	p.expect(tLBrace, "Expected '{' after class name")
	for !p.at(tRBrace) {
		switch {
		case p.at(tLet):
			cs.Fields = append(cs.Fields, p.letStmt().(*LetStmt))
		case p.at(tFun):
			ft := p.next()
			cs.Methods = append(cs.Methods, p.funcRest(p.ident("Expected method name"), ft.Line))
		case p.at(tSemi):
			p.next()
		default:
			p.fail("A class body may contain only `let` fields and `fun` methods")
		}
	}
	p.expect(tRBrace, "Expected '}' after class body")
	return cs
}

// ---------------- التعابير ----------------

func (p *parser) expr() Expr { return p.assignment() }

func (p *parser) assignment() Expr {
	left := p.or()
	if p.at(tEq) {
		t := p.next()
		val := p.assignment()
		switch left.(type) {
		case *Ident, *Index, *Member:
			return &Assign{Target: left, Value: val, Line: t.Line}
		}
		panic(rtErr("E0012", t.Line, "Invalid assignment target"))
	}
	return left
}

func (p *parser) or() Expr {
	l := p.and()
	for p.match(tOr) {
		l = &Logical{And: false, L: l, R: p.and()}
	}
	return l
}

func (p *parser) and() Expr {
	l := p.equality()
	for p.match(tAnd) {
		l = &Logical{And: true, L: l, R: p.equality()}
	}
	return l
}

func (p *parser) equality() Expr {
	l := p.comparison()
	for p.at(tEqEq) || p.at(tBangEq) {
		t := p.next()
		l = &Binary{Op: t.T, L: l, R: p.comparison(), Line: t.Line}
	}
	return l
}

func (p *parser) comparison() Expr {
	l := p.term()
	for p.at(tGt) || p.at(tGe) || p.at(tLt) || p.at(tLe) {
		t := p.next()
		l = &Binary{Op: t.T, L: l, R: p.term(), Line: t.Line}
	}
	return l
}

func (p *parser) term() Expr {
	l := p.factor()
	for p.at(tPlus) || p.at(tMinus) {
		t := p.next()
		l = &Binary{Op: t.T, L: l, R: p.factor(), Line: t.Line}
	}
	return l
}

func (p *parser) factor() Expr {
	l := p.unary()
	for p.at(tStar) || p.at(tSlash) || p.at(tPercent) {
		t := p.next()
		l = &Binary{Op: t.T, L: l, R: p.unary(), Line: t.Line}
	}
	return l
}

func (p *parser) unary() Expr {
	if p.at(tBang) || p.at(tMinus) {
		t := p.next()
		return &Unary{Op: t.T, X: p.unary(), Line: t.Line}
	}
	return p.postfix()
}

func (p *parser) postfix() Expr {
	e := p.primary()
	for {
		switch {
		case p.at(tLParen):
			t := p.next()
			var args []Expr
			if !p.at(tRParen) {
				for {
					args = append(args, p.expr())
					if !p.match(tComma) {
						break
					}
				}
			}
			p.expect(tRParen, "Expected ')' after arguments")
			e = &Call{Callee: e, Args: args, Line: t.Line}
		case p.at(tLBracket):
			t := p.next()
			idx := p.expr()
			p.expect(tRBracket, "Expected ']'")
			e = &Index{Obj: e, Idx: idx, Line: t.Line}
		case p.at(tDot):
			t := p.next()
			e = &Member{Obj: e, Name: p.ident("Expected a name after '.'"), Line: t.Line}
		default:
			return e
		}
	}
}

func (p *parser) primary() Expr {
	t := p.next()
	switch t.T {
	case tNumber:
		return &NumLit{V: t.Num}
	case tString:
		return &StrLit{V: t.Lex}
	case tTemplate:
		return p.template(t)
	case tTrue:
		return &BoolLit{V: true}
	case tFalse:
		return &BoolLit{V: false}
	case tNil:
		return &NilLit{}
	case tIdent:
		return &Ident{Name: t.Lex, Line: t.Line}
	case tLParen:
		e := p.expr()
		p.expect(tRParen, "Expected ')' after expression")
		return e
	case tLBracket:
		a := &ArrayLit{}
		if !p.at(tRBracket) {
			for {
				a.Elems = append(a.Elems, p.expr())
				if !p.match(tComma) || p.at(tRBracket) {
					break
				}
			}
		}
		p.expect(tRBracket, "Expected ']' after array elements")
		return a
	case tLBrace:
		m := &MapLit{}
		for !p.at(tRBrace) {
			kt := p.next()
			if kt.T != tIdent && kt.T != tString {
				p.pos--
				p.fail("each entry in a `{ ... }` map literal starts with an identifier or a quoted string key")
			}
			p.expect(tColon, "Expected ':' after map key")
			m.Keys = append(m.Keys, &StrLit{V: kt.Lex})
			m.Vals = append(m.Vals, p.expr())
			if !p.match(tComma) {
				break
			}
		}
		p.expect(tRBrace, "Expected '}' after map literal")
		return m
	case tFun:
		return &Lambda{Fn: p.funcRest("<lambda>", t.Line)}
	}
	p.pos--
	p.fail("Expected an expression")
	return nil
}

// template يقسّم `نص ${تعبير} نص` إلى أجزاء ويحلّل كل تعبير بمحلل فرعي.
func (p *parser) template(t Token) Expr {
	raw := []rune(t.Lex)
	tpl := &Template{}
	var lit strings.Builder
	flush := func() {
		if lit.Len() > 0 {
			tpl.Parts = append(tpl.Parts, TplPart{Text: lit.String()})
			lit.Reset()
		}
	}
	for i := 0; i < len(raw); i++ {
		c := raw[i]
		if c == '\\' && i+1 < len(raw) {
			i++
			switch raw[i] {
			case 'n':
				lit.WriteByte('\n')
			case 't':
				lit.WriteByte('\t')
			default:
				lit.WriteRune(raw[i])
			}
			continue
		}
		if c == '$' && i+1 < len(raw) && raw[i+1] == '{' {
			depth := 1
			j := i + 2
			for j < len(raw) && depth > 0 {
				if raw[j] == '{' {
					depth++
				} else if raw[j] == '}' {
					depth--
				}
				j++
			}
			if depth != 0 {
				panic(rtErr("E0012", t.Line, "unterminated `${` in template string"))
			}
			inner := string(raw[i+2 : j-1])
			sub := &parser{toks: Lex(inner)}
			for k := range sub.toks {
				sub.toks[k].Line += t.Line - 1
			}
			e := sub.expr()
			if !sub.at(tEOF) {
				sub.fail("unexpected token inside `${ }`")
			}
			flush()
			tpl.Parts = append(tpl.Parts, TplPart{E: e})
			i = j - 1
			continue
		}
		lit.WriteRune(c)
	}
	flush()
	return tpl
}
