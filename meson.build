project('SCXvid', 'cpp',
    default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++17'],
    license: 'GPL-2.0',
    license_files: 'LICENSE',
    meson_version: '>=1.2.3',
    version: '2.0',
)

sources = [
     'src/scxvid.cpp',
]

xvid_dep = dependency('xvidcore')

deps = [xvid_dep]

incdir = include_directories(
    run_command(
        find_program('python', 'python3'),
        '-c',
        'import vapoursynth as vs; print(vs.get_include())',
        check: true,
    ).stdout().strip(),
)

py = import('python').find_installation(pure: false)

shared_module('scxvid',
    sources,
    dependencies : deps,
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    install: true,
    install_dir: py.get_install_dir() / 'vapoursynth/plugins',
    name_prefix: '',
)