#define _GNU_SOURCE

#define LUA_LIB
#include <lua.h>
#include <lauxlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

// Reference to the Lua function passed as a parameter
int lua_function_ref = LUA_NOREF;

// Custom nlua_error function
void custom_nlua_error(lua_State *const lstate, const char *const msg) {
  if (lua_function_ref != LUA_NOREF) {
    // code snippet from original nlua_error
    // https://github.com/neovim/neovim/blob/33464189bc02b2555e26dc4e9f7b3fbbcdd02490/src/nvim/lua/executor.c#L131
    size_t len;
    const char *str = NULL;

    if (luaL_getmetafield(lstate, -1, "__tostring")) {
      if (lua_isfunction(lstate, -1) && luaL_callmeta(lstate, -2, "__tostring")) {
        // call __tostring, convert the result and pop result.
        str = lua_tolstring(lstate, -1, &len);
        lua_pop(lstate, 1);
      }
      // pop __tostring.
      lua_pop(lstate, 1);
    }

    if (!str) {
      // defer to lua default conversion, this will render tables as [NULL].
      str = lua_tolstring(lstate, -1, &len);
    }

    // get real error message
    size_t needed = snprintf(NULL, 0, msg, (int)len, str) + 1;
    char buffer[needed];
    snprintf(buffer, sizeof(buffer), msg, (int)len, str);

    lua_rawgeti(lstate, LUA_REGISTRYINDEX, lua_function_ref);  // Get the Lua function
    lua_pushstring(lstate, buffer);  // set parameter to the error message

    if (lua_pcall(lstate, 1, 0, 0) != LUA_OK) {  // Call the Lua function with 1 argument
      const char* error = lua_tostring(lstate, -1);
      printf("Error calling custom Lua function: %s\n", error);
      lua_pop(lstate, 1);  // Pop the error message from the stack
    }
  } else {
    printf("Error in handle_lua_errors: no error function found");
  }
}

// Patching function to replace memory location with a custom jump
// https://reverseengineering.stackexchange.com/a/20399
void patch_mem(uint64_t address, uint64_t jump_destination) {
  long page_size = sysconf(_SC_PAGESIZE);
  void* aligned_address = (void*)(address & ~(page_size - 1));
  if (mprotect(aligned_address, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
    perror("mprotect");
    return;
  }
  *(uint16_t*)(address + 0x0) = 0xb848;           // mov rax, Iv
  *(uint64_t*)(address + 0x2) = jump_destination; // mov rax, jump_destination
  *(uint16_t*)(address + 0xa) = 0xe0ff;           // jmp rax
  if (mprotect(aligned_address, page_size, PROT_READ | PROT_EXEC) < 0) {
    perror("mprotect");
    return;
  }
}

// The Lua-facing function to replace nlua_error
int replace_nlua_error(lua_State *L) {
  if (!lua_isfunction(L, 1)) {
    return luaL_error(L, "Expected a function as the first argument");
  }

  int old_lua_function_ref = lua_function_ref;

  // Store the Lua function in the registry
  lua_function_ref = luaL_ref(L, LUA_REGISTRYINDEX);

  // if custom_nlua_error is already patched, return
  if (old_lua_function_ref != LUA_NOREF) {
    return 0;
  }

  // Find the original nlua_error using dlsym
  void *handle = dlopen(NULL, RTLD_NOW);  // Load the current process
  if (!handle) {
    return luaL_error(L, "Failed to open handle: %s", dlerror());
  }

  void *original_nlua_error = dlsym(handle, "nlua_error");
  if (!original_nlua_error) {
    dlclose(handle);
    return luaL_error(L, "Failed to find nlua_error: %s", dlerror());
  }

  // Replace the original nlua_error with the custom one
  patch_mem((uint64_t)original_nlua_error, (uint64_t)custom_nlua_error);

  dlclose(handle);
  return 0;
}

// The entry point for the module
int luaopen_handle_lua_errors_override_nlua_error(lua_State *L) {
  luaL_Reg functions[] = {
    {"replace_nlua_error", replace_nlua_error},
    {NULL, NULL}
  };
  luaL_newlib(L, functions);
  return 1;
}

