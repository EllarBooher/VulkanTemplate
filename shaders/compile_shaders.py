import sys, argparse, pathlib, subprocess

parser = argparse.ArgumentParser(
	prog='compile_shaders',
	description='Compile shaders from glsl to spirv-v binary with glslangValidator');

parser.add_argument('-c', help='Absolute path to glslangValidator on system', type=pathlib.Path)

args = parser.parse_args()
glslangValidatorPath = args.c;

if glslangValidatorPath == None:
	raise Exception("Need valid glslangValidator path to compile shaders.")

print('Compiling shaders with ' + str(glslangValidatorPath), flush=True)

# TODO: figure out dependency files and integration with build system, which will become useful once glsl includes are needed
# OR we just build all shaders from scratch when they are changed, since compilation does not take long

def compile_shader(root, nameGLSL, nameSPIRV, defines=[]):
	rootPath = pathlib.Path(root);
	
	sourcePath = rootPath / nameGLSL;
	binaryPath = rootPath / nameSPIRV;

	print('Compiling ' + str(binaryPath))

	compileArgs = [ glslangValidatorPath, '-g', '-V', '--quiet', sourcePath, '-o', binaryPath]

	for define in defines:
		compileArgs.append('-D' + define)	

	subprocess.run(compileArgs, check=True)

compile_shader('./deferred/', 'gbuffer.frag', 'gbuffer.frag.spv')
compile_shader('./deferred/', 'gbuffer.vert', 'gbuffer.vert.spv')
compile_shader('./deferred/', 'light.comp', 'light.comp.spv')
compile_shader('./deferred/', 'ssao.comp', 'ssao.comp.spv')

compile_shader('./', 'geometry.frag', 'geometry.frag.spv')
compile_shader('./', 'geometry.vert', 'geometry.vert.spv')
compile_shader('./', 'oetf_srgb.comp', 'oetf_srgb.comp.spv')
compile_shader('./', 'testpattern.comp', 'testpattern.comp.spv')

compile_shader('./gaussian_blur/', 'gaussian_blur.comp', 'gaussian_blur.vertical.comp.spv', ['GAUSSIAN_BLUR_DIRECTION=0'])
compile_shader('./gaussian_blur/', 'gaussian_blur.comp', 'gaussian_blur.horizontal.comp.spv', ['GAUSSIAN_BLUR_DIRECTION=1'])
