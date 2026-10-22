# Self-Organizing Data Structure -- Adaptive Data Lab
# Author: Asutosh Sahoo | Veer Surendra Sai University of Technology
CC      ?= gcc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDLIBS  := -lm
SRC     := src/list.c src/workload.c src/metrics.c
BUILD   := build

all: $(BUILD)/adl

$(BUILD)/adl: $(SRC) src/main.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) src/main.c -o $@ $(LDLIBS)

$(BUILD)/test_correctness: $(SRC) tests/test_correctness.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) $(SRC) tests/test_correctness.c -o $@ $(LDLIBS)

$(BUILD):
	mkdir -p $(BUILD)

test: $(BUILD)/test_correctness
	./$(BUILD)/test_correctness

demo: $(BUILD)/adl
	./$(BUILD)/adl demo

bench: $(BUILD)/adl
	./$(BUILD)/adl bench

clean:
	rm -rf $(BUILD) results.csv timeline.csv

.PHONY: all test demo bench clean
