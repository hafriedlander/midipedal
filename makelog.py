
import math

logs = [
"#include <Arduino.h>",
"uint16_t fixed_reverselog(uint16_t x) { ",
"  switch(x) {",
"    case 0: return 0;"
]


for x in range(1,16383):
    logs.append("    case "+str(x)+": return "+str(int(math.pow(16383, float(x)/16383))) + ";")

logs.append("  }")
logs.append("return 16381;")
logs.append("}")

open("fixedlog.cc", "w").write("\n".join(logs))
