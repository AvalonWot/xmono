--通过const_cmdid.def生成Python可以使用的id文件

function CMDID(t, id, num)
    print(id, "=", num)
end

header = [[
# -*- coding: utf-8 -*-
# 该文件通过parse_cmdid.lua自动生成
]]
print(header)
f = loadfile("const_cmdid.def")
f()
print("\n")