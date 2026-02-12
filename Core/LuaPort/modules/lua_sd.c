// Lua_sd.c
// Lua binding for FatFs SD card
// API:
//   local f = sd.open("test.txt", "w")
//   sd.write(f, "hello")        -- or f:write("hello")
//   sd.close(f)                 -- or f:close()
// Also provides typo alias: wirte

#include "lua.h"
#include "lauxlib.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "ff.h"     // FatFs: FIL, FRESULT, f_open/f_read/f_write/f_close/f_mount/f_lseek...

#ifndef LUA_SD_MODNAME
#define LUA_SD_MODNAME "sd"
#endif

// -------- FatFs drive/path config --------
#ifndef LUA_SD_DRIVE
#define LUA_SD_DRIVE "0:"     // change to "1:" / "SD:" if your port uses another name
#endif

#ifndef LUA_SD_MOUNT_PATH
#define LUA_SD_MOUNT_PATH LUA_SD_DRIVE
#endif

// -------- internal state (global) --------
static FATFS g_sd_fs;
static bool  g_sd_mounted = false;

static inline uint32_t check_u32(lua_State* L, int idx) {
  lua_Integer v = luaL_checkinteger(L, idx);
  if (v < 0) luaL_error(L, "arg %d must be >= 0", idx);
  return (uint32_t)v;
}

static const char* fr2str(FRESULT fr) {
  switch (fr) {
    case FR_OK:                  return "FR_OK";
    case FR_DISK_ERR:            return "FR_DISK_ERR";
    case FR_INT_ERR:             return "FR_INT_ERR";
    case FR_NOT_READY:           return "FR_NOT_READY";
    case FR_NO_FILE:             return "FR_NO_FILE";
    case FR_NO_PATH:             return "FR_NO_PATH";
    case FR_INVALID_NAME:        return "FR_INVALID_NAME";
    case FR_DENIED:              return "FR_DENIED";
    case FR_EXIST:               return "FR_EXIST";
    case FR_INVALID_OBJECT:      return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED:     return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE:       return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED:         return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM:       return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED:        return "FR_MKFS_ABORTED";
    case FR_TIMEOUT:             return "FR_TIMEOUT";
    case FR_LOCKED:              return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE:     return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER:   return "FR_INVALID_PARAMETER";
    default:                     return "FR_?";
  }
}

static int sd_ensure_mounted(lua_State* L) {
  if (g_sd_mounted) return 0;
  FRESULT fr = f_mount(&g_sd_fs, LUA_SD_MOUNT_PATH, 1);
  if (fr != FR_OK) {
    return luaL_error(L, "sd: mount failed (%s=%d)", fr2str(fr), (int)fr);
  }
  g_sd_mounted = true;
  return 0;
}

// Build FatFs path:
// - if user already passes "0:/xxx" or "0:xxx" => use as-is
// - else prefix "0:" and ensure slash if needed
static void sd_make_path(char* out, size_t out_sz, const char* in) {
  if (!in) { out[0] = 0; return; }

  // Already contains ":"
  if (strchr(in, ':') != NULL) {
    // use as-is
    strncpy(out, in, out_sz - 1);
    out[out_sz - 1] = 0;
    return;
  }

  // Prefix drive
  // Allow in starts with '/' or not
  if (in[0] == '/') {
    // "0:" + "/xxx"
    snprintf(out, out_sz, "%s%s", LUA_SD_DRIVE, in);
  } else {
    // "0:" + "/"+ "xxx"
    snprintf(out, out_sz, "%s/%s", LUA_SD_DRIVE, in);
  }
}

