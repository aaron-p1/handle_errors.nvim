-- check necessary system requirements
if not jit then
  error("handle_lua_errors plugin: LuaJIT is required")
end

if jit.arch ~= "x64" then
  error("handle_lua_errors plugin: x64 architecture is required")
end

local M = {}

local function empty_cb()
end

---sets the function that gets called when a lua error occurs
---@param cb fun(traceback: string)?
function M.set_on_error(cb)
  local op = require("handle_lua_errors.override_nlua_error")
  op.replace_nlua_error(cb or empty_cb)
end

return M
