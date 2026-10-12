// Package gateway: طبقة API فوق محرك Rin الأصلي (C++).
//
// تشغّل ملف rin الحقيقي كعملية فرعية لكل طلب، وتضيف ما يصعب في C++: خادم HTTP/JSON،
// مهلة زمنية صارمة، سقف للتزامن وللمخرجات، ومصادقة بتوكن. المفسّر المدمج بـ Go (حزمة rin)
// يبقى محركاً احتياطياً عند غياب ملف rin أو عند طلبه صراحةً.
package gateway

import (
	"bytes"
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync/atomic"
	"time"

	"github.com/rinlang/gorin/rin"
)

const (
	EngineCPP = "cpp"
	EngineGo  = "go"
)

type Config struct {
	Addr          string        // مثال 127.0.0.1:8787
	RinBin        string        // مسار ملف rin الأصلي؛ فارغ = المفسّر المدمج بـ Go
	Token         string        // إلزامي إن لم يكن العنوان loopback
	Engine        string        // الافتراضي: cpp إن وُجد RinBin وإلا go
	MaxConcurrent int           // أقصى عدد تشغيلات متزامنة (الافتراضي 4)
	Timeout       time.Duration // سقف المهلة لكل طلب (الافتراضي 10s)
	MaxOutput     int           // أقصى حجم لكل من stdout وstderr بالبايت (الافتراضي 1MB)
	MaxBody       int64         // أقصى حجم لجسم الطلب (الافتراضي 1MB)
	AllowHosts    []string      // مضيفون مسموح لـ /svc/fetch بجلبهم (يدعم *.example.com)؛ فارغ = الجلب معطّل
}

func (c *Config) applyDefaults() {
	if c.Addr == "" {
		c.Addr = "127.0.0.1:8787"
	}
	if c.MaxConcurrent <= 0 {
		c.MaxConcurrent = 4
	}
	if c.Timeout <= 0 {
		c.Timeout = 10 * time.Second
	}
	if c.MaxOutput <= 0 {
		c.MaxOutput = 1 << 20
	}
	if c.MaxBody <= 0 {
		c.MaxBody = 1 << 20
	}
	if c.Engine == "" {
		if c.RinBin != "" {
			c.Engine = EngineCPP
		} else {
			c.Engine = EngineGo
		}
	}
}

// RunRequest جسم POST /run.
type RunRequest struct {
	Code      string `json:"code"`
	Engine    string `json:"engine,omitempty"`     // cpp | go
	MaxDepth  int    `json:"max_depth,omitempty"`  // 1..50000
	Budget    int64  `json:"budget,omitempty"`     // سقف عدد العبارات
	TimeoutMs int    `json:"timeout_ms,omitempty"` // لا يتجاوز Config.Timeout
	Stdin     string `json:"stdin,omitempty"`
}

type RunResponse struct {
	OK         bool   `json:"ok"`
	Engine     string `json:"engine"`
	Stdout     string `json:"stdout"`
	Stderr     string `json:"stderr"`
	ExitCode   int    `json:"exit_code"`
	DurationMs int64  `json:"duration_ms"`
	TimedOut   bool   `json:"timed_out,omitempty"`
	Truncated  bool   `json:"truncated,omitempty"`
}

type Server struct {
	cfg   Config
	sem   chan struct{}
	runs  atomic.Int64
	fails atomic.Int64
}

func New(cfg Config) *Server {
	cfg.applyDefaults()
	return &Server{cfg: cfg, sem: make(chan struct{}, cfg.MaxConcurrent)}
}

// ---------------- مخزن مخرجات بسقف ----------------

type limitedBuffer struct {
	buf       bytes.Buffer
	max       int
	truncated bool
}

// Write لا يفشل أبداً (حتى لا تتوقف العملية الفرعية على أنبوب ممتلئ)، بل يقتطع ويضع علامة.
func (l *limitedBuffer) Write(p []byte) (int, error) {
	room := l.max - l.buf.Len()
	if room <= 0 {
		l.truncated = true
		return len(p), nil
	}
	if len(p) > room {
		l.buf.Write(p[:room])
		l.truncated = true
		return len(p), nil
	}
	return l.buf.Write(p)
}

