package rin

type Expr interface{}
type Stmt interface{}

type (
	NumLit  struct{ V float64 }
	StrLit  struct{ V string }
	BoolLit struct{ V bool }
	NilLit  struct{}
	Ident   struct {
		Name string
		Line int
	}
	TplPart struct {
		Text string // نص حرفي إن كان E == nil
		E    Expr
	}
	Template struct{ Parts []TplPart }
	ArrayLit struct{ Elems []Expr }
	MapLit   struct {
		Keys []Expr
		Vals []Expr
	}
	Unary struct {
		Op   TokType
		X    Expr
		Line int
	}
	Binary struct {
		Op   TokType
		L, R Expr
		Line int
	}
	Logical struct {
		And  bool
		L, R Expr
	}
	Assign struct {
		Target Expr // *Ident | *Index | *Member
		Value  Expr
		Line   int
	}
	Call struct {
		Callee Expr
		Args   []Expr
		Line   int
	}
	Index struct {
		Obj, Idx Expr
		Line     int
	}
	Member struct {
		Obj  Expr
		Name string
		Line int
	}
	Lambda struct{ Fn *FuncDecl }
)

type FuncDecl struct {
	Name   string
	Params []string
	Body   *Block
	Line   int
}

type (
	LetStmt struct {
		Name string
		Init Expr // قد يكون nil
		Line int
	}
	PrintStmt struct {
		Args []Expr
		Line int
	}
	ExprStmt struct{ E Expr }
	Block    struct {
		Stmts    []Stmt
		NeedsEnv bool // فيه let/fun/class مباشرة
	}
	IfStmt struct {
		Cond Expr
		Then *Block
		Else Stmt // *Block | *IfStmt | nil
	}
	WhileStmt struct {
		Cond Expr
		Body *Block
	}
	ForStmt struct {
		Init Stmt
		Cond Expr
		Incr Expr
		Body *Block
	}
	ForInStmt struct {
		Var     string
		Declare bool
		Iter    Expr
		Body    *Block
		Line    int
	}
	BreakStmt    struct{}
	ContinueStmt struct{}
	ReturnStmt   struct{ Value Expr }
	TryStmt      struct {
		Body  *Block
		Var   string
		Catch *Block
	}
	ThrowStmt struct {
		Value Expr
		Line  int
	}
	FuncStmt  struct{ Fn *FuncDecl }
	ClassStmt struct {
		Name    string
		Base    string
		Fields  []*LetStmt
		Methods []*FuncDecl
		Line    int
	}
)
