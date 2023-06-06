#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include "interface.h"
#include "lua.h"

lua_State *L;

void init_lua() {
  // printf(">> LUA: init\n");
  L = luaL_newstate();
  luaL_openlibs(L);
}

void deinit_lua() {
  // printf(">> LUA: deinit\n");
  lua_close(L);
}

void lua_run(char *line) { l_report(L, luaL_dostring(L, line)); }

//////////////////////////////////////////////////////////////////////////////

static lua_State *globalL = NULL;

/*
 ** Hook set by signal function to stop the interpreter.
 */
static void lstop(lua_State *L, lua_Debug *ar) {
  (void)ar;                   /* unused arg. */
  lua_sethook(L, NULL, 0, 0); /* reset hook */
  luaL_error(L, "interrupted!");
}

/*
 ** Function to be called at a C signal. Because a C signal cannot
 ** just change a Lua state (as there is no proper synchronization),
 ** this function only sets a hook that, when called, will stop the
 ** interpreter.
 */
static void laction(int i) {
  signal(i, SIG_DFL); /* if another SIGINT happens, terminate process */
  lua_sethook(globalL, lstop, LUA_MASKCALL | LUA_MASKRET | LUA_MASKCOUNT, 1);
}

/*
 ** Message handler used to run all chunks
 */
static int msghandler(lua_State *L) {
  const char *msg = lua_tostring(L, 1);
  if (msg == NULL) {                         /* is error object not a
                                              * string?
                                              * */
    if (luaL_callmeta(L, 1, "__tostring") && /* does it have a
                                              * metamethod
                                              **/
        (lua_type(L, -1) == LUA_TSTRING)) {  /* that produces a string?
                                              **/
      return 1;                              /* that is the message */
    } else {
      msg = lua_pushfstring(L, "(error object is a %s value)",
                            luaL_typename(L, 1));
    }
  }
  luaL_traceback(L, L, msg, 1); /* append a standard traceback */
  return 1;                     /* return the traceback */
}

/*
 ** Check whether 'status' is not OK and, if so, prints the error
 ** message on the top of the stack. It assumes that the error object
 ** is a string, as it was either generated by Lua or by 'msghandler'.
 */
static int report(lua_State *L, int status) {
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    lua_writestringerror("%s\n", msg);
    lua_pop(L, 1); /* remove message */
  }
  return status;
}

int l_report(lua_State *L, int status) {
  report(L, status);
  return 0;
}

static int docall(lua_State *L, int narg, int nres) {
  int status;
  int base = lua_gettop(L) - narg;  /* function index */
  lua_pushcfunction(L, msghandler); /* push message handler */
  lua_insert(L, base);              /* put it under function and args */
  globalL = L;                      /* to be available to 'laction' */
  signal(SIGINT, laction);          /* set C-signal handler */
  status = lua_pcall(L, narg, nres, base);
  signal(SIGINT, SIG_DFL); /* reset C-signal handler */
  lua_remove(L, base);     /* remove message handler from the stack **/
  return status;
}

int l_docall(lua_State *L, int narg, int nres) {
  int stat = docall(L, narg, nres);
  // FIXME: error handling
  return stat;
}

static int dochunk(lua_State *L, int status) {
  if (status == LUA_OK) {
    status = docall(L, 0, 0);
  }
  return report(L, status);
}

int l_dofile(lua_State *L, const char *name) {
  return dochunk(L, luaL_loadfile(L, name));
}

int l_dostring(lua_State *L, const char *s, const char *name) {
  return dochunk(L, luaL_loadbuffer(L, s, strlen(s), name));
}
