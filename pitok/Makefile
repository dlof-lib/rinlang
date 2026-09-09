# pitok/Makefile — يبني PITOK بمعزل كامل عن باقي المستودع.
# لا يعتمد على app/src/main/cpp/CMakeLists.txt ولا على أي ملف rin_*.cpp/h.

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -Iinclude

SRC := src/pitok_lexer.cpp src/pitok_parser.cpp
BIN := bin/pitok_parse

all: $(BIN)

$(BIN): tools/pitok_parse.cpp $(SRC) include/*.h
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SRC) tools/pitok_parse.cpp -o $(BIN)

test: $(BIN)
	@echo "== تشغيل على عيّنات UI حقيقية ==" && \
	for f in tests/*.pitok; do \
		echo "-- $$f --"; \
		./$(BIN) "$$f" > /dev/null || exit 1; \
	done && echo "كل الاختبارات نجحت."

e2e: tools/pitok_loom_e2e.cpp $(SRC) ../app/src/main/cpp/loom/pitok_loom_bridge.h
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -I../app/src/main/cpp $(SRC) tools/pitok_loom_e2e.cpp -o bin/pitok_loom_e2e
	./bin/pitok_loom_e2e tests/basic.pitok

# pitokc: نفس الهدف المسجَّل في app/src/main/cpp/CMakeLists.txt (انظره هناك للبناء الحقيقي
# عبر NDK) — هنا فقط لبناء/تجربة سريعة محليًا بلا cmake.
pitokc: tools/pitokc.cpp $(SRC) ../app/src/main/cpp/loom/pitok_loom_bridge.h
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -I../app/src/main/cpp $(SRC) tools/pitokc.cpp -o bin/pitokc

clean:
	rm -rf bin

.PHONY: all test e2e pitokc clean
