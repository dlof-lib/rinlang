package gateway

import (
	"context"
	"crypto/hmac"
	"crypto/md5"
	"crypto/rand"
	"crypto/sha1"
	"crypto/sha256"
	"crypto/sha512"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"hash"
	"io"
	"net/http"
	"net/url"
	"regexp"
	"strings"
	"time"
)

// خدمات الربط: يستدعيها سكربت Rin عبر apiPost/httpPost على البوابة، فيحصل على ما يصعب
// أو يخطر في C++ (تجزئات، HMAC، base64، UUID آمن، regex بلا انفجار backtracking، جلب HTTP محكوم).
//
//	apiRegister("go", "http://127.0.0.1:8787");
//	apiHeader("go", "Authorization", "Bearer TOKEN");
//	let r = apiPost("go", "/svc/hash", {"algo": "sha256", "data": "abc"});
//	print r.json["result"];

type svcError struct {
	Code int
	Msg  string
}

func (e *svcError) Error() string { return e.Msg }

func badInput(format string, a ...interface{}) error {
	return &svcError{Code: http.StatusBadRequest, Msg: fmt.Sprintf(format, a...)}
}

type svcFunc func(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error)

var services = map[string]svcFunc{
	"hash":   svcHash,
	"hmac":   svcHMAC,
	"base64": svcBase64,
	"uuid":   svcUUID,
	"random": svcRandom,
	"json":   svcJSON,
	"regex":  svcRegex,
	"time":   svcTime,
	"fetch":  svcFetch,
}

func newHash(algo string) (func() hash.Hash, error) {
	switch strings.ToLower(algo) {
	case "md5":
		return md5.New, nil
	case "sha1":
		return sha1.New, nil
	case "sha256", "":
		return sha256.New, nil
	case "sha512":
		return sha512.New, nil
	}
	return nil, badInput("algo must be md5, sha1, sha256 or sha512")
}

func decode(raw json.RawMessage, v interface{}) error {
	if len(raw) == 0 {
		raw = json.RawMessage("{}")
	}
	if err := json.Unmarshal(raw, v); err != nil {
		return badInput("invalid JSON: %v", err)
	}
	return nil
}

func svcHash(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct{ Algo, Data string }
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	nh, err := newHash(in.Algo)
	if err != nil {
		return nil, err
	}
	h := nh()
	h.Write([]byte(in.Data))
	return hex.EncodeToString(h.Sum(nil)), nil
}

func svcHMAC(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct{ Algo, Key, Data string }
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	nh, err := newHash(in.Algo)
	if err != nil {
		return nil, err
	}
	m := hmac.New(nh, []byte(in.Key))
	m.Write([]byte(in.Data))
	return hex.EncodeToString(m.Sum(nil)), nil
}

func svcBase64(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct {
		Op   string
		Data string
		URL  bool
	}
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	enc := base64.StdEncoding
	if in.URL {
		enc = base64.URLEncoding
	}
	switch in.Op {
	case "encode", "":
		return enc.EncodeToString([]byte(in.Data)), nil
	case "decode":
		b, err := enc.DecodeString(in.Data)
		if err != nil {
			return nil, badInput("invalid base64: %v", err)
		}
		return string(b), nil
	}
	return nil, badInput("op must be encode or decode")
}

func svcUUID(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return nil, err
	}
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("%x-%x-%x-%x-%x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16]), nil
}

func svcRandom(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct{ N int }
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	if in.N <= 0 {
		in.N = 16
	}
	if in.N > 1024 {
		return nil, badInput("n must be 1..1024")
	}
	b := make([]byte, in.N)
	if _, err := rand.Read(b); err != nil {
		return nil, err
	}
	return hex.EncodeToString(b), nil
}

func svcJSON(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct{ Op, Data string }
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	var v interface{}
	if err := json.Unmarshal([]byte(in.Data), &v); err != nil {
		return nil, badInput("`data` is not valid JSON: %v", err)
	}
	var out []byte
	var err error
	switch in.Op {
	case "pretty", "":
		out, err = json.MarshalIndent(v, "", "  ")
	case "minify":
		out, err = json.Marshal(v)
	default:
		return nil, badInput("op must be pretty or minify")
	}
	if err != nil {
		return nil, err
	}
	return string(out), nil
}

