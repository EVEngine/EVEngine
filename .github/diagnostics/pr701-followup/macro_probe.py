from pathlib import Path
s=Path('src/modules/graphics/webgpu/Graphics.cpp').read_text(encoding='utf-8')
start=s.index('#include <SDL2/SDL.h>')
end=s.index('#if defined(__APPLE__)',start)
block=s[start:end]
tail='\nint main(){auto status=wgpu::PopErrorScopeStatus::Success;return status==wgpu::PopErrorScopeStatus::Success?0:1;}\n'
Path('/tmp/pr701-after.cpp').write_text('#include <webgpu/webgpu_cpp.h>\n'+block+tail,encoding='utf-8')
old=block.replace('#ifdef Success\n#undef Success\n#endif\n','')
Path('/tmp/pr701-before.cpp').write_text('#include <webgpu/webgpu_cpp.h>\n'+old+tail,encoding='utf-8')