static BYTE sd_parse_mode(lua_State* L, const char* mode) {
  // Accept: "r", "w", "a", "r+", "w+", "a+", also allow "rb"/"wb"/"ab" etc.
  // FatFs flags:
  //   FA_READ, FA_WRITE, FA_CREATE_ALWAYS, FA_OPEN_EXISTING, FA_OPEN_ALWAYS, FA_CREATE_NEW
  //   FA_OPEN_APPEND (if enabled in your FatFs version)
  if (!mode || mode[0] == 0) mode = "r";

  bool plus = (strchr(mode, '+') != NULL);

  switch (mode[0]) {
    case 'r':
      return (BYTE)(FA_READ | (plus ? FA_WRITE : 0) | FA_OPEN_EXISTING);
    case 'w':
      return (BYTE)(FA_WRITE | (plus ? FA_READ : 0) | FA_CREATE_ALWAYS);
    case 'a': {
      // If your FatFs has FA_OPEN_APPEND, prefer it; otherwise OPEN_ALWAYS + seek end after open
#ifdef FA_OPEN_APPEND
      return (BYTE)(FA_WRITE | (plus ? FA_READ : 0) | FA_OPEN_APPEND);
#else
      return (BYTE)(FA_WRITE | (plus ? FA_READ : 0) | FA_OPEN_ALWAYS);
#endif
    }
    default:
      luaL_error(L, "sd.open: invalid mode '%s' (use r/w/a/r+/w+/a+)", mode);
      return (BYTE)(FA_READ | FA_OPEN_EXISTING);
  }
}

// -----------------------------
// userdata: sd.file
// -----------------------------
typedef struct {
  FIL   fil;
  bool  opened;
  BYTE  mode;
} sd_file_ud_t;

#define SD_FILE_MT "sd.file.mt"

static sd_file_ud_t* check_sd_file(lua_State* L, int idx) {
  return (sd_file_ud_t*)luaL_checkudata(L, idx, SD_FILE_MT);
}

static int l_sd_file_close(lua_State* L) {
  sd_file_ud_t* ud = check_sd_file(L, 1);
  if (ud->opened) {
    FRESULT fr = f_close(&ud->fil);
    ud->opened = false;
    if (fr != FR_OK) {
      return luaL_error(L, "sd.close: f_close failed (%s=%d)", fr2str(fr), (int)fr);
    }
  }
  return 0;
}

// f:write(data) -> written_len
static int l_sd_file_write(lua_State* L) {
  sd_file_ud_t* ud = check_sd_file(L, 1);
  size_t len = 0;
  const char* data = luaL_checklstring(L, 2, &len);

  if (!ud->opened) {
    return luaL_error(L, "sd.write: file is closed");
  }

  UINT bw = 0;
  FRESULT fr = f_write(&ud->fil, data, (UINT)len, &bw);
  if (fr != FR_OK) {
    return luaL_error(L, "sd.write: f_write failed (%s=%d)", fr2str(fr), (int)fr);
  }

  lua_pushinteger(L, (lua_Integer)bw);
  return 1;
}

// f:read(n) -> string
static int l_sd_file_read(lua_State* L) {
  sd_file_ud_t* ud = check_sd_file(L, 1);
  uint32_t n = check_u32(L, 2);

  if (!ud->opened) {
    return luaL_error(L, "sd.read: file is closed");
  }

  luaL_Buffer b;
  char* buf = luaL_buffinitsize(L, &b, (size_t)n);

  UINT br = 0;
  FRESULT fr = f_read(&ud->fil, buf, (UINT)n, &br);
  if (fr != FR_OK) {
    return luaL_error(L, "sd.read: f_read failed (%s=%d)", fr2str(fr), (int)fr);
  }

  luaL_pushresultsize(&b, (size_t)br);
  return 1;
}

// f:seek(pos) -> new_pos
static int l_sd_file_seek(lua_State* L) {
  sd_file_ud_t* ud = check_sd_file(L, 1);
  uint32_t pos = check_u32(L, 2);

  if (!ud->opened) {
    return luaL_error(L, "sd.seek: file is closed");
  }

  FRESULT fr = f_lseek(&ud->fil, (FSIZE_t)pos);
  if (fr != FR_OK) {
    return luaL_error(L, "sd.seek: f_lseek failed (%s=%d)", fr2str(fr), (int)fr);
  }

  lua_pushinteger(L, (lua_Integer)f_tell(&ud->fil));
  return 1;
}

// f:size() -> size
static int l_sd_file_size(lua_State* L) {
  sd_file_ud_t* ud = check_sd_file(L, 1);
  if (!ud->opened) {
    return luaL_error(L, "sd.size: file is closed");
  }
  lua_pushinteger(L, (lua_Integer)f_size(&ud->fil));
  return 1;
}

static const luaL_Reg sd_file_methods[] = {
  {"close", l_sd_file_close},
  {"read",  l_sd_file_read},
  {"write", l_sd_file_write},
  {"wirte", l_sd_file_write},   // typo alias
  {"seek",  l_sd_file_seek},
  {"size",  l_sd_file_size},
  {NULL, NULL}
};

