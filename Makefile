SHELL := /bin/bash
ROOT := $(CURDIR)

.PHONY: all packer prepare runtime test clean

all: packer

packer:
	cmake -S vendor/packer -B build/packer -DBUILD_SHIMS=OFF -DCMAKE_BUILD_TYPE=Release
	cmake --build build/packer -j$(shell nproc)

runtime:
	scripts/build_runtime.sh

test:
	python3 -m unittest discover -s tests -v

clean:
	rm -rf build out
