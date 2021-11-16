#===============================================================================
# Moruga project
#===============================================================================
# Copyright (c) 2019-2021 Marwijn Hessel
#
# Moruga is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Moruga is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING3.
# If not, see <https://www.gnu.org/licenses/>
#===============================================================================

PROJECT_NAME := Moruga

#===============================================================================
# environment detection
#===============================================================================

UNAME := $(shell uname)

#===============================================================================
# source directories
#===============================================================================

SOURCE_DIRS := src src/filters

#===============================================================================
# include directories
#===============================================================================

INCLUDE_DIRS := src

#===============================================================================
# library directories
#===============================================================================

LIB_DIRS :=

#===============================================================================
# libraries
#===============================================================================

ifeq ($(UNAME), Linux)
  LIBS := z
else
  LIBS := psapi z
endif

#===============================================================================
# build artifacts
#===============================================================================

ifeq ($(UNAME), Linux)
  BIN_FILE := $(PROJECT_NAME)
else
  BIN_FILE := $(PROJECT_NAME).exe
endif

LSS_FILE    := $(PROJECT_NAME).lss
PROFILE_DIR := Profile

#===============================================================================
# toolchain
#===============================================================================

ifeq ($(TOOLCHAIN),llvm)
  CC      := clang
  CXX     := clang++
  OBJDUMP := llvm-objdump
else
  CC      := gcc
  CXX     := g++
  OBJDUMP := objdump
endif

#===============================================================================
# output directories
#===============================================================================

ifeq ($(MODE),debug)
  BUILD_DIR := Debug
else
  BUILD_DIR := Release
endif

#===============================================================================
# c++ compiler flags
#===============================================================================

CXXFLAGS := -std=c++20 -fdata-sections -ffunction-sections

#===============================================================================
# c\c++ compiler flags
#===============================================================================

CCFLAGS := -m64 -MMD -mno-ms-bitfields -march=native -mtune=native -pthread

ifeq ($(MODE),debug)
  CCFLAGS += -g3 -O0
  ifeq ($(UNAME), Linux)
    CCFLAGS += -fsanitize=address -fsanitize=undefined
  else
    CCFLAGS += -fstack-protector-strong
  endif
else
  CCFLAGS += -O3 -flto -fomit-frame-pointer -fno-rtti -funroll-loops -ftree-vectorize
  ifeq ($(MODE),profile)
    CCFLAGS += -fprofile-generate=$(PROFILE_DIR)
  else
    ifeq ($(MODE),guided)
      CCFLAGS += -fprofile-use=$(PROFILE_DIR)
    endif
  endif
endif

ifeq ($(TOOLCHAIN),llvm)
  CCFLAGS += -Weverything \
             -Wno-c++98-compat \
             -Wno-c++98-compat-pedantic \
             -Wno-covered-switch-default \
             -Wno-exit-time-destructors \
             -Wno-four-char-constants \
             -Wno-global-constructors \
             -Wno-gnu-anonymous-struct \
             -Wno-multichar \
             -Wno-nested-anon-types \
             -Wno-reserved-id-macro \
             -Wno-unknown-attributes \
             -Wno-used-but-marked-unused
else
  CCFLAGS += -Wall \
             -Wcast-align \
             -Wcast-qual \
             -Wchar-subscripts \
             -Wconversion \
             -Wdisabled-optimization \
             -Wduplicated-branches \
             -Wduplicated-cond \
             -Wextra \
             -Wfloat-equal \
             -Wformat \
             -Wformat-nonliteral \
             -Wformat-security \
             -Wformat-y2k \
             -Wformat=2 \
             -Wimport \
             -Winit-self \
             -Winvalid-pch \
             -Wlogical-op \
             -Wmissing-braces \
             -Wmissing-declarations \
             -Wmissing-format-attribute \
             -Wmissing-include-dirs \
             -Wmissing-noreturn \
             -Wno-multichar \
             -Wno-pragmas \
             -Wno-unsafe-loop-optimizations \
             -Wnull-dereference \
             -Wpacked \
             -Wpointer-arith \
             -Wredundant-decls \
             -Wshadow \
             -Wsign-conversion \
             -Wstack-protector \
             -Wswitch-default \
             -Wswitch-enum \
             -Wundef \
             -Wuninitialized \
             -Wuseless-cast \
             -Wwrite-strings
endif

#===============================================================================
# pre-processor defines
#===============================================================================

DEFINES := _GNU_SOURCE WIN32_LEAN_AND_MEAN _CRT_DISABLE_PERFCRIT_LOCKS _CRT_SECURE_NO_WARNINGS