// regex: محرك RE2 في Go — زمن خطّي مضمون، فلا خطر ReDoS.
func svcRegex(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct{ Op, Pattern, Text, Repl string }
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	re, err := regexp.Compile(in.Pattern)
	if err != nil {
		return nil, badInput("invalid pattern: %v", err)
	}
	switch in.Op {
	case "match", "":
		return re.MatchString(in.Text), nil
	case "find":
		loc := re.FindStringIndex(in.Text)
		if loc == nil {
			return nil, nil
		}
		return in.Text[loc[0]:loc[1]], nil
	case "findall":
		all := re.FindAllString(in.Text, -1)
		if all == nil {
			all = []string{}
		}
		return all, nil
	case "replace":
		return re.ReplaceAllString(in.Text, in.Repl), nil
	case "split":
		return re.Split(in.Text, -1), nil
	}
	return nil, badInput("op must be match, find, findall, replace or split")
}

func svcTime(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	now := time.Now()
	return map[string]interface{}{
		"unix_ms": now.UnixMilli(),
		"utc":     now.UTC().Format(time.RFC3339),
	}, nil
}

// ---- fetch: جلب HTTP محكوم بقائمة سماح (يمنع SSRF) ----

func (s *Server) hostAllowed(host string) bool {
	host = strings.ToLower(host)
	for _, a := range s.cfg.AllowHosts {
		a = strings.ToLower(a)
		if a == host {
			return true
		}
		if strings.HasPrefix(a, "*.") && strings.HasSuffix(host, a[1:]) {
			return true
		}
	}
	return false
}

func svcFetch(s *Server, ctx context.Context, raw json.RawMessage) (interface{}, error) {
	var in struct {
		URL       string
		Method    string
		Headers   map[string]string
		Body      string
		TimeoutMs int `json:"timeout_ms"`
	}
	if err := decode(raw, &in); err != nil {
		return nil, err
	}
	if len(s.cfg.AllowHosts) == 0 {
		return nil, &svcError{Code: http.StatusForbidden, Msg: "fetch is disabled: start the gateway with --allow-host"}
	}
	u, err := url.Parse(in.URL)
	if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Hostname() == "" {
		return nil, badInput("`url` must be an absolute http(s) URL")
	}
	if !s.hostAllowed(u.Hostname()) {
		return nil, &svcError{Code: http.StatusForbidden, Msg: "host not allowed: " + u.Hostname()}
	}
	method := strings.ToUpper(in.Method)
	if method == "" {
		method = http.MethodGet
	}
	timeout := s.cfg.Timeout
	if in.TimeoutMs > 0 {
		if d := time.Duration(in.TimeoutMs) * time.Millisecond; d < timeout {
			timeout = d
		}
	}
	ctx, cancel := context.WithTimeout(ctx, timeout)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, method, in.URL, strings.NewReader(in.Body))
	if err != nil {
		return nil, badInput("bad request: %v", err)
	}
	for k, v := range in.Headers {
		req.Header.Set(k, v)
	}
	client := &http.Client{
		CheckRedirect: func(r *http.Request, via []*http.Request) error {
			if len(via) >= 5 {
				return errors.New("too many redirects")
			}
			if !s.hostAllowed(r.URL.Hostname()) {
				return errors.New("redirect to a host that is not allowed: " + r.URL.Hostname())
			}
			return nil
		},
	}
	resp, err := client.Do(req)
	if err != nil {
		return nil, &svcError{Code: http.StatusBadGateway, Msg: err.Error()}
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 2<<20))
	headers := map[string]string{}
	for k := range resp.Header {
		headers[k] = resp.Header.Get(k)
	}
	return map[string]interface{}{"status": resp.StatusCode, "headers": headers, "body": string(body)}, nil
}

// ---- المعالج ----

func (s *Server) handleSvc(w http.ResponseWriter, r *http.Request) {
	name := strings.TrimPrefix(r.URL.Path, "/svc/")
	fn, ok := services[name]
	if !ok {
		writeJSON(w, http.StatusNotFound, map[string]interface{}{"ok": false, "error": "unknown service: " + name})
		return
	}
	var raw json.RawMessage
	if r.Method == http.MethodPost {
		r.Body = http.MaxBytesReader(w, r.Body, s.cfg.MaxBody)
		b, err := io.ReadAll(r.Body)
		if err != nil {
			writeJSON(w, http.StatusBadRequest, map[string]interface{}{"ok": false, "error": "cannot read body: " + err.Error()})
			return
		}
		if len(strings.TrimSpace(string(b))) > 0 {
			raw = b
		}
	} else if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]interface{}{"ok": false, "error": "use GET or POST"})
		return
	}
	res, err := fn(s, r.Context(), raw)
	if err != nil {
		code := http.StatusInternalServerError
		var se *svcError
		if errors.As(err, &se) {
			code = se.Code
		}
		writeJSON(w, code, map[string]interface{}{"ok": false, "error": err.Error()})
		return
	}
	writeJSON(w, http.StatusOK, map[string]interface{}{"ok": true, "result": res})
}
