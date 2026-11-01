package rin

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func run(t *testing.T, src string) (string, error) {
	t.Helper()
	var buf bytes.Buffer
	in := New(&buf)
	err := in.Run(src)
	return buf.String(), err
}

// كل testdata/*.rin يُقارَن حرفياً بـ .expected
func TestGolden(t *testing.T) {
	files, _ := filepath.Glob("../testdata/*.rin")
	if len(files) == 0 {
		t.Fatal("no testdata found")
	}
	for _, f := range files {
		f := f
		t.Run(filepath.Base(f), func(t *testing.T) {
			src, _ := os.ReadFile(f)
			want, err := os.ReadFile(strings.TrimSuffix(f, ".rin") + ".expected")
			if err != nil {
				t.Skip("no .expected")
			}
			got, rerr := run(t, string(src))
			if rerr != nil {
				t.Fatalf("runtime error: %v", rerr)
			}
			if strings.TrimRight(got, "\n") != strings.TrimRight(string(want), "\n") {
				t.Fatalf("output mismatch\n--- got ---\n%s\n--- want ---\n%s", got, want)
			}
		})
	}
}

func TestDepthLimitIsCatchable(t *testing.T) {
	out, err := run(t, "fun f(n) { return f(n + 1); }\ntry { f(0); } catch (e) { print \"caught\"; }\n")
	if err != nil || strings.TrimSpace(out) != "caught" {
		t.Fatalf("out=%q err=%v", out, err)
	}
}

func TestDeepRecursionWorks(t *testing.T) {
	var buf bytes.Buffer
	in := New(&buf)
	in.MaxDepth = 50000
	if err := in.Run("fun d(n) { if (n == 0) { return 0; } return 1 + d(n - 1); }\nprint d(49990);\n"); err != nil {
		t.Fatal(err)
	}
	if strings.TrimSpace(buf.String()) != "49990" {
		t.Fatalf("got %q", buf.String())
	}
}

func TestBudgetStopsInfiniteLoop(t *testing.T) {
	var buf bytes.Buffer
	in := New(&buf)
	in.Budget = 10000
	err := in.Run("while (true) { }")
	if err == nil || !strings.Contains(err.Error(), "Execution limit") {
		t.Fatalf("expected execution limit error, got %v", err)
	}
}

func TestErrors(t *testing.T) {
	cases := map[string]string{
		"print x;":         "E0001",
		"print 1 +;":       "E0012",
		"print \"a\" - 1;": "E0004",
		"print [1][5];":    "E0021",
	}
	for src, code := range cases {
		_, err := run(t, src)
		re, ok := err.(*RinError)
		if !ok || re.Code != code {
			t.Errorf("%q: want %s, got %v", src, code, err)
		}
	}
}
