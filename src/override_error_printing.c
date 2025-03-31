#define _GNU_SOURCE

#define LUA_LIB
#include <lauxlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Reference to the Lua function passed as a parameter
int lua_emsg_multiline_ref = LUA_NOREF;

long page_size;

#define PATCH_SIZE 2 + 8 + 2
// holds data before patching
char original_emsg_multiline_data[PATCH_SIZE] = {0};
// holds data after patching
char patched_emsg_multiline_data[PATCH_SIZE] = {0};

bool is_patched = false;
bool handle_single_line = false;

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/lua/executor.c#L123
extern lua_State *get_global_lstate(void);
// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/lua/executor.c#L153
extern void nlua_error(lua_State *const lstate, const char *const msg);

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/message.c#L672
extern int emsg_not_now(void);
// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/message.c#L682
extern bool emsg_multiline(const char *s, const char *kind, int hl_id,
                           bool multiline);

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/highlight.h#L21
extern const char *hlf_names[];

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/strings.c#L508
extern char *vim_strchr(const char *const string, const int c);

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/ex_eval.c#L159
extern bool cause_errthrow(const char *mesg, bool multiline, bool severe,
                           bool *ignore);

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/globals.h
extern int emsg_off;
extern int emsg_silent;
extern bool emsg_severe;
extern int called_emsg;
extern int did_emsg;

// https://github.com/neovim/neovim/blob/70f3e15298b0b2c46c6bb48bb17dda273d4ab4b2/src/nvim/option_vars.h#L309
extern char *p_debug;

// Default highlight ID for error messages
int default_error_hl_id = 0;

// Find the default highlight ID for error messages
void find_default_error_hl_id(void) {
  // Search through hlf_names until we find "ErrorMsg"
  // or hit a reasonable limit (in 0.11 it was 68)
  for (int i = 1; i < 68 && hlf_names[i] != NULL; i++) {
    if (strcmp(hlf_names[i], "ErrorMsg") == 0) {
      default_error_hl_id = i;
      return;
    }
  }
  printf("handle_errors.nvim: Could not find ErrorMsg in hlf_names array\n");
  // Keep default_error_hl_id as 0 in case of error
}

// set memory to writable, write patch, set memory to executable
// https://reverseengineering.stackexchange.com/a/20399
void patch_mem(void *addr, char *patch) {
  void *aligned_address = (void *)((uint64_t)addr & ~(page_size - 1));
  if (mprotect(aligned_address, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) <
      0) {
    printf("handle_errors.nvim: mprotect failed");
    return;
  }
  memcpy(addr, patch, PATCH_SIZE);
  if (mprotect(aligned_address, page_size, PROT_READ | PROT_EXEC) < 0) {
    printf("handle_errors.nvim: mprotect failed");
    return;
  }
}

void apply_patch() {
  if (!is_patched) {
    patch_mem(emsg_multiline, patched_emsg_multiline_data);
    is_patched = true;
  }
}

void remove_patch() {
  if (is_patched) {
    patch_mem(emsg_multiline, original_emsg_multiline_data);
    is_patched = false;
  }
}

bool original_emsg_multiline(const char *s, const char *kind, int hl_id,
                             bool multiline) {
  bool was_patched = is_patched;

  remove_patch();
  bool result = emsg_multiline(s, kind, hl_id, multiline);
  if (was_patched) {
    apply_patch();
  }

  return result;
}

bool custom_emsg_multiline(const char *s, const char *kind, int hl_id,
                           bool multiline) {
  if (!handle_single_line && !multiline) {
    return original_emsg_multiline(s, kind, hl_id, multiline);
  }

  // until lua_State this code is based on the original function

  // Skip this if not giving error messages at the moment.
  if (emsg_not_now()) {
    return true;
  }

  // if is NULL the original would not print anything but still change variables
  if (emsg_off && vim_strchr(p_debug, 't') == NULL) {
    return original_emsg_multiline(s, kind, hl_id, multiline);
  }

  bool ignore = false;
  if (cause_errthrow(s, multiline, emsg_severe, &ignore)) {
    // this is above emsg_off in original function
    called_emsg++;

    if (!ignore) {
      did_emsg++;
    }
    return true;
  }

  if (emsg_silent != 0) {
    return original_emsg_multiline(s, kind, hl_id, multiline);
  }

  // if custom error lua function is set
  if (lua_emsg_multiline_ref != LUA_NOREF) {
    // remove patch to print error messages
    remove_patch();

    lua_State *L = get_global_lstate();

    lua_rawgeti(L, LUA_REGISTRYINDEX, lua_emsg_multiline_ref);

    // Check if the value retrieved is a function
    if (!lua_isfunction(L, -1)) {
      printf("Not a function: %d", lua_emsg_multiline_ref);
    }

    lua_pushstring(L, s);

    // Create options table with kind, hl_id, and multiline
    // Parameters:
    // - lua_State,
    // - arraysize (0 - we don't need array part),
    // - hashsize (3 fields to add)
    lua_createtable(L, 0, 3);

    // push kind to be able to use it in lua_setfield
    lua_pushstring(L, kind);
    // -2 refers to the table on the stack
    // (values we push are at -1, the table is one below)
    lua_setfield(L, -2, "kind");

    lua_pushinteger(L, hl_id);
    lua_setfield(L, -2, "hl_id");

    lua_pushboolean(L, multiline);
    lua_setfield(L, -2, "multiline");

    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
      nlua_error(
          L, "handle_errors.nvim: Error in error handling lua callback: %.*s");
    }
    apply_patch();
  }

  return true;
}

