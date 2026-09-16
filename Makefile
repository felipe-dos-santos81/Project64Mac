# Makefile for Project64 — macOS / Apple Silicon build with SDL3
# Targets are numbered by build stage:
#   0 deps → 1 common → 2 core → 3 rsp → 4 video → 5 audio → 6 input → 7 frontend → 8 run
SERVICE = Project64 macOS

# ── Variables ─────────────────────────────────────────────────────────────────
SRC        = Source
BUILD      = build/macos
LIBDIR     = $(BUILD)/lib
BIN        = Bin/macOS
# Plugin output dir matches the core's default Directory_PluginInitial ("Plugin").
# Keep the comment on its own line: GNU make keeps whitespace before an inline #.
PLUGINS    = $(BIN)/Plugin
# Apple clang, not Homebrew clang: this build needs the macOS SDK and OpenGL.framework.
# Plain '=', not '?=': make's built-in CXX/CC have origin 'default', so '?=' would not
# override them. Override on the command line instead (make CXX=... CC=...).
CXX        = /usr/bin/clang++
CC         = /usr/bin/clang
ARCH       = -arch arm64
OPT       ?= -O2 -g
SDL_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
YAML_CFLAGS := $(shell pkg-config --cflags yaml-cpp 2>/dev/null)
YAML_LIBS   := $(shell pkg-config --libs yaml-cpp 2>/dev/null)
CPPFLAGS   = -I$(SRC) -I$(SRC)/3rdParty -I$(SRC)/3rdParty/asmjit/src -DASMJIT_STATIC
# Vendored upstream code is not warning-clean, so warnings are off by default and
# turned back on per object group (see FRONTEND_OBJS below) for hand-written code.
WARN       = -w
# -MMD -MP: emit a .d file per object listing the headers it used, so editing a header
# rebuilds what includes it. -MP adds phony targets so a deleted header is not a hard error.
DEPFLAGS   = -MMD -MP
# c++14, not c++11: Project64-rsp-core/Recompiler/RspCodeBlock.cpp uses std::make_unique.
CXXFLAGS   = $(ARCH) -std=c++14 $(OPT) -fPIC $(WARN) $(DEPFLAGS) $(CPPFLAGS)
CFLAGS     = $(ARCH) -std=gnu99 $(OPT) -fPIC $(WARN) $(DEPFLAGS) $(CPPFLAGS)
LDFLAGS    = $(ARCH)

objs = $(addprefix $(BUILD)/, $(addsuffix .o, $(basename $(1))))

# ── Source lists (validated against the tree on 2026-09-15) ───────────────────
COMMON_SRC = $(addprefix Common/, CriticalSection.cpp DateTime.cpp DynamicLibrary.cpp File.cpp \
  HighResTimeStamp.cpp IniFile.cpp Log.cpp md5.cpp MemoryManagement.cpp path.cpp Platform.cpp \
  Random.cpp StdString.cpp SyncEvent.cpp Thread.cpp Trace.cpp Util.cpp)
SETTINGS_SRC = Settings/Settings.cpp
ZLIB_SRC = $(addprefix 3rdParty/zlib/, adler32.c compress.c crc32.c deflate.c gzclose.c gzlib.c \
  gzread.c gzwrite.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c \
  contrib/minizip/ioapi.c contrib/minizip/mztools.c contrib/minizip/unzip.c contrib/minizip/zip.c)
PNG_SRC = $(addprefix 3rdParty/png/, png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c \
  pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c)
