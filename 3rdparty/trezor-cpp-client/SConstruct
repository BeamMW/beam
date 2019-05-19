src_files = []
libs = []
cpp_flags = []
opt_flags = []

#env = Environment(ENV=os.environ, CFLAGS=ARGUMENTS.get('CFLAGS', ''))

src_files += [
    'src/messages/messages.pb.cc',
    'src/messages/messages-common.pb.cc',
    'src/messages/messages-management.pb.cc',
    'src/messages/messages-beam.pb.cc',
    'main.cpp',
]

libs += [
    'pthread',
    'protobuf',
    'curl',
]

cpp_flags += [
    '-I.',
    '-Isrc',
    '-Isrc/messages/',
]

opt_flags += ['-O3']
#,  '-g3']

opt_flags += [
    '-pedantic',
    '-Wall',
    '-Wextra',
    '-Wcast-align',
    '-Wcast-qual',
    '-Wctor-dtor-privacy',
    '-Wdisabled-optimization',
    '-Wformat=2',
    '-Winit-self',
    '-Wlogical-op',
    '-Wmissing-include-dirs',
    '-Wnoexcept',
    '-Wold-style-cast',
    '-Woverloaded-virtual',
    '-Wredundant-decls',
    '-Wshadow',
    '-Wsign-conversion',
    '-Wsign-promo',
    '-Wstrict-null-sentinel',
    '-Wstrict-overflow=5',
    '-Wswitch-default',
    '-Wno-unused',
#    '-Wundef',
#    '-Wmissing-declarations',
]

cpp_flags += opt_flags

Program(target='build/main',
        source=src_files,
        CPPFLAGS=cpp_flags,
        LIBS=libs,
        LIBPATH=['/usr/local/lib'],
)
