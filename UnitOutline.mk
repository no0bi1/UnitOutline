# clang-cl cross build, same toolchain as UnitXP SP3. Point the two variables at your copies:
#   make XWIN_ROOT=... MINHOOK_DIR=... [CXXDEFS=/DOUTLINE_DEBUG]
TRIPLE     = i686-pc-windows-msvc
CXX        = clang-cl
LD         = lld-link
XWIN_ROOT ?= ./xwinSDK
MINHOOK_DIR ?= ./MinHook_134

TARGET = UnitOutline.dll
SRCS = UnitOutline.cpp $(MINHOOK_DIR)/src/buffer.c $(MINHOOK_DIR)/src/hde/hde32.c $(MINHOOK_DIR)/src/hook.c $(MINHOOK_DIR)/src/trampoline.c
OBJS = $(patsubst %.cpp,%.obj,$(patsubst %.c,%.obj,$(SRCS)))

CXXFLAGS = --target=$(TRIPLE) /O1 /MT /EHsc /W3 -Wno-microsoft-cast /nologo /hotpatch /arch:SSE2 /fp:precise \
           /std:c++17 /DWIN32_LEAN_AND_MEAN /D_X86_ /D_WIN32 /DNULL=0 $(CXXDEFS) \
           -imsvc "$(XWIN_ROOT)/crt/include" -imsvc "$(XWIN_ROOT)/sdk/include/ucrt" \
           -imsvc "$(XWIN_ROOT)/sdk/include/um" -imsvc "$(XWIN_ROOT)/sdk/include/shared" \
           -imsvc "$(MINHOOK_DIR)/include"
LDFLAGS  = /NOLOGO /DLL /MACHINE:X86 /OPT:REF /OPT:ICF /SAFESEH /NXCOMPAT \
           /LIBPATH:"$(XWIN_ROOT)/crt/lib/x86" /LIBPATH:"$(XWIN_ROOT)/sdk/lib/ucrt/x86" /LIBPATH:"$(XWIN_ROOT)/sdk/lib/um/x86"

all: $(TARGET)
$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) /OUT:$@ $(OBJS) user32.lib kernel32.lib libcmt.lib
%.obj: %.cpp
	$(CXX) $(CXXFLAGS) /c $< /Fo$@
%.obj: %.c
	$(CXX) $(CXXFLAGS) /c $< /Fo$@
clean:
	rm -f $(OBJS) *.dll *.lib *.exp *.pdb
.PHONY: all clean
