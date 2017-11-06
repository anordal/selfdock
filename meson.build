project('selfdock', 'c',
	meson_version: '>=0.33.0',
	default_options: ['c_std=gnu99', 'warning_level=3'],
	license: 'MPL 2.0'
)
prefix = get_option('prefix')
rootoverlay = prefix + '/share/selfdock'

narg = dependency('narg', fallback: ['narg', 'narg_dep'])

selfdock = executable(
	'selfdock', 'selfdock.c',
	dependencies: narg,
	c_args: '-DROOTOVERLAY=' + rootoverlay,
	install: false
)

meson.add_install_script('install.mk')
setprefix = 'MESON_INSTALL_PREFIX=' + prefix
run_target('uninstall-actual', command: ['make', '-f', 'install.mk', setprefix, 'uninstall'])
run_target('moduletest', command: ['make', '-f', 'install.mk', setprefix, 'test', '-j2'])
