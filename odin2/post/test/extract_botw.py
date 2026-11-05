"""Extrai shaders GLSL embutidos em runtime/nvn/vulkan/shaders.c do BOTWRecompAndroid (strings C)."""
import ast
import re
import sys

src = open(sys.argv[1]).read()
out = sys.argv[2].rstrip('/') + '/'
for name in ['post.vert', 'fxaa.frag', 'easu.frag', 'rcas.frag', 'sgsr1.frag']:
    m = re.search(r'if \(!strcmp\(name, "%s"\)\) \{.*?return (.*?);\n    \}' % re.escape(name), src, re.S)
    if not m:
        sys.exit('não encontrado: ' + name)
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))
    open(out + name, 'w').write(''.join(ast.literal_eval('"%s"' % p) for p in parts))
