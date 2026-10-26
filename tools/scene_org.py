#!/usr/bin/env python3
"""scene_org.py — browse / search / fetch files.scene.org (freely distributable demos).

  scene_org.py ls <path>          list a directory, e.g. parties/1992/theparty92/
  scene_org.py find <term>        search the archive
  scene_org.py get <path> <dir>   download one file
SPDX-License-Identifier: GPL-3.0-or-later
"""
import re, sys, urllib.request, urllib.parse, os

BASE = "https://files.scene.org"
UA = {"User-Agent": "frank-amiga-bench/1.0"}

def fetch(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60).read()

def ls(path):
    html = fetch(f"{BASE}/browse/{path.strip('/')}/").decode("utf-8", "replace")
    out = []
    for cls, href, name, size in re.findall(
            r"<li class='file ([^']*)'><a href='([^']+)'><span class='filename'>([^<]+)</span> <span class='filesize'>([^<]+)</span>", html):
        if "parent" in cls: continue
        out.append((("d" if "directory" in cls else "f"), name, size, href))
    return out

def find(term):
    html = fetch(f"{BASE}/search/?q={urllib.parse.quote(term)}").decode("utf-8", "replace")
    return sorted(set(re.findall(r"href='(https://files\.scene\.org/view/[^']+)'", html)))

def get(path, outdir):
    path = path.strip("/")
    url = f"{BASE}/get/{path}"
    data = fetch(url)
    os.makedirs(outdir, exist_ok=True)
    dst = os.path.join(outdir, os.path.basename(path))
    open(dst, "wb").write(data)
    print(f"{dst} {len(data)} bytes")

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "ls":
        for t, n, s, h in ls(sys.argv[2]): print(t, n, s)
    elif cmd == "find":
        for v in find(sys.argv[2]): print(v.replace(BASE + "/view/", ""))
    elif cmd == "get":
        get(sys.argv[2], sys.argv[3])
