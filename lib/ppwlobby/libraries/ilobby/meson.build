libdwcilobby_srcs = files(
    'src/dwci_lobby.cpp',
    'src/dwci_lobbybase.cpp',
    'src/dwci_lobbynewhandler.cpp',
    'src/dwci_lobbycallback.cpp',
    'src/dwci_lobbydata.cpp'
)

# TODO: NX build does not work because switch.h required c++>=11
# Android: clang rejects the 64-bit pointer truncations gcc accepts with -fpermissive
if(build_target != 'nx' and build_target != 'android')
libdwcilobby = static_library('dwcilobby',
    sources: libdwcilobby_srcs,
    c_args: ppwlobby_c_args,
    cpp_args: cpp_args + ppwlobby_c_args,
    include_directories: [public_includes, ppwlobby_public_includes],
    dependencies: [nitrosdk_dep, nitrodwc_dep, nitrowifi_dep],
    pic: (build_target == 'nx'),
    native: (build_target == 'linux' or build_target == 'win64')
)

ppwlobby_libs += libdwcilobby
endif
