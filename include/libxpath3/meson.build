xp3version_conf = configuration_data()
xp3version_conf.set('XP3_VERSION', version)
xp3version_conf.set('XP3_VERSION_MAJOR', version_parts[0].to_int())
xp3version_conf.set('XP3_VERSION_MINOR', version_parts[1].to_int())
xp3version_conf.set('XP3_VERSION_MICRO', version_parts[2].to_int())

xp3version_h = configure_file(
    input: 'xp3version.h.in',
    output: 'xp3version.h',
    configuration: xp3version_conf,
    install: true,
    install_dir: get_option('includedir') / 'libxpath3',
)

install_headers('xpath3.h', subdir: 'libxpath3')