ASMJIT_SRC = $(patsubst $(SRC)/%,%,$(wildcard $(SRC)/3rdParty/asmjit/src/asmjit/core/*.cpp \
  $(SRC)/3rdParty/asmjit/src/asmjit/arm/*.cpp $(SRC)/3rdParty/asmjit/src/asmjit/x86/*.cpp))
# SoftFloat supplies the IEEE semantics the interpreter's FPU ops depend on
# (Project64-core/N64System/Interpreter/InterpreterOps.cpp). These 264 sources, the 8086
# specialization, the MinGW platform.h and the absence of SOFTFLOAT_FAST_INT64 came from
# upstream's softfloat.vcxproj, deleted along with the rest of the MSVC build. Do not
# replace this list with a wildcard over source/*.c: the 86 files it leaves out are the
# FAST_INT64 variants, and 210 translation units fail to compile in this configuration.
# Only the files listed here are shipped, so a wildcard would also pick up nothing new.
SOFTFLOAT_DIR = 3rdParty/softfloat-3e
SOFTFLOAT_SRC = \
  3rdParty/softfloat-3e/source/8086/extF80M_isSignalingNaN.c \
  3rdParty/softfloat-3e/source/8086/f128M_isSignalingNaN.c \
  3rdParty/softfloat-3e/source/8086/softfloat_raiseFlags.c \
  3rdParty/softfloat-3e/source/8086/s_commonNaNToExtF80M.c \
  3rdParty/softfloat-3e/source/8086/s_commonNaNToF128M.c \
  3rdParty/softfloat-3e/source/8086/s_commonNaNToF16UI.c \
  3rdParty/softfloat-3e/source/8086/s_commonNaNToF32UI.c \
  3rdParty/softfloat-3e/source/8086/s_commonNaNToF64UI.c \
  3rdParty/softfloat-3e/source/8086/s_extF80MToCommonNaN.c \
  3rdParty/softfloat-3e/source/8086/s_extF80UIToCommonNaN.c \
  3rdParty/softfloat-3e/source/8086/s_f128MToCommonNaN.c \
  3rdParty/softfloat-3e/source/8086/s_f16UIToCommonNaN.c \
  3rdParty/softfloat-3e/source/8086/s_f32UIToCommonNaN.c \
  3rdParty/softfloat-3e/source/8086/s_f64UIToCommonNaN.c \
  3rdParty/softfloat-3e/source/8086/s_propagateNaNExtF80M.c \
  3rdParty/softfloat-3e/source/8086/s_propagateNaNF128M.c \
  3rdParty/softfloat-3e/source/8086/s_propagateNaNF16UI.c \
  3rdParty/softfloat-3e/source/8086/s_propagateNaNF32UI.c \
  3rdParty/softfloat-3e/source/8086/s_propagateNaNF64UI.c \
  3rdParty/softfloat-3e/source/extF80M_add.c 3rdParty/softfloat-3e/source/extF80M_div.c \
  3rdParty/softfloat-3e/source/extF80M_eq.c 3rdParty/softfloat-3e/source/extF80M_eq_signaling.c \
  3rdParty/softfloat-3e/source/extF80M_le.c 3rdParty/softfloat-3e/source/extF80M_le_quiet.c \
  3rdParty/softfloat-3e/source/extF80M_lt.c 3rdParty/softfloat-3e/source/extF80M_lt_quiet.c \
  3rdParty/softfloat-3e/source/extF80M_mul.c 3rdParty/softfloat-3e/source/extF80M_rem.c \
  3rdParty/softfloat-3e/source/extF80M_roundToInt.c 3rdParty/softfloat-3e/source/extF80M_sqrt.c \
  3rdParty/softfloat-3e/source/extF80M_sub.c 3rdParty/softfloat-3e/source/extF80M_to_f128M.c \
  3rdParty/softfloat-3e/source/extF80M_to_f16.c 3rdParty/softfloat-3e/source/extF80M_to_f32.c \
  3rdParty/softfloat-3e/source/extF80M_to_f64.c 3rdParty/softfloat-3e/source/extF80M_to_i32.c \
  3rdParty/softfloat-3e/source/extF80M_to_i32_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80M_to_i64.c \
  3rdParty/softfloat-3e/source/extF80M_to_i64_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80M_to_ui32.c \
  3rdParty/softfloat-3e/source/extF80M_to_ui32_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80M_to_ui64.c \
  3rdParty/softfloat-3e/source/extF80M_to_ui64_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80_eq.c 3rdParty/softfloat-3e/source/extF80_eq_signaling.c \
  3rdParty/softfloat-3e/source/extF80_isSignalingNaN.c 3rdParty/softfloat-3e/source/extF80_le.c \
  3rdParty/softfloat-3e/source/extF80_le_quiet.c 3rdParty/softfloat-3e/source/extF80_lt.c \
  3rdParty/softfloat-3e/source/extF80_lt_quiet.c 3rdParty/softfloat-3e/source/extF80_to_f16.c \
  3rdParty/softfloat-3e/source/extF80_to_f32.c 3rdParty/softfloat-3e/source/extF80_to_f64.c \
  3rdParty/softfloat-3e/source/extF80_to_i32.c \
  3rdParty/softfloat-3e/source/extF80_to_i32_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80_to_i64_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80_to_ui32.c \
  3rdParty/softfloat-3e/source/extF80_to_ui32_r_minMag.c \
  3rdParty/softfloat-3e/source/extF80_to_ui64_r_minMag.c \
  3rdParty/softfloat-3e/source/f128M_add.c 3rdParty/softfloat-3e/source/f128M_div.c \
  3rdParty/softfloat-3e/source/f128M_eq.c 3rdParty/softfloat-3e/source/f128M_eq_signaling.c \
  3rdParty/softfloat-3e/source/f128M_le.c 3rdParty/softfloat-3e/source/f128M_le_quiet.c \
  3rdParty/softfloat-3e/source/f128M_lt.c 3rdParty/softfloat-3e/source/f128M_lt_quiet.c \
  3rdParty/softfloat-3e/source/f128M_mul.c 3rdParty/softfloat-3e/source/f128M_mulAdd.c \
  3rdParty/softfloat-3e/source/f128M_rem.c 3rdParty/softfloat-3e/source/f128M_roundToInt.c \
  3rdParty/softfloat-3e/source/f128M_sqrt.c 3rdParty/softfloat-3e/source/f128M_sub.c \
  3rdParty/softfloat-3e/source/f128M_to_extF80M.c 3rdParty/softfloat-3e/source/f128M_to_f16.c \
  3rdParty/softfloat-3e/source/f128M_to_f32.c 3rdParty/softfloat-3e/source/f128M_to_f64.c \
  3rdParty/softfloat-3e/source/f128M_to_i32.c \
  3rdParty/softfloat-3e/source/f128M_to_i32_r_minMag.c \
  3rdParty/softfloat-3e/source/f128M_to_i64.c \
  3rdParty/softfloat-3e/source/f128M_to_i64_r_minMag.c \
  3rdParty/softfloat-3e/source/f128M_to_ui32.c \
  3rdParty/softfloat-3e/source/f128M_to_ui32_r_minMag.c \
  3rdParty/softfloat-3e/source/f128M_to_ui64.c \
  3rdParty/softfloat-3e/source/f128M_to_ui64_r_minMag.c 3rdParty/softfloat-3e/source/f16_add.c \
  3rdParty/softfloat-3e/source/f16_div.c 3rdParty/softfloat-3e/source/f16_eq.c \
  3rdParty/softfloat-3e/source/f16_eq_signaling.c \
  3rdParty/softfloat-3e/source/f16_isSignalingNaN.c 3rdParty/softfloat-3e/source/f16_le.c \
  3rdParty/softfloat-3e/source/f16_le_quiet.c 3rdParty/softfloat-3e/source/f16_lt.c \
  3rdParty/softfloat-3e/source/f16_lt_quiet.c 3rdParty/softfloat-3e/source/f16_mul.c \
  3rdParty/softfloat-3e/source/f16_mulAdd.c 3rdParty/softfloat-3e/source/f16_rem.c \
  3rdParty/softfloat-3e/source/f16_roundToInt.c 3rdParty/softfloat-3e/source/f16_sqrt.c \
  3rdParty/softfloat-3e/source/f16_sub.c 3rdParty/softfloat-3e/source/f16_to_extF80M.c \
  3rdParty/softfloat-3e/source/f16_to_f128M.c 3rdParty/softfloat-3e/source/f16_to_f32.c \
  3rdParty/softfloat-3e/source/f16_to_f64.c 3rdParty/softfloat-3e/source/f16_to_i32.c \
  3rdParty/softfloat-3e/source/f16_to_i32_r_minMag.c 3rdParty/softfloat-3e/source/f16_to_i64.c \
  3rdParty/softfloat-3e/source/f16_to_i64_r_minMag.c 3rdParty/softfloat-3e/source/f16_to_ui32.c \
  3rdParty/softfloat-3e/source/f16_to_ui32_r_minMag.c \
  3rdParty/softfloat-3e/source/f16_to_ui64.c \
  3rdParty/softfloat-3e/source/f16_to_ui64_r_minMag.c 3rdParty/softfloat-3e/source/f32_add.c \
  3rdParty/softfloat-3e/source/f32_div.c 3rdParty/softfloat-3e/source/f32_eq.c \
  3rdParty/softfloat-3e/source/f32_eq_signaling.c \
  3rdParty/softfloat-3e/source/f32_isSignalingNaN.c 3rdParty/softfloat-3e/source/f32_le.c \
  3rdParty/softfloat-3e/source/f32_le_quiet.c 3rdParty/softfloat-3e/source/f32_lt.c \
  3rdParty/softfloat-3e/source/f32_lt_quiet.c 3rdParty/softfloat-3e/source/f32_mul.c \
  3rdParty/softfloat-3e/source/f32_mulAdd.c 3rdParty/softfloat-3e/source/f32_rem.c \
  3rdParty/softfloat-3e/source/f32_roundToInt.c 3rdParty/softfloat-3e/source/f32_sqrt.c \
  3rdParty/softfloat-3e/source/f32_sub.c 3rdParty/softfloat-3e/source/f32_to_extF80M.c \
  3rdParty/softfloat-3e/source/f32_to_f128M.c 3rdParty/softfloat-3e/source/f32_to_f16.c \
  3rdParty/softfloat-3e/source/f32_to_f64.c 3rdParty/softfloat-3e/source/f32_to_i32.c \
  3rdParty/softfloat-3e/source/f32_to_i32_r_minMag.c 3rdParty/softfloat-3e/source/f32_to_i64.c \
  3rdParty/softfloat-3e/source/f32_to_i64_r_minMag.c 3rdParty/softfloat-3e/source/f32_to_ui32.c \
  3rdParty/softfloat-3e/source/f32_to_ui32_r_minMag.c \
  3rdParty/softfloat-3e/source/f32_to_ui64.c \
  3rdParty/softfloat-3e/source/f32_to_ui64_r_minMag.c 3rdParty/softfloat-3e/source/f64_add.c \
  3rdParty/softfloat-3e/source/f64_div.c 3rdParty/softfloat-3e/source/f64_eq.c \
  3rdParty/softfloat-3e/source/f64_eq_signaling.c \
  3rdParty/softfloat-3e/source/f64_isSignalingNaN.c 3rdParty/softfloat-3e/source/f64_le.c \
  3rdParty/softfloat-3e/source/f64_le_quiet.c 3rdParty/softfloat-3e/source/f64_lt.c \
  3rdParty/softfloat-3e/source/f64_lt_quiet.c 3rdParty/softfloat-3e/source/f64_mul.c \
  3rdParty/softfloat-3e/source/f64_mulAdd.c 3rdParty/softfloat-3e/source/f64_rem.c \
  3rdParty/softfloat-3e/source/f64_roundToInt.c 3rdParty/softfloat-3e/source/f64_sqrt.c \
  3rdParty/softfloat-3e/source/f64_sub.c 3rdParty/softfloat-3e/source/f64_to_extF80M.c \
  3rdParty/softfloat-3e/source/f64_to_f128M.c 3rdParty/softfloat-3e/source/f64_to_f16.c \
  3rdParty/softfloat-3e/source/f64_to_f32.c 3rdParty/softfloat-3e/source/f64_to_i32.c \
  3rdParty/softfloat-3e/source/f64_to_i32_r_minMag.c 3rdParty/softfloat-3e/source/f64_to_i64.c \
  3rdParty/softfloat-3e/source/f64_to_i64_r_minMag.c 3rdParty/softfloat-3e/source/f64_to_ui32.c \
  3rdParty/softfloat-3e/source/f64_to_ui32_r_minMag.c \
  3rdParty/softfloat-3e/source/f64_to_ui64.c \
  3rdParty/softfloat-3e/source/f64_to_ui64_r_minMag.c \
  3rdParty/softfloat-3e/source/i32_to_extF80.c 3rdParty/softfloat-3e/source/i32_to_extF80M.c \
  3rdParty/softfloat-3e/source/i32_to_f128M.c 3rdParty/softfloat-3e/source/i32_to_f16.c \
  3rdParty/softfloat-3e/source/i32_to_f32.c 3rdParty/softfloat-3e/source/i32_to_f64.c \
  3rdParty/softfloat-3e/source/i64_to_extF80.c 3rdParty/softfloat-3e/source/i64_to_extF80M.c \
  3rdParty/softfloat-3e/source/i64_to_f128M.c 3rdParty/softfloat-3e/source/i64_to_f16.c \
  3rdParty/softfloat-3e/source/i64_to_f32.c 3rdParty/softfloat-3e/source/i64_to_f64.c \
  3rdParty/softfloat-3e/source/softfloat_state.c 3rdParty/softfloat-3e/source/s_add256M.c \
  3rdParty/softfloat-3e/source/s_addCarryM.c 3rdParty/softfloat-3e/source/s_addComplCarryM.c \
  3rdParty/softfloat-3e/source/s_addExtF80M.c 3rdParty/softfloat-3e/source/s_addF128M.c \
  3rdParty/softfloat-3e/source/s_addM.c 3rdParty/softfloat-3e/source/s_addMagsF16.c \
  3rdParty/softfloat-3e/source/s_addMagsF32.c 3rdParty/softfloat-3e/source/s_addMagsF64.c \
  3rdParty/softfloat-3e/source/s_approxRecip32_1.c \
  3rdParty/softfloat-3e/source/s_approxRecipSqrt32_1.c \
  3rdParty/softfloat-3e/source/s_approxRecipSqrt_1Ks.c \
  3rdParty/softfloat-3e/source/s_approxRecip_1Ks.c 3rdParty/softfloat-3e/source/s_compare128M.c \
  3rdParty/softfloat-3e/source/s_compare96M.c \
  3rdParty/softfloat-3e/source/s_compareNonnormExtF80M.c \
  3rdParty/softfloat-3e/source/s_countLeadingZeros8.c 3rdParty/softfloat-3e/source/s_eq128.c \
  3rdParty/softfloat-3e/source/s_invalidExtF80M.c 3rdParty/softfloat-3e/source/s_invalidF128M.c \
  3rdParty/softfloat-3e/source/s_isNaNF128M.c 3rdParty/softfloat-3e/source/s_le128.c \
  3rdParty/softfloat-3e/source/s_lt128.c 3rdParty/softfloat-3e/source/s_mul64To128M.c \
  3rdParty/softfloat-3e/source/s_mulAddF128M.c 3rdParty/softfloat-3e/source/s_mulAddF16.c \
  3rdParty/softfloat-3e/source/s_mulAddF32.c 3rdParty/softfloat-3e/source/s_mulAddF64.c \
  3rdParty/softfloat-3e/source/s_negXM.c 3rdParty/softfloat-3e/source/s_normExtF80SigM.c \
  3rdParty/softfloat-3e/source/s_normRoundPackMToExtF80M.c \
  3rdParty/softfloat-3e/source/s_normRoundPackMToF128M.c \
  3rdParty/softfloat-3e/source/s_normRoundPackToF16.c \
  3rdParty/softfloat-3e/source/s_normRoundPackToF32.c \
  3rdParty/softfloat-3e/source/s_normRoundPackToF64.c \
  3rdParty/softfloat-3e/source/s_normSubnormalF128SigM.c \
  3rdParty/softfloat-3e/source/s_normSubnormalF16Sig.c \
  3rdParty/softfloat-3e/source/s_normSubnormalF32Sig.c \
  3rdParty/softfloat-3e/source/s_normSubnormalF64Sig.c \
  3rdParty/softfloat-3e/source/s_remStepMBy32.c 3rdParty/softfloat-3e/source/s_roundMToI64.c \
  3rdParty/softfloat-3e/source/s_roundMToUI64.c \
  3rdParty/softfloat-3e/source/s_roundPackMToExtF80M.c \
  3rdParty/softfloat-3e/source/s_roundPackMToF128M.c \
  3rdParty/softfloat-3e/source/s_roundPackToF16.c \
  3rdParty/softfloat-3e/source/s_roundPackToF32.c \
  3rdParty/softfloat-3e/source/s_roundPackToF64.c 3rdParty/softfloat-3e/source/s_roundToI32.c \
  3rdParty/softfloat-3e/source/s_roundToI64.c 3rdParty/softfloat-3e/source/s_roundToUI32.c \
  3rdParty/softfloat-3e/source/s_roundToUI64.c 3rdParty/softfloat-3e/source/s_shiftLeftM.c \
  3rdParty/softfloat-3e/source/s_shiftNormSigF128M.c \
  3rdParty/softfloat-3e/source/s_shiftRightJam256M.c \
  3rdParty/softfloat-3e/source/s_shiftRightJam32.c \
  3rdParty/softfloat-3e/source/s_shiftRightJam64.c \
  3rdParty/softfloat-3e/source/s_shiftRightJamM.c 3rdParty/softfloat-3e/source/s_shiftRightM.c \
  3rdParty/softfloat-3e/source/s_shortShiftLeft64To96M.c \
  3rdParty/softfloat-3e/source/s_shortShiftLeftM.c \
  3rdParty/softfloat-3e/source/s_shortShiftRightExtendM.c \
  3rdParty/softfloat-3e/source/s_shortShiftRightJam64.c \
  3rdParty/softfloat-3e/source/s_shortShiftRightJamM.c \
  3rdParty/softfloat-3e/source/s_shortShiftRightM.c 3rdParty/softfloat-3e/source/s_sub1XM.c \
  3rdParty/softfloat-3e/source/s_sub256M.c 3rdParty/softfloat-3e/source/s_subM.c \
  3rdParty/softfloat-3e/source/s_subMagsF16.c 3rdParty/softfloat-3e/source/s_subMagsF32.c \
  3rdParty/softfloat-3e/source/s_subMagsF64.c \
  3rdParty/softfloat-3e/source/s_tryPropagateNaNExtF80M.c \
  3rdParty/softfloat-3e/source/s_tryPropagateNaNF128M.c \
  3rdParty/softfloat-3e/source/ui32_to_extF80.c 3rdParty/softfloat-3e/source/ui32_to_extF80M.c \
  3rdParty/softfloat-3e/source/ui32_to_f128M.c 3rdParty/softfloat-3e/source/ui32_to_f16.c \
  3rdParty/softfloat-3e/source/ui32_to_f32.c 3rdParty/softfloat-3e/source/ui32_to_f64.c \
  3rdParty/softfloat-3e/source/ui64_to_extF80.c 3rdParty/softfloat-3e/source/ui64_to_extF80M.c \
  3rdParty/softfloat-3e/source/ui64_to_f128M.c 3rdParty/softfloat-3e/source/ui64_to_f16.c \
  3rdParty/softfloat-3e/source/ui64_to_f32.c 3rdParty/softfloat-3e/source/ui64_to_f64.c

# RSPRegisterHandler lives in rsp-core but the core's SPRegistersHandler links against
# it, so the core archive needs it too. RSPCORE_SRC's wildcard over that same directory
# maps to the identical object path, so it is compiled once and linked into both
# binaries - do not "fix" this into two separate compiles.
CORE_SRC = Project64-rsp-core/cpu/RSPRegisterHandler.cpp \
  $(addprefix Project64-core/, AppInit.cpp Logging.cpp Settings.cpp \
  Multilanguage/Language.cpp Settings/LoggingSettings.cpp \
  N64System/Enhancement/Enhancement.cpp N64System/Enhancement/Enhancements.cpp \
  N64System/Enhancement/EnhancementFile.cpp N64System/Enhancement/EnhancementList.cpp \
  N64System/Interpreter/InterpreterOps.cpp \
  N64System/MemoryHandler/AudioInterfaceHandler.cpp N64System/MemoryHandler/CartridgeDomain1Address1Handler.cpp \
  N64System/MemoryHandler/CartridgeDomain1Address3Handler.cpp N64System/MemoryHandler/CartridgeDomain2Address1Handler.cpp \
  N64System/MemoryHandler/CartridgeDomain2Address2Handler.cpp N64System/MemoryHandler/DisplayControlRegHandler.cpp \
  N64System/MemoryHandler/ISViewerHandler.cpp N64System/MemoryHandler/MIPSInterfaceHandler.cpp \
  N64System/MemoryHandler/PeripheralInterfaceHandler.cpp N64System/MemoryHandler/PifRamHandler.cpp \
  N64System/MemoryHandler/RDRAMInterfaceHandler.cpp N64System/MemoryHandler/RDRAMRegistersHandler.cpp \
  N64System/MemoryHandler/RomMemoryHandler.cpp N64System/MemoryHandler/SerialInterfaceHandler.cpp \
  N64System/MemoryHandler/SPRegistersHandler.cpp N64System/MemoryHandler/VideoInterfaceHandler.cpp \
  N64System/Mips/Disk.cpp N64System/Mips/GBCart.cpp N64System/Mips/MemoryVirtualMem.cpp N64System/Mips/Mempak.cpp \
  N64System/Mips/R4300iInstruction.cpp N64System/Mips/Register.cpp N64System/Mips/Rumblepak.cpp \
  N64System/Mips/Transferpak.cpp N64System/Mips/SystemEvents.cpp N64System/Mips/SystemTiming.cpp N64System/Mips/TLB.cpp \
  N64System/Recompiler/CodeBlock.cpp N64System/Recompiler/CodeSection.cpp N64System/Recompiler/ExitInfo.cpp \
  N64System/Recompiler/FunctionInfo.cpp N64System/Recompiler/FunctionMap.cpp N64System/Recompiler/JumpInfo.cpp \
  N64System/Recompiler/LoopAnalysis.cpp N64System/Recompiler/Recompiler.cpp N64System/Recompiler/RecompilerOps.cpp \
  N64System/Recompiler/RecompilerMemory.cpp N64System/Recompiler/RegBase.cpp \
  N64System/Recompiler/Aarch64/Aarch64ops.cpp N64System/Recompiler/Aarch64/Aarch64RecompilerOps.cpp \
  N64System/Recompiler/Aarch64/Aarch64RegInfo.cpp \
  N64System/SaveType/Eeprom.cpp N64System/SaveType/FlashRam.cpp N64System/SaveType/Sram.cpp \
  N64System/FramePerSecond.cpp N64System/N64System.cpp N64System/N64Rom.cpp N64System/Profiling.cpp \
  N64System/SpeedLimiter.cpp N64System/SystemGlobals.cpp N64System/EmulationThread.cpp N64System/N64Disk.cpp \
  Plugins/AudioPlugin.cpp Plugins/GFXPlugin.cpp Plugins/ControllerPlugin.cpp Plugins/RSPPlugin.cpp \
  Plugins/PluginBase.cpp Plugins/Plugin.cpp RomList/RomList.cpp \
  Settings/SettingType/SettingsType-Application.cpp Settings/SettingType/SettingsType-ApplicationIndex.cpp \
  Settings/SettingType/SettingsType-ApplicationPath.cpp Settings/SettingType/SettingsType-GameSetting.cpp \
  Settings/SettingType/SettingsType-GameSettingIndex.cpp Settings/SettingType/SettingsType-RelativePath.cpp \
  Settings/SettingType/SettingsType-RDB.cpp Settings/SettingType/SettingsType-RDBCpuType.cpp \
  Settings/SettingType/SettingsType-RDBOnOff.cpp Settings/SettingType/SettingsType-RDBLinking.cpp \
  Settings/SettingType/SettingsType-RDBRamSize.cpp Settings/SettingType/SettingsType-RDBSaveChip.cpp \
  Settings/SettingType/SettingsType-RDBUser.cpp Settings/SettingType/SettingsType-RomDatabase.cpp \
  Settings/SettingType/SettingsType-RomDatabaseIndex.cpp Settings/SettingType/SettingsType-RomDatabaseSetting.cpp \
  Settings/SettingType/SettingsType-SelectedDirectory.cpp Settings/SettingType/SettingsType-TempBool.cpp \
  Settings/SettingType/SettingsType-TempNumber.cpp Settings/SettingType/SettingsType-TempString.cpp \
  Settings/DebugSettings.cpp Settings/GameSettings.cpp Settings/SystemSettings.cpp)
RSPCORE_SRC = $(patsubst $(SRC)/%,%,$(wildcard $(SRC)/Project64-rsp-core/cpu/*.cpp \
  $(SRC)/Project64-rsp-core/Hle/*.cpp $(SRC)/Project64-rsp-core/Recompiler/*.cpp \
  $(SRC)/Project64-rsp-core/Settings/*.cpp)) Project64-rsp-core/RSPDebugger.cpp Project64-rsp-core/RSPInfo.cpp
RSP_SRC = Project64-sdl/PluginRSP.cpp $(RSPCORE_SRC)
VIDEO_SRC = $(addprefix Project64-video/, 3dmath.cpp Combine.cpp Config.cpp CRC.cpp Debugger.cpp \
  DepthBufferRender.cpp Ext_TxFilter.cpp F3DTEXA.cpp FBtoScreen.cpp Main.cpp rdp.cpp ScreenResolution.cpp \
  Settings.cpp TexBuffer.cpp TexCache.cpp trace.cpp turbo3D.cpp ucode.cpp ucode00.cpp ucode01.cpp ucode02.cpp \
  ucode03.cpp ucode04.cpp ucode05.cpp ucode06.cpp ucode07.cpp ucode08.cpp ucode09.cpp ucode09rdp.cpp ucodeFB.cpp \
  Util.cpp Renderer/OGLcombiner.cpp Renderer/OGLgeometry.cpp Renderer/OGLglitchmain.cpp Renderer/OGLtextures.cpp \
  Renderer/Renderer.cpp TextureEnhancer/TxFilterExport.cpp TextureEnhancer/TxFilter.cpp TextureEnhancer/TxCache.cpp \
  TextureEnhancer/TxTexCache.cpp TextureEnhancer/TxHiResCache.cpp TextureEnhancer/TxQuantize.cpp \
  TextureEnhancer/TxUtil.cpp TextureEnhancer/TextureFilters.cpp TextureEnhancer/TextureFilters_2xsai.cpp \
  TextureEnhancer/TextureFilters_hq2x.cpp TextureEnhancer/TextureFilters_hq4x.cpp TextureEnhancer/TxImage.cpp \
  TextureEnhancer/TxReSample.cpp TextureEnhancer/TxDbg.cpp TextureEnhancer/tc-1.1+/fxt1.c \
  TextureEnhancer/tc-1.1+/dxtn.c TextureEnhancer/tc-1.1+/wrapper.c TextureEnhancer/tc-1.1+/texstore.c)
AUDIO_SRC = $(addprefix Project64-audio/, AudioMain.cpp AudioSettings.cpp trace.cpp Driver/SoundBase.cpp Driver/SdlAudio.cpp)
INPUT_SRC = Project64-sdl/PluginInput.cpp Project64-sdl/InputConfig.cpp
# InputConfig.cpp is shared with the input plugin: the frontend parses the layout to size its window.
FRONTEND_SRC = $(addprefix Project64-sdl/, main.cpp SdlNotification.cpp SdlRenderWindow.cpp GridHost.cpp FaceGestures.cpp Overlay.cpp GameConfig.cpp InputConfig.cpp)
# Objective-C++: the face tracker talks to AVFoundation and Vision. Frontend only.
FRONTEND_MM_SRC = Project64-sdl/FaceTracker.mm
WIZARD_SRC = $(addprefix Project64-wizard/, main.cpp Screens.cpp WizardDraft.cpp)

COMMON_OBJS   = $(call objs,$(COMMON_SRC))
SETTINGS_OBJS = $(call objs,$(SETTINGS_SRC))
ZLIB_OBJS     = $(call objs,$(ZLIB_SRC))
PNG_OBJS      = $(call objs,$(PNG_SRC))
ASMJIT_OBJS   = $(call objs,$(ASMJIT_SRC))
SOFTFLOAT_OBJS = $(call objs,$(SOFTFLOAT_SRC))
CORE_OBJS     = $(call objs,$(CORE_SRC))
RSP_OBJS      = $(call objs,$(RSP_SRC))
VIDEO_OBJS    = $(call objs,$(VIDEO_SRC))
AUDIO_OBJS    = $(call objs,$(AUDIO_SRC))
INPUT_OBJS    = $(call objs,$(INPUT_SRC))
FRONTEND_OBJS = $(call objs,$(FRONTEND_SRC))
FRONTEND_MM_OBJS = $(call objs,$(FRONTEND_MM_SRC))
WIZARD_OBJS = $(call objs,$(WIZARD_SRC))
ALL_OBJS      = $(COMMON_OBJS) $(SETTINGS_OBJS) $(ZLIB_OBJS) $(PNG_OBJS) $(ASMJIT_OBJS) \
  $(SOFTFLOAT_OBJS) $(CORE_OBJS) $(RSP_OBJS) $(VIDEO_OBJS) $(AUDIO_OBJS) $(INPUT_OBJS) \
  $(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(WIZARD_OBJS)

# The four plugin dylibs, named once: the build rules and the smoke test both use this.
PLUGIN_DYLIBS = $(PLUGINS)/GFX/Project64-video.dylib $(PLUGINS)/Audio/Project64-audio.dylib \
  $(PLUGINS)/RSP/Project64-rsp.dylib $(PLUGINS)/Input/Project64-input-sdl.dylib

VERSION_HEADERS = $(addprefix $(SRC)/, Project64-core/Version.h Project64-video/Version.h \
  Project64-audio/Version.h Project64-rsp-core/Version.h)

# Per-component flags
# HAVE_UNISTD_H makes zconf.h define Z_HAVE_UNISTD_H and include <unistd.h>, which
# declares read/write/close/lseek for the gz* sources. Without it clang errors on
# implicit function declarations (C99+).
$(ZLIB_OBJS): CPPFLAGS += -DUSE_FILE32API -DZ_BUFSIZE=46516 -DHAVE_UNISTD_H
# pngconf.h defines MACOS when TARGET_OS_MAC is set, which makes pngpriv.h reach for
# the classic Mac OS <fp.h>. Including Apple's <math.h> first defines __MATH_H__, the
# guard pngpriv.h honours, so it skips <fp.h> and png still gets the math declarations.
$(PNG_OBJS): CPPFLAGS += -include math.h
# -Wno-implicit-function-declaration matches what MSVC tolerates for seven extF80
# comparison helpers whose prototypes sit behind the FAST_INT64 guard. s_lt128.c is in the
# build and defines the symbol, and the N64 has no 80-bit float type, so nothing the
# emulator executes reaches those paths.
$(SOFTFLOAT_OBJS): CPPFLAGS += -I$(SRC)/$(SOFTFLOAT_DIR)/build/Win32-SSE2-MinGW \
  -I$(SRC)/$(SOFTFLOAT_DIR)/source/8086 -I$(SRC)/$(SOFTFLOAT_DIR)/source/include
$(SOFTFLOAT_OBJS): CFLAGS += -Wno-implicit-function-declaration
# InterpreterOps.cpp includes softfloat.h directly, so the core needs the same headers.
$(CORE_OBJS): CPPFLAGS += -I$(SRC)/$(SOFTFLOAT_DIR)/source/8086 \
  -I$(SRC)/$(SOFTFLOAT_DIR)/source/include -I$(SRC)/$(SOFTFLOAT_DIR)/build/Win32-SSE2-MinGW
$(VIDEO_OBJS): CPPFLAGS += -DNOSSE
$(AUDIO_OBJS) $(INPUT_OBJS) $(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(WIZARD_OBJS) $(BUILD)/Project64-sdl/InputConfigTest.o $(BUILD)/Project64-wizard/WizardDraftTest.o: CPPFLAGS += $(SDL_CFLAGS)
$(INPUT_OBJS) $(WIZARD_OBJS) $(BUILD)/Project64-sdl/InputConfigTest.o $(BUILD)/Project64-wizard/WizardDraftTest.o: CPPFLAGS += $(YAML_CFLAGS)
$(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(WIZARD_OBJS): WARN = -Wall

.PHONY: help deps version common core rsp video audio input frontend wizard config all run grid run-wizard rom-test grid-selftest pointer-selftest face-selftest wizard-selftest unit-test input-config-test pointer-layout-test face-gesture-test game-config-test wizard-draft-test test clean

# ── Environment ──────────────────────────────────────────────────────────────

help: ## Print this help message (default)
	@printf '\033[01;32m${SERVICE} — Apple Silicon build with SDL3\033[00;37m\n\n'
	@printf "\033[33mUsage:\033[0m\n  make [target] [arg=\"val\"...]\n\n\033[33mTargets:\033[0m\n"
	@grep -hE '^[-a-zA-Z0-9_\.\/]+:.*?## .*$$' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "} \
		{name[NR] = $$1; desc[NR] = $$2; if (length($$1) > w) w = length($$1)} \
		END {for (i = 1; i <= NR; i++) printf "  \033[36m%-*s\033[0m %s\n", w, name[i], desc[i]}'
.DEFAULT_GOAL := help

deps: # Verify clang, make, pkg-config and Homebrew SDL3 and yaml-cpp are present
	@command -v $(CXX) >/dev/null || { echo "clang++ not found: xcode-select --install"; exit 1; }
	@command -v pkg-config >/dev/null || { echo "pkg-config not found: brew install pkg-config"; exit 1; }
	@pkg-config --exists sdl3 || { echo "SDL3 not found: brew install sdl3"; exit 1; }
	@pkg-config --exists yaml-cpp || { echo "yaml-cpp not found: brew install yaml-cpp"; exit 1; }
	@echo "deps ok: SDL3 $$(pkg-config --modversion sdl3), yaml-cpp $$(pkg-config --modversion yaml-cpp), $$($(CXX) --version | head -1)"

version: $(VERSION_HEADERS) # Generate Version.h from Version.h.in for core, video, audio, rsp-core

$(SRC)/%/Version.h: $(SRC)/%/Version.h.in
	cp $< $@

# ── Stage 1 · Common libraries ───────────────────────────────────────────────

common: $(LIBDIR)/libCommon.a $(LIBDIR)/libSettings.a $(LIBDIR)/libzlib.a $(LIBDIR)/libpng.a $(LIBDIR)/libasmjit.a $(LIBDIR)/libsoftfloat.a # Build Common, Settings, zlib, png, asmjit, softfloat static libs

# One recipe for every static library; each rule below only names its objects.
# 'rm -f' first so a source dropped from a list does not leave a stale member behind.
$(LIBDIR)/%.a:
	@mkdir -p $(dir $@)
	@rm -f $@
	ar rcs $@ $^

$(LIBDIR)/libCommon.a: $(COMMON_OBJS)
$(LIBDIR)/libSettings.a: $(SETTINGS_OBJS)
$(LIBDIR)/libzlib.a: $(ZLIB_OBJS)
$(LIBDIR)/libpng.a: $(PNG_OBJS)
$(LIBDIR)/libsoftfloat.a: $(SOFTFLOAT_OBJS)

$(LIBDIR)/libasmjit.a: $(ASMJIT_OBJS)
# ── Stage 2 · Emulator core ──────────────────────────────────────────────────

core: $(LIBDIR)/libProject64-core.a # Build the Project64 core static library

$(CORE_OBJS): $(SRC)/Project64-core/Version.h
$(LIBDIR)/libProject64-core.a: $(CORE_OBJS)

# ── Stages 3–6 · Plugins ─────────────────────────────────────────────────────

rsp: $(PLUGINS)/RSP/Project64-rsp.dylib # Build the RSP plugin (rsp-core + entry point)
$(RSP_OBJS): $(SRC)/Project64-rsp-core/Version.h
$(PLUGINS)/RSP/Project64-rsp.dylib: $(RSP_OBJS) $(LIBDIR)/libSettings.a $(LIBDIR)/libzlib.a $(LIBDIR)/libCommon.a
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -dynamiclib -o $@ $^

video: $(PLUGINS)/GFX/Project64-video.dylib # Build the video plugin (Glide64 on desktop OpenGL)
$(VIDEO_OBJS): $(SRC)/Project64-video/Version.h
$(PLUGINS)/GFX/Project64-video.dylib: $(VIDEO_OBJS) $(LIBDIR)/libpng.a $(LIBDIR)/libzlib.a $(LIBDIR)/libSettings.a $(LIBDIR)/libCommon.a
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -dynamiclib -o $@ $^ -framework OpenGL

audio: $(PLUGINS)/Audio/Project64-audio.dylib # Build the audio plugin (SDL3 audio stream driver)
$(AUDIO_OBJS): $(SRC)/Project64-audio/Version.h
$(PLUGINS)/Audio/Project64-audio.dylib: $(AUDIO_OBJS) $(LIBDIR)/libSettings.a $(LIBDIR)/libCommon.a
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -dynamiclib -o $@ $^ $(SDL_LIBS)

input: $(PLUGINS)/Input/Project64-input-sdl.dylib # Build the SDL3 keyboard/gamepad input plugin
$(PLUGINS)/Input/Project64-input-sdl.dylib: $(INPUT_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -dynamiclib -o $@ $^ $(SDL_LIBS) $(YAML_LIBS)

# ── Stage 7 · Frontend ───────────────────────────────────────────────────────

frontend: $(BIN)/Project64 # Build the SDL3 frontend executable
$(BIN)/Project64: $(FRONTEND_OBJS) $(FRONTEND_MM_OBJS) $(LIBDIR)/libProject64-core.a $(LIBDIR)/libasmjit.a $(LIBDIR)/libzlib.a $(LIBDIR)/libsoftfloat.a $(LIBDIR)/libCommon.a
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(YAML_LIBS) -framework OpenGL -framework Foundation -framework AVFoundation -framework Vision -framework CoreMedia -framework CoreVideo -lobjc -lpthread

# ── Stage 7c · Binding wizard ─────────────────────────────────────────────────

# A second binary, with no ROM and no OpenGL: SDL_Renderer and SDL's debug font. It shares
# the frontend's InputConfig, FaceGestures and FaceTracker objects rather than its own.
wizard: $(BIN)/Project64-wizard # Build the binding wizard
$(BIN)/Project64-wizard: $(WIZARD_OBJS) $(BUILD)/Project64-sdl/InputConfig.o $(BUILD)/Project64-sdl/FaceGestures.o $(BUILD)/Project64-sdl/FaceTracker.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(YAML_LIBS) -framework Foundation -framework AVFoundation -framework Vision -framework CoreMedia -framework CoreVideo -lobjc -lpthread

# ── Stage 7b · Data files ─────────────────────────────────────────────────────

# The core reads its ROM database, enhancement and language data from the base directory
# beside the executable. Without them it creates empty .rdb files and every game falls back
# to defaults, which is why the RSP reports "uCode crc not found in INI" and never runs the
# graphics task. Copy the shipped data next to the binary. Project64.cfg is a user file and
# is never overwritten.
config: # Install ROM database, enhancements, input mappings and language files beside the binary (refreshes Config/mouse and Config/face)
	@mkdir -p $(BIN)/Config $(BIN)/Lang
	@cp -f Config/Project64.rdb Config/Project64.rdx Config/Audio.rdb Config/Video.rdb $(BIN)/Config/
	@# -n, not -f: cheats and enhancement settings are user data once installed.
	@cp -Rn Config/Cheats Config/Enhancements $(BIN)/Config/ 2>/dev/null || true
	@# -n: the mapping is user data once installed.
	@cp -n Config/input.yaml $(BIN)/Config/ 2>/dev/null || true
	@# Replace, don't merge: the shipped mouse layouts are examples that follow the tracked
	@# files. Copying alone would leave an example renamed upstream behind under its old
	@# name, where the per-ROM lookup still finds it and the reader then rejects its stale
	@# zone names. A player's own layout goes beside the ROM as <rom>.yaml, checked first.
	@mkdir -p $(BIN)/Config/mouse
	@rm -f $(BIN)/Config/mouse/*.yaml
	@cp -f Config/mouse/*.yaml $(BIN)/Config/mouse/
	@# Same rule for the face layouts. They are outside the per-ROM lookup on purpose.
	@mkdir -p $(BIN)/Config/face
	@rm -f $(BIN)/Config/face/*.yaml
	@cp -f Config/face/*.yaml $(BIN)/Config/face/
	@cp -f Lang/*.pj.Lang Lang/*.pj.lang $(BIN)/Lang/ 2>/dev/null || true
	@echo "config installed into $(BIN)"

all: deps common core rsp video audio input frontend wizard config ## [STEP 0-7c] deps, common, core, rsp, video, audio, input, frontend, wizard, config: build everything

# ── Stage 8 · Run / test ─────────────────────────────────────────────────────

run: all ## Run a ROM in a window (usage: make run rom=/path/to/game.z64 [input=Config/mouse/super_mario_64_usa.yaml] [face=1|face=0])
	@test -n "$(rom)" || { echo "usage: make run rom=/path/to/game.z64 [input=<yaml>] [face=1|face=0]"; exit 1; }
	$(if $(input),PJ64_INPUT_YAML="$(input)") $(if $(face),PJ64_FACE=$(face)) ./$(BIN)/Project64 "$(rom)"

grid: all ## Run 1-16 ROMs in a grid (usage: make grid roms="a.z64 b.z64")
	@test -n "$(roms)" || { echo 'usage: make grid roms="a.z64 b.z64"'; exit 1; }
	./$(BIN)/Project64 --grid $(roms)

run-wizard: wizard ## Launch the binding wizard (writes a YAML next to the ROM or under Config/)
	./$(BIN)/Project64-wizard

rom-test: ## Run the ROM pack test harness (see Scripts/run_rom_pack.py --help)
	python3 Scripts/run_rom_pack.py

grid-selftest: ## Prove key broadcast across a grid of 4 tiles (usage: make grid-selftest rom=/path/to/game.z64)
	@test -n "$(rom)" || { echo "usage: make grid-selftest rom=/path/to/game.z64"; exit 1; }
	Scripts/grid_selftest.sh "$(rom)"

pointer-selftest: ## Prove the injected-pointer path maps a game click to A and mid1 to Start (usage: make pointer-selftest rom=/path/to/game.z64)
	@test -n "$(rom)" || { echo "usage: make pointer-selftest rom=/path/to/game.z64"; exit 1; }
	Scripts/pointer_selftest.sh "$(rom)"

face-selftest: ## Prove the injected-face path maps mouth-open to A and the head stick to the X axis, with no camera (usage: make face-selftest rom=/path/to/game.z64)
	@test -n "$(rom)" || { echo "usage: make face-selftest rom=/path/to/game.z64"; exit 1; }
	Scripts/face_selftest.sh "$(rom)"

wizard-selftest: wizard ## Prove the wizard's screens write the mapping they show, with no window and no camera
	Scripts/wizard_selftest.sh

# ── Unit tests ───────────────────────────────────────────────────────────────
# Five headless binaries, one recipe. Each is a Test.o plus the object(s) under test;
# all five link SDL3 and yaml-cpp, and the linker drops libraries nothing references,
# so a new test needs no link flags of its own. The per-test names AGENTS.md documents
# stay as aliases; `make unit-test` builds and runs all five.
UNIT_TESTS = input-config-test pointer-layout-test face-gesture-test game-config-test wizard-draft-test

$(BUILD)/input-config-test: $(BUILD)/Project64-sdl/InputConfigTest.o $(BUILD)/Project64-sdl/InputConfig.o
$(BUILD)/pointer-layout-test: $(BUILD)/Project64-sdl/PointerLayoutTest.o
$(BUILD)/face-gesture-test: $(BUILD)/Project64-sdl/FaceGesturesTest.o $(BUILD)/Project64-sdl/FaceGestures.o
$(BUILD)/game-config-test: $(BUILD)/Project64-sdl/GameConfigTest.o $(BUILD)/Project64-sdl/GameConfig.o
$(BUILD)/wizard-draft-test: $(BUILD)/Project64-wizard/WizardDraftTest.o $(BUILD)/Project64-wizard/WizardDraft.o $(BUILD)/Project64-sdl/InputConfig.o

$(BUILD)/%-test:
	$(CXX) $(LDFLAGS) -o $@ $^ $(SDL_LIBS) $(YAML_LIBS)

unit-test: $(addprefix $(BUILD)/, $(UNIT_TESTS)) ## Build and run all five headless unit tests
	@set -e; for t in $(UNIT_TESTS); do $(BUILD)/$$t; done
	@echo "ok: unit tests"

# One documented name per binary: `make face-gesture-test` builds then runs it.
$(UNIT_TESTS): %: $(BUILD)/%
	@$(BUILD)/$*

test: all ## Smoke test: frontend --version exits 0 and every plugin exports GetDllInfo
	./$(BIN)/Project64 --version
	@for p in $(PLUGIN_DYLIBS); do \
		nm -gU $$p | grep -q ' _GetDllInfo$$' || { echo "$$p: missing GetDllInfo"; exit 1; }; \
		echo "ok: $$p"; \
	done

clean: ## Remove build/macos, Bin/macOS and generated Version.h files
	rm -rf $(BUILD) $(BIN) $(VERSION_HEADERS)
	@echo "Cleanup complete."

# ── Pattern rules ────────────────────────────────────────────────────────────

$(BUILD)/%.o: $(SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.mm
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -fobjc-arc -c $< -o $@

$(BUILD)/%.o: $(SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies recorded by -MMD on the previous build. Absent on a clean tree,
# which is why this is '-include': there is nothing to be stale about yet.
-include $(ALL_OBJS:.o=.d)
