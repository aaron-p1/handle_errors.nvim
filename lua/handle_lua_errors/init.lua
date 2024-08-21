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

local function only_single_line_cb(msg, multiline)
  if not multiline then
    M.call_original(msg, multiline)
  end
end

---Sets the error handler that gets called when an error string is about to
---be printed. (lua errors, vimscript errors, `vim.notify` with log level error)
---@param cb fun(msg: string, multiline: boolean)?|boolean Handler callback, `nil` or `false` for nothing, `true` for printing only single line errors
function M.set_on_error(cb)
  local oep = require("handle_lua_errors.override_error_printing")

  cb = cb or empty_cb

  if cb == true then
    cb = only_single_line_cb
  end

  oep.patch_emsg_multiline(cb)
end

---Sets the error handler to the original one
function M.reset_on_error()
  local oep = require("handle_lua_errors.override_error_printing")

  oep.set_to_original()
end

---Calls the native print error function
---@param msg string
---@param multiline boolean
function M.call_original(msg, multiline)
  local oep = require("handle_lua_errors.override_error_printing")

  oep.call_original(msg, multiline)
end

return M
