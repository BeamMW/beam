#/bin/python

# Hello World program in Python
import sys

t_poll  = "function ___sys_poll"
t_proxy = "if (ENVIRONMENT_IS_PTHREAD) return"
t_ret   = "return"
t_end   = ");"

# read all input
text  = sys.stdin.read()

# find function sys_poll
ipoll = text.index(t_poll)

#add with AB declaration
text = text[:ipoll] + "const AB = new Int32Array(new SharedArrayBuffer(4));\n" + text[ipoll:]

# find start of proxy call
ipoll = text.index(t_poll)
iret  = text.index(t_proxy, ipoll)
iret  = text.index(t_ret, iret)

# replace with our changes
text = text[:iret] + '{\n  var ret =' + text[iret + len(t_ret):]

# find end of proxy call
iend  = text.index(t_end, iret)
text = text[:iend + len(t_end)] + '\n  if (ret == 0) Atomics.wait(AB, 0, 0, 50);\n  return ret;\n }' + text[iend + len(t_end):]

print(text)

