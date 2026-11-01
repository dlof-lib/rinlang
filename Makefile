# Thin convenience wrapper around scripts/build_all.sh — see that file for what each target
# actually does (Gradle for apk, Emscripten for web, g++/X11 for desktop) and for the exact
# per-target requirements. `make help` (or no target) prints usage.

SHELL := /usr/bin/env bash

.PHONY: help apk apk-release web desktop all clean

help:
	@bash scripts/build_all.sh

apk:
	@bash scripts/build_all.sh apk debug

apk-release:
	@bash scripts/build_all.sh apk release

web:
	@bash scripts/build_all.sh web

desktop:
	@bash scripts/build_all.sh desktop

all:
	@bash scripts/build_all.sh all debug

clean:
	@bash scripts/build_all.sh clean
