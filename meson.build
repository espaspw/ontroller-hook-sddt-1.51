mu3io_lib = static_library(
    'mu3io',
    name_prefix : '',
    include_directories : inc,
    implicit_include_directories : false,
    dependencies : [
        xinput_lib,
        cc.find_library('winusb'),
        cc.find_library('setupapi'),
        cc.find_library('shlwapi'),
    ],
    sources : [
        'config.c',
        'config.h',
        'leddata.h',
        'ledoutput.c',
        'ledoutput.h',
        'mu3io.c',
        'mu3io.h',
        'pipeimpl.c',
        'pipeimpl.h',
        'serialimpl.c',
        'serialimpl.h'
    ],
)
