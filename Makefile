SHELL := $(shell command -v bash)
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD := $(ROOT)build
WEIGHTS := $(ROOT)weights

.PHONY: all fixture reference test test-syntax bootstrap doctor corpus trainer train quick upstream-smoke clean package

all: test

$(BUILD):
	mkdir -p $@

fixture: $(BUILD)/mkfixture $(BUILD)/mkfixture_prod $(WEIGHTS)/fixture.bfw $(WEIGHTS)/fixture-prod.bfw

$(BUILD)/mkfixture: tools/mkfixture.c | $(BUILD)
	$(CC) $(CFLAGS) $< -lm -o $@

$(BUILD)/mkfixture_prod: tools/mkfixture_prod.c tools/mkfixture.c | $(BUILD)
	$(CC) $(CFLAGS) tools/mkfixture_prod.c -lm -o $@

$(WEIGHTS)/fixture.bfw: $(BUILD)/mkfixture
	mkdir -p $(WEIGHTS)
	$(BUILD)/mkfixture $@

$(WEIGHTS)/fixture-prod.bfw: $(BUILD)/mkfixture_prod
	mkdir -p $(WEIGHTS)
	$(BUILD)/mkfixture_prod $@

reference: $(BUILD)/reference

$(BUILD)/reference: tools/reference.c | $(BUILD)
	$(CC) $(CFLAGS) -Werror $< -o $@

test-syntax:
	bash -n bashformer.sh bootstrap.sh data/fetch_dracula.sh tools/notorch_flags.sh \
	  tests/parity.sh tests/method.sh tests/bootstrap.sh tests/forge.sh tests/stateful.sh tests/dynamics.sh tests/spa.sh tests/doctor.sh tests/run_all.sh
	$(CC) -Itests/stub -std=c11 -Wall -Wextra -Werror -fsyntax-only src/train.c tools/notorch_doctor.c

test: fixture reference test-syntax
	bash tests/run_all.sh

bootstrap:
	./bootstrap.sh

$(BUILD)/notorch-doctor: tools/notorch_doctor.c | $(BUILD)
	@prefix="$${BASHFORMER_PREFIX:-$${HOME}/.local}"; \
	  test -f "$$prefix/lib/libnotorch.a" || \
	  { echo 'notorch missing: run make bootstrap'; exit 1; }
	$(CC) $(CFLAGS) $< $$(./tools/notorch_flags.sh cflags) $$(./tools/notorch_flags.sh libs) -o $@

doctor: $(BUILD)/notorch-doctor
	@echo "notorch backend: $$(./tools/notorch_flags.sh backend)"
	@echo "notorch ref:     $$(./tools/notorch_flags.sh ref)"
	$(BUILD)/notorch-doctor

corpus:
	./data/fetch_dracula.sh

trainer: $(BUILD)/bashformer-train

$(BUILD)/bashformer-train: src/train.c | $(BUILD)
	@prefix="$${BASHFORMER_PREFIX:-$${HOME}/.local}"; \
	  test -f "$$prefix/lib/libnotorch.a" || \
	  { echo 'notorch missing: run make bootstrap'; exit 1; }
	$(CC) $(CFLAGS) $< $$(./tools/notorch_flags.sh cflags) $$(./tools/notorch_flags.sh libs) -o $(BUILD)/bashformer-train

train: trainer
	$(BUILD)/bashformer-train --corpus data/dracula.txt --out weights/bashformer \
	  --steps $${STEPS:-20000} --lr $${LR:-3e-4}

quick: trainer
	$(BUILD)/bashformer-train --corpus data/tiny.txt --out weights/quick \
	  --steps $${STEPS:-20} --lr $${LR:-1e-3}

upstream-smoke: doctor trainer
	$(BUILD)/bashformer-train --corpus data/tiny.txt --out weights/upstream-smoke \
	  --steps $${STEPS:-20} --lr $${LR:-1e-3}
	./bashformer.sh --weights weights/upstream-smoke.bfw --prompt 'The blood was ' \
	  --tokens 4 --generated-only

clean:
	rm -rf $(BUILD)/*
	rm -f $(WEIGHTS)/*.bfw $(WEIGHTS)/*.bin $(WEIGHTS)/*.meta

package: test
	@tmp=$$(mktemp -d); trap 'rm -rf -- "$$tmp"' EXIT; \
	  mkdir -p "$$tmp/bashformer"; \
	  tar --exclude=.git --exclude=build --exclude='weights/*.bfw' \
	      --exclude='weights/*.bin' --exclude='weights/*.meta' \
	      --exclude='bashformer-src.tar.gz' -cf - . | \
	      tar -C "$$tmp/bashformer" -xf -; \
	  tar -C "$$tmp" -czf "$(ROOT)bashformer-src.tar.gz" bashformer; \
	  echo "wrote $(ROOT)bashformer-src.tar.gz"