// ---------------- التنفيذ ----------------

// Execute ينفّذ طلباً واحداً بالمحرك المناسب ويحترم المهلة وسقوف الحجم.
func (s *Server) Execute(ctx context.Context, req RunRequest) RunResponse {
	engine := req.Engine
	if engine == "" {
		engine = s.cfg.Engine
	}
	timeout := s.cfg.Timeout
	if req.TimeoutMs > 0 {
		if d := time.Duration(req.TimeoutMs) * time.Millisecond; d < timeout {
			timeout = d
		}
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()

	start := time.Now()
	var resp RunResponse
	switch engine {
	case EngineCPP:
		resp = s.runCPP(ctx, req)
	default:
		resp = s.runGo(ctx, req)
	}
	resp.DurationMs = time.Since(start).Milliseconds()
	s.runs.Add(1)
	if !resp.OK {
		s.fails.Add(1)
	}
	return resp
}

func (s *Server) runCPP(ctx context.Context, req RunRequest) RunResponse {
	resp := RunResponse{Engine: EngineCPP, ExitCode: -1}
	if s.cfg.RinBin == "" {
		resp.Stderr = "gateway: no rin binary configured (use --rin or engine=go)"
		return resp
	}
	dir, err := os.MkdirTemp("", "gorin-run-*")
	if err != nil {
		resp.Stderr = "gateway: " + err.Error()
		return resp
	}
	defer os.RemoveAll(dir)
	if err := os.WriteFile(filepath.Join(dir, "main.rin"), []byte(req.Code), 0o600); err != nil {
		resp.Stderr = "gateway: " + err.Error()
		return resp
	}
	var args []string
	if req.MaxDepth > 0 {
		args = append(args, fmt.Sprintf("--max-depth=%d", req.MaxDepth))
	}
	if req.Budget > 0 {
		args = append(args, fmt.Sprintf("--budget=%d", req.Budget))
	}
	args = append(args, "main.rin")

	cmd := exec.CommandContext(ctx, s.cfg.RinBin, args...)
	cmd.Dir = dir
	out := &limitedBuffer{max: s.cfg.MaxOutput}
	errb := &limitedBuffer{max: s.cfg.MaxOutput}
	cmd.Stdout, cmd.Stderr = out, errb
	if req.Stdin != "" {
		cmd.Stdin = strings.NewReader(req.Stdin)
	}
	cmd.WaitDelay = 2 * time.Second

	runErr := cmd.Run()
	resp.Stdout, resp.Stderr = out.buf.String(), errb.buf.String()
	resp.Truncated = out.truncated || errb.truncated
	switch {
	case errors.Is(ctx.Err(), context.DeadlineExceeded):
		resp.TimedOut = true
		resp.Stderr += "\ngateway: timed out"
	case runErr == nil:
		resp.OK, resp.ExitCode = true, 0
	default:
		var ee *exec.ExitError
		if errors.As(runErr, &ee) {
			resp.ExitCode = ee.ExitCode()
		} else {
			resp.Stderr += "\ngateway: " + runErr.Error()
		}
	}
	return resp
}

// runGo: المفسّر المدمج. لا يمكن إيقافه قسراً عند المهلة، فيُحدَّد له Budget افتراضي يوقفه أخيراً.
func (s *Server) runGo(ctx context.Context, req RunRequest) RunResponse {
	resp := RunResponse{Engine: EngineGo, ExitCode: -1}
	out := &limitedBuffer{max: s.cfg.MaxOutput}
	in := rin.New(out)
	if req.MaxDepth > 0 && req.MaxDepth <= 50000 {
		in.MaxDepth = req.MaxDepth
	}
	in.Budget = req.Budget
	if in.Budget <= 0 {
		in.Budget = 50_000_000
	}
	done := make(chan error, 1)
	go func() {
		defer func() {
			if r := recover(); r != nil {
				done <- fmt.Errorf("internal error: %v", r)
			}
		}()
		done <- in.Run(req.Code)
	}()
	select {
	case err := <-done:
		resp.Stdout = out.buf.String()
		resp.Truncated = out.truncated
		if err == nil {
			resp.OK, resp.ExitCode = true, 0
			return resp
		}
		resp.ExitCode = 1
		var re *rin.RinError
		if errors.As(err, &re) {
			resp.Stderr = fmt.Sprintf("error[%s]: %s\n  --> main.rin:%d", re.Code, re.Msg, re.Line)
		} else {
			resp.Stderr = err.Error()
		}
	case <-ctx.Done():
		// لا نقرأ المخزن: المفسّر ما زال يكتب فيه (تجنّباً لسباق البيانات).
		resp.TimedOut = true
		resp.Stderr = "gateway: timed out"
	}
	return resp
}

// ---------------- HTTP ----------------

func writeJSON(w http.ResponseWriter, code int, v interface{}) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func (s *Server) auth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if s.cfg.Token != "" {
			got := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
			if subtle.ConstantTimeCompare([]byte(got), []byte(s.cfg.Token)) != 1 {
				writeJSON(w, http.StatusUnauthorized, map[string]string{"error": "unauthorized"})
				return
			}
		}
		next(w, r)
	}
}

