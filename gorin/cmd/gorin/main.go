// gorin — طبقة API فوق محرك Rin الأصلي.
//
//	gorin serve [--addr 127.0.0.1:8787] [--rin /path/rin] [--token T] [--timeout 10s]
//	gorin run   [--engine cpp|go] [--rin /path/rin] file.rin
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"time"

	"github.com/rinlang/gorin/gateway"
)

type listFlag []string

func (l *listFlag) String() string     { return fmt.Sprint([]string(*l)) }
func (l *listFlag) Set(v string) error { *l = append(*l, v); return nil }

func findRin(flagVal string) string {
	if flagVal != "" {
		return flagVal
	}
	if v := os.Getenv("RIN_BIN"); v != "" {
		return v
	}
	if p, err := exec.LookPath("rin"); err == nil {
		return p
	}
	return ""
}

func usage() {
	fmt.Fprintln(os.Stderr, `gorin — طبقة API فوق محرك Rin (C++)
  gorin serve [--addr A] [--rin PATH] [--token T] [--engine cpp|go]
              [--max-concurrent N] [--timeout 10s] [--max-output BYTES]
              [--allow-host HOST]...
  gorin run   [--engine cpp|go] [--rin PATH] [--max-depth N] [--budget N] file.rin`)
}

func main() {
	if len(os.Args) < 2 {
		usage()
		os.Exit(2)
	}
	switch os.Args[1] {
	case "serve":
		serve(os.Args[2:])
	case "run":
		runFile(os.Args[2:])
	default:
		usage()
		os.Exit(2)
	}
}

func serve(args []string) {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	addr := fs.String("addr", "127.0.0.1:8787", "عنوان الاستماع")
	rinBin := fs.String("rin", "", "مسار ملف rin (الافتراضي: RIN_BIN أو PATH)")
	token := fs.String("token", os.Getenv("GORIN_TOKEN"), "توكن Bearer (أو GORIN_TOKEN)")
	engine := fs.String("engine", "", "cpp | go (الافتراضي: cpp إن وُجد rin)")
	conc := fs.Int("max-concurrent", 4, "أقصى تشغيلات متزامنة")
	timeout := fs.Duration("timeout", 10*time.Second, "أقصى مهلة لكل طلب")
	maxOut := fs.Int("max-output", 1<<20, "أقصى حجم مخرجات بالبايت")
	var allow listFlag
	fs.Var(&allow, "allow-host", "مضيف مسموح لـ /svc/fetch (يتكرر؛ يدعم *.example.com)")
	fs.Parse(args)

	srv := gateway.New(gateway.Config{
		AllowHosts: allow, Addr: *addr, RinBin: findRin(*rinBin), Token: *token, Engine: *engine,
		MaxConcurrent: *conc, Timeout: *timeout, MaxOutput: *maxOut,
	})
	hs, err := srv.HTTPServer()
	if err != nil {
		fmt.Fprintln(os.Stderr, "gorin:", err)
		os.Exit(2)
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt)
	defer stop()
	go func() {
		<-ctx.Done()
		sc, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		hs.Shutdown(sc)
	}()
	if err := hs.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		fmt.Fprintln(os.Stderr, "gorin:", err)
		os.Exit(1)
	}
}

func runFile(args []string) {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	rinBin := fs.String("rin", "", "مسار ملف rin")
	engine := fs.String("engine", "", "cpp | go")
	depth := fs.Int("max-depth", 0, "حد عمق الاستدعاء")
	budget := fs.Int64("budget", 0, "سقف عدد العبارات")
	timeout := fs.Duration("timeout", 30*time.Second, "المهلة")
	fs.Parse(args)
	if fs.NArg() != 1 {
		usage()
		os.Exit(2)
	}
	src, err := os.ReadFile(fs.Arg(0))
	if err != nil {
		fmt.Fprintln(os.Stderr, "gorin:", err)
		os.Exit(2)
	}
	srv := gateway.New(gateway.Config{RinBin: findRin(*rinBin), Engine: *engine, Timeout: *timeout})
	resp := srv.Execute(context.Background(), gateway.RunRequest{Code: string(src), MaxDepth: *depth, Budget: *budget})
	fmt.Print(resp.Stdout)
	if resp.Stderr != "" {
		fmt.Fprintln(os.Stderr, resp.Stderr)
	}
	if !resp.OK {
		code := resp.ExitCode
		if code <= 0 {
			code = 1
		}
		os.Exit(code)
	}
}
