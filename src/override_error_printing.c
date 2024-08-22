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

// /src/nvim/lua/executor.c
extern lua_State *get_global_lstate(void);
extern void nlua_error(lua_State *const lstate, const char *const msg);

// /src/nvim/message.c
extern int emsg_not_now(void);
extern bool emsg_multiline(const char *, bool);

// /src/nvim/strings.c
extern char *vim_strchr(const char *const string, const int c);

// /src/nvim/ex_eval.c
extern bool cause_errthrow(const char *mesg, bool multiline, bool severe,
                           bool *ignore);

// /src/nvim/globals.h
extern int emsg_off;
extern int emsg_silent;
extern bool emsg_severe;
extern int called_emsg;
extern int did_emsg;

// /src/nvim/option_vars.h
extern char *p_debug;

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

bool original_emsg_multiline(const char *s, bool multiline) {
  bool was_patched = is_patched;

  remove_patch();
  bool result = emsg_multiline(s, multiline);
  if (was_patched) {
    apply_patch();
  }

  return result;
}

bool custom_emsg_multiline(const char *s, bool multiline) {
  if (!handle_single_line && !multiline) {
    return original_emsg_multiline(s, multiline);
  }

  // until lua_State this code is based on the original function

  // Skip this if not giving error messages at the moment.
  if (emsg_not_now()) {
    return true;
  }

  if (emsg_off && vim_strchr(p_debug, 't') == NULL) {
    return original_emsg_multiline(s, multiline);
  }

  bool ignore = false;
  if (cause_errthrow(s, multiline, emsg_severe, &ignore)) {
    // in original function
    called_emsg++;
    if (!ignore) {
      did_emsg++;
    }
    return true;
  }

  if (emsg_silent != 0) {
    return original_emsg_multiline(s, multiline);
  }

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
    lua_pushboolean(L, multiline);

    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
      nlua_error(
          L, "handle_errors.nvim: Error in error handling lua callback: %.*s");
    }
    apply_patch();
  }

  return true;
}

//@param msg string
//@param multiline boolean default true
int call_original(lua_State *L) {
  const char *msg = luaL_checkstring(L, 1);
  bool multiline = true;

  if (lua_gettop(L) > 1) {
    multiline = lua_toboolean(L, 2);
  }

  original_emsg_multiline(msg, multiline);

  return 0;
}

//@param cb fun(msg: string, multiline: boolean)
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
  memcpy(original_emsg_multiline_data, emsg_multiline, PATCH_SIZE);

  // Patch to replace memory location with a custom jump
  // mov rax, Iv
  *(uint16_t *)(patched_emsg_multiline_data + 0x0) = 0xb848;
  // mov rax, jump_destination
  *(uint64_t *)(patched_emsg_multiline_data + 0x2) =
      (uint64_t)custom_emsg_multiline;
  // jmp rax
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