ifeq ($(MODE),debug)
  DEFINES += _DEBUG
else
  DEFINES += NDEBUG
endif

#===============================================================================
# Include What You Use handling
#===============================================================================

ifeq ($(MODE),iwyu)
  CCFLAGS := -w
  CXXFLAGS := -Xiwyu --no_comment -std=c++20 -m64 -ggdb -O1
endif

#===============================================================================
# linker flags
#===============================================================================

LDFLAGS := -Wl,--gc-sections

ifeq ($(TOOLCHAIN),llvm)
  ifneq ($(MODE),debug)
    LDFLAGS += -fuse-ld=lld
  endif
endif

ifneq ($(UNAME), Linux)
  LDFLAGS += -static src/$(PROJECT_NAME).res
endif

ifneq ($(MODE),debug)
  ifneq ($(MODE),profile)
    LDFLAGS += -s
  endif 
endif

#===============================================================================
# various commands
#===============================================================================

CD    := @cd
CP    := @cp
ECHO  := @echo
MKDIR := @mkdir -p
RM    := @rm -rf
TIDY  := clang-tidy

#===============================================================================
# list all sources, objects & dependencies
#===============================================================================

CPPSOURCES := $(wildcard $(patsubst %,%/*.cpp,$(SOURCE_DIRS)))
OBJECTS    := $(CPPSOURCES:%.cpp=%.o)
OBJECTS    := $(subst ../,,$(OBJECTS))
OBJECTS    := $(subst ./,,$(OBJECTS))
OBJECTS    := $(addprefix $(BUILD_DIR)/,$(OBJECTS))
DEPS       := $(OBJECTS:%.o=%.d)

#===============================================================================
# add prefixes
#===============================================================================

_INCLUDE_DIRS := $(patsubst %,-I%,$(INCLUDE_DIRS))
_DEFINES      := $(patsubst %,-D%,$(DEFINES))
_LIBS         := $(patsubst %,-l%,$(LIBS))
_LIB_DIRS     := $(patsubst %,-L%,$(LIB_DIRS))

#===============================================================================
# include the completion files
#===============================================================================

ifeq ($(MAKECMDGOALS),$(BUILD_DIR)/$(BIN_FILE))
	-include $(DEPS)
endif

#===============================================================================
# build targets
#===============================================================================
.PHONY : all guided clean

#===============================================================================
# build the application
#===============================================================================
all:
	$(MAKE) mkdirs
	$(RM) $(BUILD_DIR)/$(BIN_FILE)
	$(MAKE) $(BUILD_DIR)/$(BIN_FILE)

#===============================================================================
# link the object files
#===============================================================================
$(BUILD_DIR)/$(BIN_FILE): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(CCFLAGS) $(_LIB_DIRS) $(OBJECTS) $(_LIBS) -o $(BUILD_DIR)/$(BIN_FILE)
ifeq ($(MODE),debug)
	$(OBJDUMP) -S $(BUILD_DIR)/$(BIN_FILE) > $(BUILD_DIR)/$(LSS_FILE)
endif

#===============================================================================
# build all cpp files
#===============================================================================
$(BUILD_DIR)/%.o: %.cpp
	$(CXX) -c $< $(CCFLAGS) $(CXXFLAGS) $(_INCLUDE_DIRS) $(_DEFINES) -o $@
#	$(TIDY) -checks='*,-llvmlibc-*,-llvm-include-order,-cppcoreguidelines-*,-hicpp-*,-fuchsia-*,-readability-*,-google-*,-clang-diagnostic-multichar,-bugprone-reserved-identifier,-cert-dcl37-c,-cert-dcl51-cpp' --quiet $< -- -std=c++20 $(_INCLUDE_DIRS) $(_DEFINES)
#	$(TIDY) -checks='modernize-*' --quiet $< -- -std=c++20 $(_INCLUDE_DIRS) $(_DEFINES)

#===============================================================================
# create the output directories
#===============================================================================
mkdirs:
	$(MKDIR) $(dir $(OBJECTS))
ifeq ($(MODE),profile)
	$(MKDIR) $(PROFILE_DIR)
endif 

#===============================================================================
# Handle guided build
#===============================================================================
guided:
	$(MAKE) MODE=profile clean
	$(MAKE) MODE=profile all
	@$(BUILD_DIR)/Moruga -6 c enwik8 enwik8.dat
	$(MAKE) clean
	$(MAKE) MODE=guided all

#===============================================================================
# remove the build artifacts
#===============================================================================
clean:
	$(RM) $(BUILD_DIR)/*
ifeq ($(MODE),profile)
	$(RM) $(PROFILE_DIR)/*
endif 
