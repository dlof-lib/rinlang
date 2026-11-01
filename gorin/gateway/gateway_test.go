package gateway

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

func post(t *testing.T, url, token string, body interface{}) (*http.Response, RunResponse) {
	t.Helper()
	b, _ := json.Marshal(body)
	req, _ := http.NewRequest(http.MethodPost, url+"/run", bytes.NewReader(b))
	if token != "" {
		req.Header.Set("Authorization", "Bearer "+token)
	}
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	var rr RunResponse
	json.NewDecoder(res.Body).Decode(&rr)
	return res, rr
}

func TestRunGoEngine(t *testing.T) {
	ts := httptest.NewServer(New(Config{Engine: EngineGo}).Handler())
	defer ts.Close()
	res, rr := post(t, ts.URL, "", map[string]interface{}{"code": "print 1 + 2;"})
	if res.StatusCode != 200 || !rr.OK || strings.TrimSpace(rr.Stdout) != "3" {
		t.Fatalf("status=%d resp=%+v", res.StatusCode, rr)
	}
}

func TestRuntimeErrorIsReported(t *testing.T) {
	ts := httptest.NewServer(New(Config{Engine: EngineGo}).Handler())
	defer ts.Close()
	_, rr := post(t, ts.URL, "", map[string]interface{}{"code": "print x;"})
	if rr.OK || rr.ExitCode != 1 || !strings.Contains(rr.Stderr, "E0001") {
		t.Fatalf("resp=%+v", rr)
	}
}

func TestAuth(t *testing.T) {
	ts := httptest.NewServer(New(Config{Engine: EngineGo, Token: "s3cret"}).Handler())
	defer ts.Close()
	res, _ := post(t, ts.URL, "", map[string]interface{}{"code": "print 1;"})
	if res.StatusCode != http.StatusUnauthorized {
		t.Fatalf("want 401, got %d", res.StatusCode)
	}
	res, rr := post(t, ts.URL, "s3cret", map[string]interface{}{"code": "print 1;"})
	if res.StatusCode != 200 || !rr.OK {
		t.Fatalf("want ok with token, got %d %+v", res.StatusCode, rr)
	}
}

func TestTimeout(t *testing.T) {
	ts := httptest.NewServer(New(Config{Engine: EngineGo}).Handler())
	defer ts.Close()
	_, rr := post(t, ts.URL, "", map[string]interface{}{
		"code": "while (true) { }", "timeout_ms": 150, "budget": 40000000,
	})
	if !rr.TimedOut || rr.OK {
		t.Fatalf("expected timeout, got %+v", rr)
	}
}

func TestBadRequests(t *testing.T) {
	ts := httptest.NewServer(New(Config{Engine: EngineGo}).Handler())
	defer ts.Close()
	res, _ := post(t, ts.URL, "", map[string]interface{}{"code": ""})
	if res.StatusCode != 400 {
		t.Fatalf("empty code: want 400, got %d", res.StatusCode)
	}
	res, _ = post(t, ts.URL, "", map[string]interface{}{"code": "print 1;", "engine": "x"})
	if res.StatusCode != 400 {
		t.Fatalf("bad engine: want 400, got %d", res.StatusCode)
	}
}

func TestRefusesPublicBindWithoutToken(t *testing.T) {
	if _, err := New(Config{Addr: "0.0.0.0:8787"}).HTTPServer(); err == nil {
		t.Fatal("expected refusal")
	}
	if _, err := New(Config{Addr: "127.0.0.1:8787"}).HTTPServer(); err != nil {
		t.Fatal(err)
	}
}

