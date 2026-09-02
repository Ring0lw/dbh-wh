CXX := clang++
IMGUI := imgui

CXXFLAGS := -std=c++26 -O2 -g -fPIC -fvisibility=hidden -Wall -Wextra -Wno-unused-parameter \
            -I$(IMGUI) -I$(IMGUI)/backends \
            -DVK_NO_PROTOTYPES -DIMGUI_IMPL_VULKAN_NO_PROTOTYPES \
            -DIMGUI_DISABLE_DEMO_WINDOWS -DIMGUI_DISABLE_DEBUG_TOOLS
LDFLAGS := -shared -fuse-ld=lld -Wl,--no-undefined -Wl,--exclude-libs,ALL -static-libstdc++ -lm -ldl -lpthread

TARGET := libdbh_esp.so
MANIFEST := dbh_esp.json
INSTALL_DIR := $(HOME)/.local/share/vulkan/implicit_layer.d

SRC := layer.cxx draw.cxx esp.cxx game.cxx lua.cxx input.cxx menu.cxx mem.cxx lg.cxx
IMGUI_SRC := $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp \
             $(IMGUI)/imgui_widgets.cpp $(IMGUI)/backends/imgui_impl_vulkan.cpp

OBJ := $(patsubst %.cxx,obj/%.o,$(SRC)) $(patsubst $(IMGUI)/%.cpp,obj/imgui/%.o,$(IMGUI_SRC))

all: $(TARGET)

obj/%.o: src/%.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

obj/imgui/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -w -c $< -o $@

$(TARGET): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)

install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	rm -f $(INSTALL_DIR)/$(MANIFEST)
	sed 's#"library_path": ".*"#"library_path": "$(CURDIR)/$(TARGET)"#' $(MANIFEST) > $(INSTALL_DIR)/$(MANIFEST)

uninstall:
	rm -f $(INSTALL_DIR)/$(MANIFEST)

clean:
	rm -rf obj $(TARGET)

.PHONY: all install uninstall clean
