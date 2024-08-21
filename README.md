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
{ 'aaron-p1/handle_lua_errors.nvim', build = 'make' }
```

I have not actually tested installing this plugin with lazy.nvim.

**The important part is that you need to compile this plugin
with `make` before using.**

## Usage

```lua
local hle = require("handle_lua_errors")

-- for doing nothing on error
hle.set_on_error()

-- Not printing any errors may not be the best idea because you might want
-- messages like "Pattern not found" when searching.
-- You can still print single line messages with this
hle.set_on_error(true)

-- for printing error message as a normal message
hle.set_on_error(function(msg, multiline)
    -- `multiline` is false for single line messages
    -- like "E486: Pattern not found: my_search_pattern".
    -- You may still want these messages, so you can check for it
    -- and call the original function in that case.
    if multiline then
        print(msg)
    else
        hle.call_original(msg, multiline)
    end
end)

-- after that errors should be handled by your callback
error("Handled by your callback")

-- to reset to the original error handling
hle.reset_on_error()
```

## Inspiration

The plugin [gitsigns.nvim](https://github.com/lewis6991/gitsigns.nvim)
throws a lot more errors that other plugins on my machine.
I just don't care about these errors and I don't want to press a key to clear
my messages every time. (https://github.com/neovim/neovim/issues/22478)

I don't care about most errors from any source and if I do, I could set
a callback that puts the messages into a log file or hidden buffer.
I certainly do not want to be distracted by them.

(And I also wanted to see if it's possible to override lua errors)
