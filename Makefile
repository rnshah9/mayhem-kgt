.MAKEFLAGS: -r -m share/mk

# targets
all::  mkdir .WAIT dep .WAIT prog
dep::
gen::
test::
install:: all
clean::

# things to override
CC     ?= gcc
BUILD  ?= build
PREFIX ?= /usr/local

# layout
SUBDIR += src/bnf
SUBDIR += src/dot
SUBDIR += src/abnf
SUBDIR += src/ebnf
SUBDIR += src/rbnf
SUBDIR += src/sid
SUBDIR += src/wsn
SUBDIR += src/rrd
SUBDIR += src/rrdump
SUBDIR += src/rrparcon
SUBDIR += src/rrta
SUBDIR += src/rrtext
SUBDIR += src/rrdot
SUBDIR += src

#INCDIR += include

# TODO: centralise
#DIR += ${BUILD}/bin
DIR += ${BUILD}/lib

.include <subdir.mk>
.include <sid.mk>
.include <lx.mk>
.include <obj.mk>
.include <dep.mk>
.include <ar.mk>
.include <so.mk>
.include <part.mk>
.include <prog.mk>
.include <mkdir.mk>
.include <install.mk>
.include <clean.mk>