static void sd_create_metatable(lua_State* L) {
  if (luaL_newmetatable(L, SD_FILE_MT)) {
    lua_newtable(L);
    luaL_setfuncs(L, sd_file_methods, 0);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, l_sd_file_close);
    lua_setfield(L, -2, "__gc");
  }
  lua_pop(L, 1);
}

// -----------------------------
// module functions: sd.open / sd.close / sd.write / sd.read ...
// -----------------------------

// sd.open(path, mode?) -> file_ud
static int l_sd_open(lua_State* L) {
  sd_ensure_mounted(L);

  const char* path_in = luaL_checkstring(L, 1);
  const char* mode = luaL_optstring(L, 2, "r");

  char path[256];
  sd_make_path(path, sizeof(path), path_in);

  BYTE fmode = sd_parse_mode(L, mode);

  sd_file_ud_t* ud = (sd_file_ud_t*)lua_newuserdatauv(L, sizeof(sd_file_ud_t), 0);
  memset(ud, 0, sizeof(*ud));
  ud->opened = false;
  ud->mode = fmode;

  FRESULT fr = f_open(&ud->fil, path, fmode);
  if (fr != FR_OK) {
    // pop userdata
    lua_pop(L, 1);
    return luaL_error(L, "sd.open('%s','%s'): f_open failed (%s=%d)", path, mode, fr2str(fr), (int)fr);
  }

#ifndef FA_OPEN_APPEND
  // If append mode but no FA_OPEN_APPEND, emulate: seek to end
  if (mode && mode[0] == 'a') {
    (void)f_lseek(&ud->fil, f_size(&ud->fil));
  }
#endif

  ud->opened = true;

  luaL_getmetatable(L, SD_FILE_MT);
  lua_setmetatable(L, -2);
  return 1;
}

// sd.close(file) -> nil
static int l_sd_close(lua_State* L) {
  // wrapper: sd.close(f) == f:close()
  luaL_checktype(L, 1, LUA_TUSERDATA);
  lua_settop(L, 1);
  return l_sd_file_close(L);
}

// sd.write(file, data) -> written_len
static int l_sd_write(lua_State* L) {
  // wrapper: sd.write(f, s) == f:write(s)
  lua_settop(L, 2);
  return l_sd_file_write(L);
}

// sd.read(file, n) -> string
static int l_sd_read(lua_State* L) {
  lua_settop(L, 2);
  return l_sd_file_read(L);
}

// sd.seek(file, pos) -> new_pos
static int l_sd_seek(lua_State* L) {
  lua_settop(L, 2);
  return l_sd_file_seek(L);
}

// sd.size(file) -> size
static int l_sd_size(lua_State* L) {
  lua_settop(L, 1);
  return l_sd_file_size(L);
}

// Optional: sd.mount() / sd.umount()
static int l_sd_mount(lua_State* L) {
  // force remount
  FRESULT fr = f_mount(&g_sd_fs, LUA_SD_MOUNT_PATH, 1);
  if (fr != FR_OK) {
    return luaL_error(L, "sd.mount: failed (%s=%d)", fr2str(fr), (int)fr);
  }
  g_sd_mounted = true;
  lua_pushboolean(L, 1);
  return 1;
}

static int l_sd_umount(lua_State* L) {
  FRESULT fr = f_mount(NULL, LUA_SD_MOUNT_PATH, 1);
  if (fr != FR_OK) {
    return luaL_error(L, "sd.umount: failed (%s=%d)", fr2str(fr), (int)fr);
  }
  g_sd_mounted = false;
  lua_pushboolean(L, 1);
  return 1;
}

static const luaL_Reg sd_funcs[] = {
  {"open",  l_sd_open},
  {"close", l_sd_close},
  {"write", l_sd_write},
  {"wirte", l_sd_write},   // typo alias
  {"read",  l_sd_read},
  {"seek",  l_sd_seek},
  {"size",  l_sd_size},
  {"mount", l_sd_mount},
  {"umount",l_sd_umount},
  {NULL, NULL}
};

int luaopen_sd(lua_State* L) {
  sd_create_metatable(L);
  luaL_newlib(L, sd_funcs);
  return 1;
}