func (s *Server) handleRun(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "use POST"})
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, s.cfg.MaxBody)
	var req RunRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid JSON body: " + err.Error()})
		return
	}
	if strings.TrimSpace(req.Code) == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "`code` is required"})
		return
	}
	if req.Engine != "" && req.Engine != EngineCPP && req.Engine != EngineGo {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "engine must be cpp or go"})
		return
	}
	if req.MaxDepth < 0 || req.MaxDepth > 50000 || req.Budget < 0 {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "max_depth must be 0..50000 and budget >= 0"})
		return
	}
	select {
	case s.sem <- struct{}{}:
		defer func() { <-s.sem }()
	case <-time.After(2 * time.Second):
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "busy: too many concurrent runs"})
		return
	case <-r.Context().Done():
		return
	}
	writeJSON(w, http.StatusOK, s.Execute(r.Context(), req))
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]interface{}{
		"status":         "ok",
		"gateway":        rin.Version,
		"default_engine": s.cfg.Engine,
		"rin_binary":     s.cfg.RinBin,
		"runs":           s.runs.Load(),
		"failures":       s.fails.Load(),
		"max_concurrent": s.cfg.MaxConcurrent,
	})
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/health", s.handleHealth)
	mux.HandleFunc("/run", s.auth(s.handleRun))
	mux.HandleFunc("/svc/", s.auth(s.handleSvc))
	return mux
}

func isLoopbackAddr(addr string) bool {
	host, _, err := net.SplitHostPort(addr)
	if err != nil || host == "" {
		return false
	}
	if host == "localhost" {
		return true
	}
	ip := net.ParseIP(host)
	return ip != nil && ip.IsLoopback()
}

// HTTPServer يبني http.Server جاهزاً؛ يرفض الاستماع خارج loopback بلا توكن.
func (s *Server) HTTPServer() (*http.Server, error) {
	if s.cfg.Token == "" && !isLoopbackAddr(s.cfg.Addr) {
		return nil, fmt.Errorf("refusing to listen on %s without --token (only loopback is allowed tokenless)", s.cfg.Addr)
	}
	log.Printf("gorin gateway: engine=%s rin=%q addr=%s concurrency=%d timeout=%s",
		s.cfg.Engine, s.cfg.RinBin, s.cfg.Addr, s.cfg.MaxConcurrent, s.cfg.Timeout)
	return &http.Server{
		Addr:              s.cfg.Addr,
		Handler:           s.Handler(),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       15 * time.Second,
		WriteTimeout:      s.cfg.Timeout + 15*time.Second,
	}, nil
}
