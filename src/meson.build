build_opts = [
    '-D_ISOC11_SOURCE', '-D_XOPEN_SOURCE=700', '-U__STRICT_ANSI__',

    # Warnings
    '-Wundef', '-Wmissing-prototypes', '-Wshadow', '-Wparentheses',
    '-Wpointer-arith', '-Wno-pointer-sign',

    # Warnings to treat as errors
    '-Werror=implicit-function-declaration',
]

# Required dependencies
dependencies = [
    dependency('zlib'),
    dependency('sdl2'),
    dependency('libavcodec'),
    dependency('libavformat'),
    dependency('libavutil'),
    dependency('libswscale'),
    dependency('libswresample'),
    dependency('threads'),
    cc.find_library('m', required: false),
]

sources = [
    'AbyssEngine.c',

    'audio/AudioStream.c',

    'common/AbyssConfiguration.c',
    'common/FileManager.c',
    'common/Globals.c',
    'common/Logging.c',
    'common/MemoryStream.c',
    'common/MpqStream.c',
    'common/RingBuffer.c',

    'data/DataTable.c',
    'data/StringTable.c',

    'drawing/Button.c',
    'drawing/Cursor.c',
    'drawing/Label.c',
    'drawing/ListBox.c',
    'drawing/Sprite.c',

    'managers/AudioManager.c',
    'managers/InputManager.c',
    'managers/VideoManager.c',

    'scenes/Scene.c',
    'scenes/SceneCatalog.c',
    'scenes/SceneIntroVideos.c',
    'scenes/SceneMainMenu.c',

    'types/DC6.c',
    'types/DC6Frame.c',
    'types/Font.c',
    'types/MPQ.c',
    'types/MPQBlock.c',
    'types/MPQHash.c',
    'types/MPQHeader.c',
    'types/Palette.c',

    'util/BitReader.c',
    'util/Crypto.c',
    'util/Huffman.c',
    'util/Implode.c',
    'util/Mutex.c',
    'util/WavDecompress.c',
]

add_global_arguments(build_opts, language: 'c')

executable('AbyssEngine',
    install: true,
    sources: sources,
    dependencies: dependencies,
)

ring_buffer_test = executable('ring_buffer_test',
    sources: ['../tests/RingBufferTest.c', 'common/RingBuffer.c', 'util/Mutex.c'],
    c_args: ['-DNO_LOGGING'],
    dependencies: dependencies,
)
test('ring_buffer_test', ring_buffer_test)
