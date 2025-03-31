-- check necessary system requirements
if not jit then
  error("handle_errors.nvim: LuaJIT is required")
end

if jit.arch ~= "x64" then
  error("handle_errors.nvim: x64 architecture is required")
end

---@class (exact) ErrorOpts
---@field kind string? The kind of message, defaults to `emsg`
---@field hl_id integer? The highlight ID to use, defaults to `ErrorMsg`
---@field multiline boolean? Whether to treat the message as multiline, defaults to true if message contains newlines

local M = {}

local function empty_cb()
end

---Sets the error handler that gets called when an error string is about to
---be printed. (lua errors, vimscript errors, `vim.notify` with log level error)
---
---If cb is `nil` or `false` multiline errors will be hidden but single line errors will still be printed.
---If cb is `true` then both single and multiline errors will be hidden.
---
---@param cb nil|fun(msg: string, opts: ErrorOpts)|boolean
---@param also_single_line boolean? If `true`, also single line errors will be passed to the callback
function M.set_on_error(cb, also_single_line)
  local oep = require("handle_errors.override_error_printing")

  cb = cb or empty_cb

  if cb == true then
    also_single_line = true
    cb = empty_cb
  end

  oep.patch_emsg_multiline(cb, also_single_line)
end

---Sets the error handler to the original one
function M.reset_on_error()
  local oep = require("handle_errors.override_error_printing")

  oep.set_to_original()
end

---Calls the native print error function
---@param msg string
---@param opts ErrorOpts?
function M.call_original(msg, opts)
  local oep = require("handle_errors.override_error_printing")

  oep.call_original(msg, opts)
end

return M
