# A simple neovim plugin for providing your own lua error callback

This plugin overrides the `nlua_error` C function to let you provide your own
error callback that will be called when an error occurs in lua code.

> **Warning**
> This plugin uses unintended ways to override the `nlua_error` function.
> It may not work in the future versions of neovim or be otherwise unstable.
> I may have also done something wrong that might break something unexpectedly
> (unlikely),
> because I don't do much in C and don't know the neovim codebase well.
> I mostly used ChatGPT to write the C part of this plugin.
>
> **Use at your own risk.**

## Requirements

- Linux (MacOS may also work, Windows probably not)
- x86_64 architecture (because overriding `nlua_error` uses machine code)
- Neovim `0.10.0` or higher (tested with `0.10.1`)
- Neovim built with LuaJIT 2.1 or higher (`:=jit.version` to check)

(other versions may work but were not tested)

## Installation

### Compilation Requirements

- GnuMake
- GCC
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

-- for printing error message as a normal message
hle.set_on_error(function(msg)
    print(msg)
end)

-- after that errors should be handled by your callback
error("Handled by your callback")
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