//@param msg string - The error message to display
//@param opts table? - Table containing options:
//@param opts.kind string? - The kind of message, defaults to `emsg`
//@param opts.hl_id integer? - The highlight ID to use, defaults to `ErrorMsg`
//@param opts.multiline boolean? - Whether to treat the message as multiline,
//                                 defaults to true if message contains newlines
//@return nil
int call_original(lua_State *L) {
  // Get required message parameter from Lua stack position 1
  const char *msg = luaL_checkstring(L, 1);

  // Default values for optional parameters
  const char *kind = "emsg";
  int hl_id = default_error_hl_id;
  // Default to true if message contains newlines
  bool multiline = strchr(msg, '\n') != NULL;

  // If options table is provided (stack position 2)
  if (lua_gettop(L) > 1 && lua_istable(L, 2)) {
    // Get kind if present
    lua_getfield(L, 2, "kind");
    if (!lua_isnil(L, -1)) {
      kind = luaL_checkstring(L, -1);
    }
    lua_pop(L, 1);

    // Get hl_id if present
    lua_getfield(L, 2, "hl_id");
    if (!lua_isnil(L, -1)) {
      hl_id = luaL_checkinteger(L, -1);
    }
    lua_pop(L, 1);

    // Get multiline if present
    lua_getfield(L, 2, "multiline");
    if (!lua_isnil(L, -1)) {
      multiline = lua_toboolean(L, -1);
    }
    lua_pop(L, 1);
  }

  original_emsg_multiline(msg, kind, hl_id, multiline);

  return 0;
}

//@param cb fun(msg: string, opts: table)
//@param handle_single_line boolean default false
int patch_emsg_multiline(lua_State *L) {
  int ref = LUA_NOREF;

  if (lua_isfunction(L, 1)) {
    lua_pushvalue(L, 1);
    ref = luaL_ref(L, LUA_REGISTRYINDEX);
  } else {
    return luaL_error(L, "Expected a function as the first argument");
  }

  // handle_single_line
  handle_single_line = lua_toboolean(L, 2);

  int old_lua_emsg_multiline_ref = lua_emsg_multiline_ref;

  lua_emsg_multiline_ref = ref;

  if (old_lua_emsg_multiline_ref != LUA_NOREF) {
    if (old_lua_emsg_multiline_ref != lua_emsg_multiline_ref) {
      luaL_unref(L, LUA_REGISTRYINDEX, old_lua_emsg_multiline_ref);
    }

    return 0;
  }

  apply_patch();

  return 0;
}

int set_to_original() {
  remove_patch();
  return 0;
}

// get function pointers and generate patch
void init() {
  find_default_error_hl_id();
  memcpy(original_emsg_multiline_data, emsg_multiline, PATCH_SIZE);

  // Create a patch that will replace the original function with a jump to our
  // custom function This is done by writing machine code directly into memory
  // that will:
  // 1. Load the address of our custom function into the RAX register
  // 2. Jump to that address

  // 0xb848 is the machine code for "mov rax, immediate_value"
  // This instruction tells the CPU to load a value directly into the RAX
  // register
  *(uint16_t *)(patched_emsg_multiline_data + 0x0) = 0xb848;

  // Store the address of our custom_emsg_multiline function
  // This will be loaded into RAX by the instruction above
  *(uint64_t *)(patched_emsg_multiline_data + 0x2) =
      (uint64_t)custom_emsg_multiline;

  // 0xe0ff is the machine code for "jmp rax"
  // This instruction tells the CPU to jump to the address stored in RAX
  // Since we loaded our custom function's address into RAX, this will jump to
  // our code
  *(uint16_t *)(patched_emsg_multiline_data + 0xa) = 0xe0ff;
}

// The entry point for the module
int luaopen_handle_errors_override_error_printing(lua_State *L) {
  page_size = sysconf(_SC_PAGESIZE);
  init();

  luaL_Reg functions[] = {{"call_original", call_original},
                          {"patch_emsg_multiline", patch_emsg_multiline},
                          {"set_to_original", set_to_original},
                          {NULL, NULL}};
  luaL_newlib(L, functions);
  return 1;
}
