package rin

import "fmt"

// RinError: خطأ وقت تشغيل/تحليل بنفس أكواد المحرك الأصلي (E0001, E0004, E0012, E0035 ...).
type RinError struct {
	Code string
	Msg  string
	Line int
	Col  int
}

func (e *RinError) Error() string { return fmt.Sprintf("[%s] %s", e.Code, e.Msg) }

// Thrown: قيمة رماها المستخدم بـ throw.
type Thrown struct {
	Val  Value
	Line int
}

func rtErr(code string, line int, format string, a ...interface{}) *RinError {
	return &RinError{Code: code, Msg: fmt.Sprintf(format, a...), Line: line}
}
