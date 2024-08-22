# A simple neovim plugin for providing your own callback for errors

This plugin lets you override the error printing in neovim e.g. to not print
lua errors, vimscript errors and `vim.notify` with log level error.
So no red messages should not appear, if you don't want them to.
This is accomplished by overriding the C function `emsg_multiline` in neovim.

> [!WARNING]
> This plugin calls neovim C functions directly. If they change, this plugin
> could crash neovim.
> So it may not work or crash in future versions of neovim.
>
> **Use it at your own risk.**

## Requirements

- Linux (MacOS may also work, Windows probably not)
- `x86_64` CPU architecture (because overriding the C function uses machine code)
- Neovim `0.10.0` or higher (tested with `0.10.1`)
- Neovim built with LuaJIT 2.1 or higher (`:=jit.version` to check)

(other versions may work but were not tested)

## Installation

### Compilation Requirements

- GnuMake
- GCC
- pkg-config
- LuaJIT

### Installation Steps

#### lazy.nvim

```lua
{ 'aaron-p1/handle_errors.nvim', build = 'make' }
```

I have not actually tested installing this plugin with lazy.nvim.

**The important part is that you need to compile this plugin
with `make` before using.**

## Usage

```lua
local he = require("handle_errors")

-- for doing nothing on multi line errors
he.set_on_error()

-- You can also hide all errors.
-- But that may not be the best idea because you still might want
-- messages like "Pattern not found" when searching.
he.set_on_error(true)

-- Example for putting multi line errors into a buffer
local err_bufnr = vim.api.nvim_create_buf(true, true)
vim.api.nvim_buf_set_name(err_bufnr, "ErrorLog")
he.set_on_error(function(msg)
    local lines = vim.split(msg, "\n")
    lines[#lines + 1] = ""
    lines[#lines + 1] = "Time: " .. os.date("%Y-%m-%d %H:%M:%S")
    lines[#lines + 1] = "---------------------------------------"
    lines[#lines + 1] = ""

    vim.api.nvim_buf_set_lines(err_bufnr, 1, 1, false, lines)
end)

-- for printing all messages
he.set_on_error(function(msg, ismultiline)
    print(msg)
end, true) -- set second parameter to `true` to also handle single line errors

-- after that errors should be handled by your callback
error("Handled by your callback")

-- to reset to the original error handling
he.reset_on_error()

-- the original error printing function can be called with
-- (msg: string, ismultiline: boolean)
he.call_original("msg", false)
```

## Why does this plugin exist

The plugin [gitsigns.nvim](https://github.com/lewis6991/gitsigns.nvim)
throws a lot more errors that other plugins on my machine.
I just don't care about these errors and I don't want to press a key to clear
my messages every time. (https://github.com/neovim/neovim/issues/22478)

I don't care about most errors from any source and if I do, I could set
a callback that puts the messages into a log file or hidden buffer.
I certainly do not want to be distracted by them.

(And I also wanted to see if it's possible to override lua errors)
