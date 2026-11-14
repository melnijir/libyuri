# Set name of the module
SET (MODULE mutter_screen)

# Screen capture of GNOME: org.gnome.Mutter.ScreenCast over D-Bus (GIO), picture through PipeWire
SET (SRC MutterScreen.cpp
		 MutterScreen.h)

add_library(${MODULE} MODULE ${SRC})
target_link_libraries(${MODULE} ${LIBNAME} PkgConfig::PIPEWIRE PkgConfig::SPA PkgConfig::GIO)

YURI_INSTALL_MODULE(${MODULE})
