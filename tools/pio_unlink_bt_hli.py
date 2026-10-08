Import("env")

HLI_SYMBOL = "ld_include_hli_vectors_bt"

flags = list(env.get("LINKFLAGS", []))
kept = []
removed = False
i = 0
while i < len(flags):
    if flags[i] == "-u" and i + 1 < len(flags) and flags[i + 1] == HLI_SYMBOL:
        removed = True
        i += 2
        continue
    kept.append(flags[i])
    i += 1

if removed:
    env.Replace(LINKFLAGS=kept)
    print("pio_unlink_bt_hli: dropped -u %s" % HLI_SYMBOL)
else:
    print("pio_unlink_bt_hli: -u %s not present" % HLI_SYMBOL)
