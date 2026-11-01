// gorin — محرك Rin الثاني بلغة Go.
//
//	gorin file.rin
//	gorin --max-depth=N --budget=N file.rin
//	gorin -e 'print 1 + 2;'
//	gorin --check file.rin        (تحليل فقط)
//	gorin < file.rin
package main

import (
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"

	"github.com/rinlang/gorin/rin"
)

func usage() {
	fmt.Fprintln(os.Stderr, `gorin — محرك Rin بلغة Go (`+rin.Version+`)
  gorin [--max-depth=N] [--budget=N] file.rin [args…]
  gorin --check file.rin
  gorin [--max-depth=N] [--budget=N] -e "كود"
  gorin < file.rin
  gorin --version | --help`)
}

func printError(err error, name, src string) {
	re, ok := err.(*rin.RinError)
	if !ok {
		fmt.Fprintln(os.Stderr, "error:", err)
		return
	}
	col := re.Col
	if col < 1 {
		col = 1
	}
	fmt.Fprintf(os.Stderr, "\nerror[%s]: %s\n", re.Code, re.Msg)
	if re.Line > 0 {
		fmt.Fprintf(os.Stderr, "  --> %s:%d:%d\n", name, re.Line, col)
		lines := strings.Split(src, "\n")
		if re.Line <= len(lines) {
			fmt.Fprintf(os.Stderr, "\n %3d | %s\n", re.Line, lines[re.Line-1])
		}
	}
}

// runEmbedded ينفّذ برنامجاً مُلحَقاً من rin_ava: كل وسائط سطر الأوامر تذهب للبرنامج (المتغير args).
func runEmbedded(name, src string, kind byte) {
	if kind != 'S' {
		fmt.Fprintln(os.Stderr, "gorin: embedded bytecode is not supported by the Go engine")
		os.Exit(1)
	}
	in := rin.New(os.Stdout)
	in.SetProgramArgs(os.Args[1:])
	if err := in.Run(src); err != nil {
		printError(err, name, src)
		os.Exit(1)
	}
	os.Exit(0)
}

func main() {
	if name, src, kind, ok := selfPayload(); ok {
		runEmbedded(name, src, kind)
	}
	args := os.Args[1:]
	maxDepth, budget := 10000, int64(0)
	checkOnly := false
parseFlags:
	for len(args) > 0 {
		a := args[0]
		switch {
		case strings.HasPrefix(a, "--max-depth="):
			n, err := strconv.Atoi(strings.TrimPrefix(a, "--max-depth="))
			if err != nil || n < 1 || n > 50000 {
				fmt.Fprintln(os.Stderr, "gorin: invalid --max-depth (1..50000)")
				os.Exit(2)
			}
			maxDepth = n
		case strings.HasPrefix(a, "--budget="):
			n, err := strconv.ParseInt(strings.TrimPrefix(a, "--budget="), 10, 64)
			if err != nil || n < 0 {
				fmt.Fprintln(os.Stderr, "gorin: invalid --budget")
				os.Exit(2)
			}
			budget = n
		case a == "--check":
			checkOnly = true
		case a == "--version" || a == "-v":
			fmt.Println("gorin", rin.Version)
			return
		case a == "--help" || a == "-h":
			usage()
			return
		default:
			break parseFlags
		}
		args = args[1:]
	}
	var src, name string
	switch {
	case len(args) >= 2 && args[0] == "-e":
		src, name = args[1], "<-e>"
	case len(args) >= 1:
		b, err := os.ReadFile(args[0])
		if err != nil {
			fmt.Fprintln(os.Stderr, "gorin:", err)
			os.Exit(2)
		}
		src, name = string(b), args[0]
	default:
		b, err := io.ReadAll(os.Stdin)
		if err != nil {
			fmt.Fprintln(os.Stderr, "gorin:", err)
			os.Exit(2)
		}
		src, name = string(b), "<stdin>"
	}
	if checkOnly {
		// يحلّل فقط بصيغة قصيرة file:line:col: error: msg (يستعملها rin_ava لفحص توافق برنامج مع محرك Go)
		if err := rin.Check(src); err != nil {
			if re, ok := err.(*rin.RinError); ok {
				col := re.Col
				if col < 1 {
					col = 1
				}
				fmt.Fprintf(os.Stderr, "%s:%d:%d: error: [%s] %s\n", name, re.Line, col, re.Code, re.Msg)
			} else {
				fmt.Fprintln(os.Stderr, "error:", err)
			}
			os.Exit(1)
		}
		return
	}
	in := rin.New(os.Stdout)
	if len(args) >= 1 && args[0] != "-e" {
		in.SetProgramArgs(args[1:])
	}
	in.MaxDepth = maxDepth
	in.Budget = budget
	if err := in.Run(src); err != nil {
		printError(err, name, src)
		os.Exit(1)
	}
}
