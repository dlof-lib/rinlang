package rin

import (
	"strconv"
	"strings"
	"unicode"
)

type TokType int

const (
	tEOF TokType = iota
	tNumber
	tString
	tTemplate
	tIdent
	tLParen
	tRParen
	tLBrace
	tRBrace
	tLBracket
	tRBracket
	tComma
	tDot
	tSemi
	tColon
	tPlus
	tMinus
	tStar
	tSlash
	tPercent
	tBang
	tBangEq
	tEq
	tEqEq
	tGt
	tGe
	tLt
	tLe
	// كلمات
	tLet
	tFun
	tReturn
	tIf
	tElse
	tWhile
	tFor
	tIn
	tBreak
	tContinue
	tTrue
	tFalse
	tNil
	tAnd
	tOr
	tTry
	tCatch
	tThrow
	tPrint
	tClass
	tExtends
	tWhen
	tOtherwise
)

var keywords = map[string]TokType{
	"let": tLet, "fun": tFun, "return": tReturn, "if": tIf, "else": tElse,
	"while": tWhile, "for": tFor, "in": tIn, "break": tBreak, "continue": tContinue,
	"true": tTrue, "false": tFalse, "nil": tNil, "and": tAnd, "or": tOr,
	"try": tTry, "catch": tCatch, "throw": tThrow, "print": tPrint, "show": tPrint,
	"extends": tExtends, "when": tWhen, "otherwise": tOtherwise,
}

type Token struct {
	T    TokType
	Lex  string
	Num  float64
	Line int
	Col  int
}

type lexer struct {
	src  []rune
	pos  int
	line int
	col  int
	toks []Token
}

func lexErr(line, col int, format string, a ...interface{}) *RinError {
	e := rtErr("E0012", line, format, a...)
	e.Col = col
	return e
}

func (lx *lexer) peek() rune {
	if lx.pos >= len(lx.src) {
		return 0
	}
	return lx.src[lx.pos]
}

func (lx *lexer) peekAt(n int) rune {
	if lx.pos+n >= len(lx.src) {
		return 0
	}
	return lx.src[lx.pos+n]
}

func (lx *lexer) adv() rune {
	r := lx.src[lx.pos]
	lx.pos++
	if r == '\n' {
		lx.line++
		lx.col = 1
	} else {
		lx.col++
	}
	return r
}

func isIdentStart(r rune) bool { return r == '_' || unicode.IsLetter(r) }
func isIdentPart(r rune) bool  { return r == '_' || unicode.IsLetter(r) || unicode.IsDigit(r) }
func isDigit(r rune) bool      { return r >= '0' && r <= '9' }

func (lx *lexer) add(t TokType, lex string, line, col int) {
	lx.toks = append(lx.toks, Token{T: t, Lex: lex, Line: line, Col: col})
}

// Lex يحوّل المصدر إلى رموز؛ يرمي *RinError (E0012) عند الخطأ.
func Lex(src string) []Token {
	lx := &lexer{src: []rune(src), line: 1, col: 1}
	for lx.pos < len(lx.src) {
		lx.scan()
	}
	lx.add(tEOF, "", lx.line, lx.col)
	return lx.toks
}

