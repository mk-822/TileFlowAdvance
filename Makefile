TARGET := TileFlowAdvance
BUILD := build
# Prefer the pinned checkout described in the README. LIBBUTANO can point elsewhere.
BUTANO_PATHS := .deps/butano/butano butano ../Hextris-Advance/butano
LIBBUTANO ?= $(patsubst %/butano.mak,%,$(firstword $(wildcard $(addsuffix /butano.mak,$(BUTANO_PATHS)))))
PYTHON ?= python
SOURCES := src
INCLUDES := include
GRAPHICS := graphics
AUDIO := audio
AUDIOBACKEND := maxmod
DMGAUDIOBACKEND := null
ROMTITLE := TILE FLOW
ROMCODE := TFLE

ifndef LIBBUTANOABS
export LIBBUTANOABS := $(realpath $(LIBBUTANO))
endif
ifeq ($(LIBBUTANOABS),)
$(error Butano not found. See the README or set LIBBUTANO to the directory containing butano.mak)
endif

include $(LIBBUTANOABS)/butano.mak
