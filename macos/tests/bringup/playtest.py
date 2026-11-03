# playtest.py <scenario-file> [out-dir]: drives faf_game through macos/run_game.sh with
# synthetic input (FAF_SHELL_TEST_INPUT, shell/TestInput.cpp) and captures frames
# (VK_LAYER_LUNARG_screenshot), so a front-end path can be replayed unattended and looked at.
#
# A scenario file has one step per line ('#' starts a comment). Frame numbers count the
# WM_PAINT messages the game has taken (one per presented frame, 60 a second):
#   @<frame>                 later lines start at this frame (absolute)
#   +<frames>                advance the current frame
#   click X Y [B]            move, then button B (1 left, 2 middle, 3 right) down and up two
#                            frames later; advances 6 frames
#   dclick X Y               double click (left)
#   drag X Y X2 Y2 [B] [N]   button B down at (X,Y), N relative moves (default 8) by
#                            (X2-X, Y2-Y) in all, button up where the pointer then is
#   tap KEY [MOD...]         KEY pressed and released, with modifiers (Shift, Ctrl, Alt, Cmd
#                            held around it); advances 6 frames
#   shot [name]              capture the next rendered frame with the engine's frame dump
#                            (shots/<frame>-<name>.png); advances 3 frames, so input that
#                            follows is not in the picture
#   vkshot                   capture this presented frame with VK_LAYER_LUNARG_screenshot
#                            (shots/<n>.png; the layer counts presents, which run ahead of
#                            the WM_PAINT count by the frames presented during start-up)
#   env NAME=VALUE           environment for the game (e.g. SIZE=prefs)
#   arg SWITCH...            extra faf_game switches
#   time S                   watchdog seconds (default: last frame / 60 + 40)
#   prefs NAME               prefs file (default frontend-test.prefs, reset from
#                            build/m4-frontend-tests/template.prefs before each run if present)
#   template FILE            reset the prefs file from FILE (relative to the scenario's folder,
#                            or absolute) before the run, instead of template.prefs
#   anything else            a raw TestInput step at the current frame ("move X Y",
#                            "key Up down", "text hello", "wheel X Y N", "focus lost", ...)
# Output: <out-dir>/game.log, shots/*.png, steps.txt (the expanded script), playtest.out.
# Python 3 stdlib only: python3 -I macos/tests/bringup/playtest.py scenario.txt out/
import os, shlex, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(os.path.dirname(here)))
MODS = {"Shift": "Left Shift", "Ctrl": "Left Ctrl", "Alt": "Left Alt", "Cmd": "Left GUI"}
prefs_dir = os.path.expanduser(
    "~/Library/Application Support/Gas Powered Games/Supreme Commander Forged Alliance")

