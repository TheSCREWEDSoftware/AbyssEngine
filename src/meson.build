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
if host_machine.system() == 'windows'
    dependencies += [cc.find_library('ws2_32')] # multiplayer
endif

sources = [
    'AbyssEngine.c',

    'audio/AudioStream.c',
    'audio/SoundEffect.c',
    'audio/Sounds.c',

    'common/AbyssConfiguration.c',
    'common/CrashHandler.c',
    'common/FileManager.c',
    'common/Globals.c',
    'common/Logging.c',
    'common/MemoryStream.c',
    'common/MpqStream.c',
    'common/Revision.c',
    'common/RingBuffer.c',

    'map/Automap.c',
    'map/LevelBuilder.c',
    'map/LevelTypes.c',
    'map/Map.c',
    'map/MapHistory.c',
    'map/MapRenderer.c',
    'map/MapSession.c',
    'map/MapUnits.c',
    'map/MonsterStats.c',
    'map/Objects.c',
    'map/Pathfinding.c',
    'map/Places.c',
    'map/World.c',

    'data/DataTable.c',
    'data/StringTable.c',

    'game/Character.c',
    'game/ControlPresets.c',
    'game/Controls.c',
    'game/ControlsExport.c',
    'game/Inventory.c',
    'game/Items.c',
    'game/Settings.c',
    'game/Skills.c',

    'drawing/Button.c',
    'drawing/Checkbox.c',
    'drawing/ConfirmDialog.c',
    'drawing/Cursor.c',
    'drawing/Dropdown.c',
    'drawing/Figure.c',
    'drawing/Label.c',
    'drawing/ListBox.c',
    'drawing/LoadingScreen.c',
    'drawing/Picture.c',
    'drawing/Screenshot.c',
    'drawing/Slider.c',
    'drawing/Sprite.c',
    'drawing/TabBar.c',
    'drawing/TextBox.c',

    'managers/AudioManager.c',
    'managers/InputManager.c',
    'managers/VideoManager.c',

    'net/Client.c',
    'net/Multiplayer.c',
    'net/NetMessage.c',
    'net/Protocol.c',
    'net/Server.c',
    'net/Socket.c',

    'scenes/CatalogCharacters.c',
    'scenes/CatalogClasses.c',
    'scenes/CatalogSprites.c',
    'scenes/CatalogTheme.c',
    'scenes/GameInventory.c',
    'scenes/GameSkills.c',
    'scenes/Scene.c',
    'scenes/SceneCatalog.c',
    'scenes/SceneCharacterCreate.c',
    'scenes/SceneCharacterSelect.c',
    'scenes/SceneIntroVideos.c',
    'scenes/SceneMainMenu.c',
    'scenes/SceneMapEditor.c',
    'scenes/SceneMultiplayer.c',
    'scenes/SceneOptions.c',
    'scenes/SceneRoam.c',

    'types/COF.c',
    'types/DC6.c',
    'types/DC6Frame.c',
    'types/DCC.c',
    'types/DS1.c',
    'types/DT1.c',
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

if get_option('debug_skip_intro')
    build_opts += ['-DABYSS_DEBUG_SKIP_INTRO']
endif
if get_option('debug_show_catalog')
    build_opts += ['-DABYSS_DEBUG_SHOW_CATALOG']
endif
if get_option('debug_show_map_editor')
    build_opts += ['-DABYSS_DEBUG_SHOW_MAP_EDITOR']
endif

build_opts += ['-DABYSS_BUILD_TYPE="' + get_option('buildtype') + '"']
add_global_arguments(build_opts, language: 'c')

# Git commit shown on the main menu (read when the build is configured; see cmake/genrev.cmake for CMake)
git = find_program('git', required: false)
rev = configuration_data()
rev.set('rev_hash', 'unknown')
rev.set('rev_branch', 'local build')
rev.set('rev_date', 'unknown')
rev.set('rev_linkage', get_option('default_library') == 'static' ? 'Static' : 'Dynamic')
if git.found()
    git_hash = run_command(git, 'describe', '--always', '--dirty=+', '--abbrev=12', '--match', 'NO_TAGS_PLEASE', check: false)
    if git_hash.returncode() == 0
        git_date = run_command(git, 'show', '-s', '--format=%ci', check: false)
        git_branch = run_command(git, 'rev-parse', '--abbrev-ref', 'HEAD', check: false)
        rev.set('rev_hash', git_hash.stdout().strip())
        rev.set('rev_date', git_date.stdout().strip())
        rev.set('rev_branch', git_branch.stdout().strip())
    endif
endif
configure_file(input: 'common/revision_data.h.in', output: 'revision_data.h', configuration: rev)

crash_link_args = host_machine.system() == 'windows' ? ['-ldbghelp'] : ['-rdynamic'] # crash reports
executable('AbyssEngine',
    install: true,
    sources: sources,
    dependencies: dependencies,
    link_args: crash_link_args,
)

# abyss-lobby: the list of online games for Multiplayer > Browse > Online (a small program of its own, no SDL)
if get_option('build_lobby_server')
    executable('abyss-lobby',
        install: true,
        sources: ['lobby/Lobby.c', 'net/Socket.c'],
        dependencies: host_machine.system() == 'windows' ? [cc.find_library('ws2_32')] : [],
    )
endif

ring_buffer_test = executable('ring_buffer_test',
    sources: ['../tests/RingBufferTest.c', 'common/RingBuffer.c', 'util/Mutex.c'],
    c_args: ['-DNO_LOGGING'],
    dependencies: dependencies,
)
test('ring_buffer_test', ring_buffer_test)