// محرك cpp: نستبدل rin بسكربت يطبع وسائطه وملف المصدر للتحقق من التمرير.
func TestCPPEngineArgsPassing(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("sh required")
	}
	dir := t.TempDir()
	fake := filepath.Join(dir, "rin")
	os.WriteFile(fake, []byte("#!/bin/sh\necho \"args: $@\"\ncat main.rin\n"), 0o755)
	ts := httptest.NewServer(New(Config{RinBin: fake}).Handler())
	defer ts.Close()
	_, rr := post(t, ts.URL, "", map[string]interface{}{"code": "print 7;", "max_depth": 5, "budget": 99})
	if !rr.OK || rr.Engine != EngineCPP ||
		!strings.Contains(rr.Stdout, "--max-depth=5 --budget=99 main.rin") ||
		!strings.Contains(rr.Stdout, "print 7;") {
		t.Fatalf("resp=%+v", rr)
	}
}

func svcCall(t *testing.T, url, name string, body interface{}) (int, map[string]interface{}) {
	t.Helper()
	b, _ := json.Marshal(body)
	res, err := http.Post(url+"/svc/"+name, "application/json", bytes.NewReader(b))
	if err != nil {
		t.Fatal(err)
	}
	defer res.Body.Close()
	var out map[string]interface{}
	json.NewDecoder(res.Body).Decode(&out)
	return res.StatusCode, out
}

func TestServices(t *testing.T) {
	ts := httptest.NewServer(New(Config{Engine: EngineGo}).Handler())
	defer ts.Close()
	cases := []struct {
		name string
		body map[string]interface{}
		want interface{}
	}{
		{"hash", map[string]interface{}{"algo": "sha256", "data": "abc"}, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
		{"hmac", map[string]interface{}{"algo": "sha256", "key": "key", "data": "The quick brown fox jumps over the lazy dog"}, "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"},
		{"base64", map[string]interface{}{"op": "encode", "data": "hello"}, "aGVsbG8="},
		{"base64", map[string]interface{}{"op": "decode", "data": "aGVsbG8="}, "hello"},
		{"regex", map[string]interface{}{"op": "replace", "pattern": "[0-9]+", "text": "a1b22", "repl": "#"}, "a#b#"},
		{"regex", map[string]interface{}{"op": "match", "pattern": "^a.c$", "text": "abc"}, true},
	}
	for _, c := range cases {
		code, out := svcCall(t, ts.URL, c.name, c.body)
		if code != 200 || out["result"] != c.want {
			t.Errorf("%s %v: code=%d out=%v want %v", c.name, c.body, code, out, c.want)
		}
	}
	code, out := svcCall(t, ts.URL, "uuid", map[string]interface{}{})
	if s, _ := out["result"].(string); code != 200 || len(s) != 36 {
		t.Errorf("uuid: %v", out)
	}
	if code, _ := svcCall(t, ts.URL, "hash", map[string]interface{}{"algo": "nope"}); code != 400 {
		t.Errorf("bad algo should be 400, got %d", code)
	}
	if code, _ := svcCall(t, ts.URL, "nothing", map[string]interface{}{}); code != 404 {
		t.Errorf("unknown service should be 404, got %d", code)
	}
}

func TestFetchIsAllowlisted(t *testing.T) {
	backend := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte("hello from backend"))
	}))
	defer backend.Close()

	closed := httptest.NewServer(New(Config{Engine: EngineGo}).Handler())
	defer closed.Close()
	if code, _ := svcCall(t, closed.URL, "fetch", map[string]interface{}{"url": backend.URL}); code != 403 {
		t.Fatalf("fetch without allowlist must be 403, got %d", code)
	}

	open := httptest.NewServer(New(Config{Engine: EngineGo, AllowHosts: []string{"127.0.0.1"}}).Handler())
	defer open.Close()
	code, out := svcCall(t, open.URL, "fetch", map[string]interface{}{"url": backend.URL})
	res, _ := out["result"].(map[string]interface{})
	if code != 200 || res["body"] != "hello from backend" {
		t.Fatalf("code=%d out=%v", code, out)
	}
	if code, _ := svcCall(t, open.URL, "fetch", map[string]interface{}{"url": "http://example.org/"}); code != 403 {
		t.Fatalf("non-allowed host must be 403, got %d", code)
	}
}