def main():
    scen = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        root, "build", "m4-frontend-tests", os.path.splitext(os.path.basename(scen))[0])
    out = os.path.abspath(out)
    os.makedirs(out, exist_ok=True)
    steps, shots, env, args = [], [], {}, []
    frame, timeout, prefs, template = 0, None, "frontend-test.prefs", None
    for raw in open(scen):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        w = line.split()
        op = w[0]
        if op.startswith("@"):
            frame = int(op[1:])
        elif op.startswith("+"):
            frame += int(op[1:])
        elif op == "click":
            x, y = w[1], w[2]
            b = w[3] if len(w) > 3 else "1"
            steps += ["%d:move %s %s" % (frame, x, y), "%d:down %s %s %s" % (frame + 2, x, y, b),
                      "%d:up %s %s %s" % (frame + 4, x, y, b)]
            frame += 6
        elif op == "dclick":
            x, y = w[1], w[2]
            steps += ["%d:move %s %s" % (frame, x, y), "%d:down %s %s 1" % (frame + 2, x, y),
                      "%d:up %s %s 1" % (frame + 3, x, y), "%d:down %s %s 1 2" % (frame + 4, x, y),
                      "%d:up %s %s 1 2" % (frame + 5, x, y)]
            frame += 8
        elif op == "drag":
            x, y, x2, y2 = (float(v) for v in w[1:5])
            b = w[5] if len(w) > 5 else "1"
            n = int(w[6]) if len(w) > 6 else 8
            steps += ["%d:move %g %g" % (frame, x, y), "%d:down %g %g %s" % (frame + 2, x, y, b)]
            f = frame + 4
            # Relative motion, as a physical mouse moves (the middle-drag camera warps the
            # pointer back to its anchor every frame).
            for i in range(n):
                steps.append("%d:rel %g %g" % (f, (x2 - x) / n, (y2 - y) / n))
                f += 2
            steps.append("%d:up %s %s" % (f + 2, "here", b))
            frame = f + 6
        elif op == "tap":
            key, mods = w[1], w[2:]
            f = frame
            for m in mods:
                steps.append("%d:key %s down" % (f, MODS[m]))
                f += 1
            steps += ["%d:key %s down" % (f, key), "%d:key %s up" % (f + 2, key)]
            f += 3
            for m in reversed(mods):
                steps.append("%d:key %s up" % (f, MODS[m]))
                f += 1
            frame = max(frame + 6, f + 1)
        elif op == "shot":
            steps.append("%d:shot %s" % (frame, w[1] if len(w) > 1 else "shot"))
            frame += 3
        elif op == "vkshot":
            shots.append(frame)
        elif op == "env":
            k, v = line[4:].strip().split("=", 1)
            env[k] = v
        elif op == "arg":
            args += shlex.split(line[4:])
        elif op == "time":
            timeout = int(w[1])
        elif op == "prefs":
            prefs = w[1]
        elif op == "template":
            template = os.path.join(os.path.dirname(os.path.abspath(scen)), w[1])
        else:
            steps.append("%d:%s" % (frame, line))
    if template is None and prefs == "frontend-test.prefs":
        template = os.path.join(root, "build", "m4-frontend-tests", "template.prefs")
    if template is not None and os.path.exists(template):
        with open(template, "rb") as src, open(os.path.join(prefs_dir, prefs), "wb") as dst:
            dst.write(src.read())
    with open(os.path.join(out, "steps.txt"), "w") as f:
        f.write("\n".join(steps) + "\n")
    # dump_Frame splits its argument at spaces: reach the folder through a path without one
    # (~/Gaming/.faf/faf-re links to the repository on this machine).
    dumps = os.path.join(out, "dumps")
    alias = os.path.expanduser("~/Gaming/.faf/faf-re")
    if " " in dumps and os.path.realpath(alias) == os.path.realpath(root):
        dumps = alias + dumps[len(os.path.realpath(root)):]
    if os.path.isdir(dumps):
        for d in os.listdir(dumps):
            for f in os.listdir(os.path.join(dumps, d)):
                os.remove(os.path.join(dumps, d, f))
            os.rmdir(os.path.join(dumps, d))
    e = dict(os.environ)
    e.update({"FAF_SHELL_TEST_INPUT": ";".join(steps), "FAF_SHELL_TEST_SHOTS": dumps,
              "OUT": out, "PREFS": prefs,
              "TIME": str(timeout if timeout else frame // 60 + 40)})
    e.setdefault("SIZE", "1280 800")
    e.setdefault("FAF_GAME_EXE", os.path.join(root, "build", "m4-frontend", "faf_game"))
    if shots:
        e["SHOTS"] = ",".join(str(s) for s in sorted(set(shots)))
    e.update(env)
    with open(os.path.join(out, "playtest.out"), "w") as log:
        rc = subprocess.call([os.path.join(root, "macos", "run_game.sh")] + args, env=e,
                             stdout=log, stderr=subprocess.STDOUT)
    # The frame dumps: <frame>-<name>/SCFrame_*.bmp -> shots/<frame>-<name>.png.
    shotdir = os.path.join(out, "shots")
    os.makedirs(shotdir, exist_ok=True)
    made = []
    if os.path.isdir(dumps):
        for d in sorted(os.listdir(dumps), key=lambda n: int(n.split("-")[0])):
            bmps = sorted(f for f in os.listdir(os.path.join(dumps, d)) if f.lower().endswith(".bmp"))
            if not bmps:
                made.append("%s: no frame written" % d)
                continue
            src = os.path.join(dumps, d, bmps[0])
            subprocess.call([sys.executable, "-I", os.path.join(here, "bmp2png.py"), src],
                            stdout=subprocess.DEVNULL)
            png = src[:-4] + ".png"
            if os.path.exists(png):
                os.replace(png, os.path.join(shotdir, d + ".png"))
                made.append(os.path.join(shotdir, d + ".png"))
    print(open(os.path.join(out, "playtest.out")).read().rstrip())
    for m in made:
        print("shot: " + m)
    sys.exit(rc)

main()
