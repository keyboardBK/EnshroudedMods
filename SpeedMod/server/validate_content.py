"""Exercise the exact EML content DLL shipped in EMF 0.4.1, on an existing shadow only."""
import ctypes
import json
import sys
from pathlib import Path

shadow = Path(sys.argv[1]).resolve()
assert shadow.name.startswith("server-validation-") and shadow.parent.name == "SpeedMod"
dll = ctypes.CDLL(str(Path(sys.argv[2]).resolve()))
dll.emf_content_abi.restype = ctypes.c_uint32
assert dll.emf_content_abi() == 1
callback_type = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p)
lines = []


@callback_type
def log(_context, level, message):
    if level <= 2:
        lines.append(message.decode("utf-8", errors="replace"))


build = dll.emf_content_build
build.argtypes = [ctypes.c_char_p, callback_type, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                  ctypes.POINTER(ctypes.c_size_t)]
build.restype = ctypes.c_int32
reply = ctypes.create_string_buffer(1024 * 1024)
length = ctypes.c_size_t()
request = json.dumps({"game_dir": str(shadow), "file_name": "enshrouded", "force": True, "server": False})
code = build(request.encode(), log, None, reply, len(reply), ctypes.byref(length))
result = json.loads(reply.value)
print(json.dumps(result, ensure_ascii=False, indent=2))
for line in lines[-5:]:
    print(line)
assert code == 0 and result["code"] == 0 and not result.get("mod_errors")
assert [{"id": m["id"], "version": m["version"]} for m in result["mods"]] == [
    {"id": "SpeedModServerEffects", "version": "0.3.0"}]
