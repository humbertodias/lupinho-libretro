# Read the Furnace link line out of a CMake Ninja file.
# Android builds a shared library; the other targets build an executable.
import sys

build = sys.argv[1]
# The core link line prefixes lupinho/src/ onto each relative archive.
marker = "lupinho/src/"
at = build.replace("\\", "/").find(marker)
rel_build = build[at + len(marker):] if at >= 0 else build
text = open(build + "/build.ninja", encoding="utf-8", errors="replace").read()
idx = -1
for marker in ("CXX_SHARED_LIBRARY_LINKER__furnace", "CXX_EXECUTABLE_LINKER__furnace"):
    start = 0
    while True:
        at = text.find(marker, start)
        if at < 0:
            break
        line_start = text.rfind("\n", 0, at) + 1
        if text[line_start:at].startswith("build "):
            idx = at
            break
        start = at + 1
    if idx >= 0:
        break
if idx < 0:
    sys.stderr.write("furnace ninja link target not found\n")
    sys.exit(1)
chunk = text[idx:idx + 2000000]
key = "\n  LINK_LIBRARIES = "
at = chunk.find(key)
if at < 0:
    sys.stderr.write("furnace link libraries not found\n")
    sys.exit(1)
rest = chunk[at + len(key):]
parts = []
for line in rest.splitlines():
    parts.append(line.strip().rstrip("$").strip())
    if not line.rstrip().endswith("$"):
        break
blob = " ".join(parts).replace("$$", "$").replace("$ ", " ").replace("\\", "/")
libs = []
for lib in blob.split():
    if (lib.endswith(".a") or lib.endswith(".lib")) and not lib.startswith("/") and not lib.startswith("-"):
        libs.append(rel_build.rstrip("/") + "/" + lib)
    else:
        libs.append(lib)
open(build + "/furengine-libs.txt", "w").write(" ".join(libs) + "\n")
print("link libs:", " ".join(libs))