func (lx *lexer) scan() {
	line, col := lx.line, lx.col
	c := lx.adv()
	switch {
	case c == ' ' || c == '\t' || c == '\r' || c == '\n':
		return
	case c == '/' && lx.peek() == '/':
		for lx.pos < len(lx.src) && lx.peek() != '\n' {
			lx.adv()
		}
		return
	case c == '/' && lx.peek() == '*':
		lx.adv()
		for {
			if lx.pos >= len(lx.src) {
				panic(lexErr(line, col, "unterminated block comment"))
			}
			if lx.peek() == '*' && lx.peekAt(1) == '/' {
				lx.adv()
				lx.adv()
				return
			}
			lx.adv()
		}
	case isDigit(c):
		start := lx.pos - 1
		for isDigit(lx.peek()) {
			lx.adv()
		}
		if lx.peek() == '.' && isDigit(lx.peekAt(1)) {
			lx.adv()
			for isDigit(lx.peek()) {
				lx.adv()
			}
		}
		// الصيغة العلمية: تُستهلك فقط إن تبعها رقم فعلاً
		if lx.peek() == 'e' || lx.peek() == 'E' {
			n := 1
			if lx.peekAt(n) == '+' || lx.peekAt(n) == '-' {
				n++
			}
			if isDigit(lx.peekAt(n)) {
				for i := 0; i < n; i++ {
					lx.adv()
				}
				for isDigit(lx.peek()) {
					lx.adv()
				}
			}
		}
		text := string(lx.src[start:lx.pos])
		f, err := strconv.ParseFloat(text, 64)
		if err != nil {
			panic(lexErr(line, col, "invalid number `%s`", text))
		}
		lx.toks = append(lx.toks, Token{T: tNumber, Lex: text, Num: f, Line: line, Col: col})
	case c == '"' || c == '\'':
		lx.add(tString, lx.readString(c, line, col), line, col)
	case c == '`':
		lx.add(tTemplate, lx.readTemplateRaw(line, col), line, col)
	case isIdentStart(c):
		start := lx.pos - 1
		for isIdentPart(lx.peek()) {
			lx.adv()
		}
		word := string(lx.src[start:lx.pos])
		if kw, ok := keywords[word]; ok {
			lx.add(kw, word, line, col)
		} else {
			lx.add(tIdent, word, line, col)
		}
	default:
		two := func(next rune, t2, t1 TokType, l2, l1 string) {
			if lx.peek() == next {
				lx.adv()
				lx.add(t2, l2, line, col)
			} else {
				lx.add(t1, l1, line, col)
			}
		}
		switch c {
		case '(':
			lx.add(tLParen, "(", line, col)
		case ')':
			lx.add(tRParen, ")", line, col)
		case '{':
			lx.add(tLBrace, "{", line, col)
		case '}':
			lx.add(tRBrace, "}", line, col)
		case '[':
			lx.add(tLBracket, "[", line, col)
		case ']':
			lx.add(tRBracket, "]", line, col)
		case ',':
			lx.add(tComma, ",", line, col)
		case '.':
			lx.add(tDot, ".", line, col)
		case ';':
			lx.add(tSemi, ";", line, col)
		case ':':
			lx.add(tColon, ":", line, col)
		case '+':
			lx.add(tPlus, "+", line, col)
		case '-':
			lx.add(tMinus, "-", line, col)
		case '*':
			lx.add(tStar, "*", line, col)
		case '/':
			lx.add(tSlash, "/", line, col)
		case '%':
			lx.add(tPercent, "%", line, col)
		case '!':
			two('=', tBangEq, tBang, "!=", "!")
		case '=':
			two('=', tEqEq, tEq, "==", "=")
		case '>':
			two('=', tGe, tGt, ">=", ">")
		case '<':
			two('=', tLe, tLt, "<=", "<")
		default:
			panic(lexErr(line, col, "unexpected character `%c`", c))
		}
	}
}

func (lx *lexer) readString(q rune, line, col int) string {
	var sb strings.Builder
	for {
		if lx.pos >= len(lx.src) {
			panic(lexErr(line, col, "unterminated string"))
		}
		c := lx.adv()
		if c == q {
			return sb.String()
		}
		if c == '\\' {
			if lx.pos >= len(lx.src) {
				panic(lexErr(line, col, "unterminated string"))
			}
			e := lx.adv()
			switch e {
			case 'n':
				sb.WriteByte('\n')
			case 't':
				sb.WriteByte('\t')
			case 'r':
				sb.WriteByte('\r')
			case '0':
				sb.WriteByte(0)
			default:
				sb.WriteRune(e) // \\ \" \' \`
			}
			continue
		}
		sb.WriteRune(c)
	}
}

// القالب يُخزَّن خاماً (بما فيه ${...}) ويقسّمه المحلل لاحقاً.
func (lx *lexer) readTemplateRaw(line, col int) string {
	var sb strings.Builder
	depth := 0
	for {
		if lx.pos >= len(lx.src) {
			panic(lexErr(line, col, "unterminated template string"))
		}
		c := lx.adv()
		if c == '\\' && lx.pos < len(lx.src) {
			sb.WriteRune(c)
			sb.WriteRune(lx.adv())
			continue
		}
		if c == '$' && lx.peek() == '{' {
			depth++
			sb.WriteRune(c)
			sb.WriteRune(lx.adv())
			continue
		}
		if depth > 0 {
			if c == '{' {
				depth++
			} else if c == '}' {
				depth--
			}
			sb.WriteRune(c)
			continue
		}
		if c == '`' {
			return sb.String()
		}
		sb.WriteRune(c)
	}
}
