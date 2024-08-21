#define _GNU_SOURCE

#define LUA_LIB
#include <dlfcn.h>
#include <lauxlib.h>
#include <lua.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Reference to the Lua function passed as a parameter
int lua_emsg_multiline_ref = LUA_NOREF;

long page_size;

#define PATCH_SIZE 2 + 8 + 2
// holds data before patching
char original_emsg_multiline[PATCH_SIZE] = {0};
// holds data after patching
char patched_emsg_multiline[PATCH_SIZE] = {0};

bool is_patched = false;

// pointers to neovim functions
bool (*emsg_multiline_addr)(const char *, bool) = NULL;
static lua_State *(*get_global_lstate)(void) = NULL;

bool custom_emsg_multiline(const char *s, bool multiline) {
  lua_State *L = get_global_lstate();

  if (lua_emsg_multiline_ref != LUA_NOREF) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, lua_emsg_multiline_ref);
    lua_pushstring(L, s);
    lua_pushboolean(L, multiline);

    if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
      const char *error = lua_tostring(L, -1);
      printf("handle_errors.nvim: Error calling custom Lua function: %s\n", error);
      lua_pop(L, 1);
    }

    return true;
  }

  // not possible

  return true;
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
    patch_mem(emsg_multiline_addr, patched_emsg_multiline);
    is_patched = true;
  }
}

void remove_patch() {
  if (is_patched) {
    patch_mem(emsg_multiline_addr, original_emsg_multiline);
    is_patched = false;
  }
}

//@param msg string
//@param multiline boolean default true
int call_original(lua_State *L) {
  const char *msg = luaL_checkstring(L, 1);
  bool multiline = true;

  if (lua_gettop(L) > 1) {
    multiline = lua_toboolean(L, 2);
  }

  bool was_patched = is_patched;
  remove_patch();
  emsg_multiline_addr(msg, multiline);
  if (was_patched) {
    apply_patch();
  }

  return 0;
}

//@param cb fun(msg: string, multiline: boolean)
int patch_emsg_multiline(lua_State *L) {
  if (!lua_isfunction(L, 1)) {
    return luaL_error(L, "Expected a function as the first argument");
  }

  int old_lua_emsg_multiline_ref = lua_emsg_multiline_ref;

  // Store the Lua function in the registry
  lua_emsg_multiline_ref = luaL_ref(L, LUA_REGISTRYINDEX);

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
  void *handle = dlopen(NULL, RTLD_NOW); // Load the current process
  if (!handle) {
    printf("handle_errors.nvim: Failed to open handle: %s", dlerror());
    return;
  }

  get_global_lstate = dlsym(handle, "get_global_lstate");
  emsg_multiline_addr = dlsym(handle, "emsg_multiline");

  if (!emsg_multiline_addr) {
    dlclose(handle);
    printf("handle_errors.nvim: Failed to find emsg_multiline: %s", dlerror());
    return;
  }

  memcpy(original_emsg_multiline, emsg_multiline_addr, PATCH_SIZE);

  // Patch to replace memory location with a custom jump
  // mov rax, Iv
  *(uint16_t *)(patched_emsg_multiline + 0x0) = 0xb848;
  // mov rax, jump_destination
  *(uint64_t *)(patched_emsg_multiline + 0x2) = (uint64_t)custom_emsg_multiline;
  // jmp rax
  *(uint16_t *)(patched_emsg_multiline + 0xa) = 0xe0ff;
}

// The entry point for the module
int luaopen_handle_lua_errors_override_error_printing(lua_State *L) {
  page_size = sysconf(_SC_PAGESIZE);
  init();

  luaL_Reg functions[] = {{"call_original", call_original},
                          {"patch_emsg_multiline", patch_emsg_multiline},
                          {"set_to_original", set_to_original},
                          {NULL, NULL}};
  luaL_newlib(L, functions);
  return 1;
}
