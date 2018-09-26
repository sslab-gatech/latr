PROJ_ROOT := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))/..
BUILD_DIR := ${PROJ_ROOT}/build
SRC_DIR := ${PROJ_ROOT}/src
SCRIPTS_DIR := ${SRC_DIR}/scripts
CONF_DIR := ${SRC_DIR}/tools/config

MAKE := make --no-print-directory

# compiler settings
CC := gcc

# target settings
BUILD_MODES := Debug Release

# misc.
NLCPU := ${shell nproc}

# misc
CPU_TOPOLOGY := ${SCRIPTS_DIR}/cpu-topology.py
CPU_TOPOLOGY_MAPS := \
	${SRC_DIR}/include/util/cpu_topology_host.h \
	${SCRIPTS_DIR}/topology/cpu_topology.py

# make targets
.PHONY: all
all: make

.PHONY: cmake
cmake:
	@for MODE in $(BUILD_MODES); do ( : ; \
		mkdir -p ${BUILD_DIR}/$$MODE; \
		cd ${BUILD_DIR}/$$MODE; \
		cmake ${SRC_DIR} \
			-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
			-DCMAKE_BUILD_TYPE=$$MODE; \
	); done
	exit

.PHONY: make
make: ${CPU_TOPOLOGY_MAPS}
	@if [[ ! -e ${BUILD_DIR} ]]; then make cmake; fi
	@for MODE in $(BUILD_MODES); do ( : ; \
		echo "-----------------"; \
		echo $$MODE; \
		cd ${BUILD_DIR}/$$MODE; \
		$(MAKE)  -j${NLCPU}; \
	); done
#$(MAKE)  #-j${NLCPU}; \

${SRC_DIR}/include/util/cpu_topology_host.h:
	${CPU_TOPOLOGY} /proc/cpuinfo c > $@

${SCRIPTS_DIR}/topology/cpu_topology.py:
	mkdir -p ${SCRIPTS_DIR}/topology
	${CPU_TOPOLOGY} /proc/cpuinfo python > $@

.PHONY: clean
clean:
	@for MODE in $(BUILD_MODES); do ( : ; \
		if [ -e ${BUILD_DIR}/$$MODE/Makefile ]; \
		then \
			cd ${BUILD_DIR}/$$MODE; \
			make clean; \
		fi; \
	); done;

TAGS:
	etags `find . -type f -name '*.cc' -or -name '*.cpp'  -or -name '*.c' -or -name '*.h'`

.PHONY: cscope
cscope:
	find ${SRC_DIR} -name '*.cc'  \
	            -or -name '*.cpp' \
	            -or -name '*.c'   \
	            -or -name '*.h' > cscope.files
	cscope -b -q -R

.PHONY: distclean
distclean:
	rm -rf ${BUILD_DIR}
	rm -f  ${CPU_TOPOLOGY_MAPS}
	rm -f cscope.out tags

.PHONY: help
help:
	@cat README.md

.PHONY: format
format:
	./format.sh

