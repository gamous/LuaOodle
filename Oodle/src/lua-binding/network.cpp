extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include  <vector>
#include "oodlebase.h"

struct ON1_HashEntry
{
#if ON1_HASHENTRY_MP0
	// 24 bit dictionary index :
	RR_COMPILER_ASSERT(OODLENETWORK1_MAX_DICTIONARY_SIZE <= (1 << 24));
	U32	index : 24; // OODLENETWORK1_MAX_DICTIONARY_SIZE
	U32 mp0 : 8;
	U32 context;
#else
	U32	index;
	U32 context;
#endif
};
typedef struct ON1_HashEntry ON1_HashEntry;
struct OodleNetwork1_Shared
{
	const U8* m_lzp_window;
	const U8* m_lzp_window_end;
	U32 m_lzp_hash_mask;
	U32 m_pad_for_alignment;
	ON1_HashEntry m_lzp_hash[1];
};
#define OODLENETWORK1_MAX_DICTIONARY_SIZE	(1<<24)		IDOC
IDOC typedef struct OodleNetwork1_Shared OodleNetwork1_Shared;
IDOC typedef struct OodleNetwork1TCP_State OodleNetwork1TCP_State;
#define OODLENETWORK1_HASH_BITS_DEFAULT	(19)	IDOC
#define OODLENETWORK1_DECOMP_BUF_OVERREAD_LEN	(5)	IDOC
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1_Shared_Size(OO_S32 htbits);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1TCP_State_Size(void);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1_CompressedBufferSizeNeeded(OO_SINTa rawLen);
IDOC OOFUNC1 void OOFUNC2 OodleNetwork1_Shared_SetWindow(OodleNetwork1_Shared* data, OO_S32 htbits, const void* window, OO_S32 window_size);
IDOC OOFUNC1 void OOFUNC2 OodleNetwork1TCP_State_Reset(OodleNetwork1TCP_State* state);
IDOC OOFUNC1 void OOFUNC2 OodleNetwork1TCP_State_InitAsCopy(OodleNetwork1TCP_State* state, const OodleNetwork1TCP_State* from);
IDOC OOFUNC1 void OOFUNC2 OodleNetwork1TCP_Train(OodleNetwork1TCP_State* state, const OodleNetwork1_Shared* shared, const void** training_packet_pointers, const OO_S32* training_packet_sizes, OO_S32 num_training_packets);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1TCP_Encode(OodleNetwork1TCP_State* state, const OodleNetwork1_Shared* shared, const void* raw, OO_SINTa rawLen, void* comp);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1TCP_Decode(OodleNetwork1TCP_State* state, const OodleNetwork1_Shared* shared, const void* comp, OO_SINTa compLen, void* raw, OO_SINTa rawLen);
IDOC typedef struct OodleNetwork1UDP_State OodleNetwork1UDP_State;
IDOC OOFUNC1 void OOFUNC2 OodleNetwork1UDP_Train(OodleNetwork1UDP_State* state, const OodleNetwork1_Shared* shared, const void** training_packet_pointers, const OO_S32* training_packet_sizes, OO_S32 num_training_packets);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1UDP_State_Size(void);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1UDP_Encode(const OodleNetwork1UDP_State* state, const OodleNetwork1_Shared* shared, const void* raw, OO_SINTa rawLen, void* comp);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1UDP_Decode(const OodleNetwork1UDP_State* state, const OodleNetwork1_Shared* shared, const void* comp, OO_SINTa compLen, void* raw, OO_SINTa rawLen);
IDOC typedef struct OodleNetwork1UDP_StateCompacted OodleNetwork1UDP_StateCompacted;
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1UDP_StateCompacted_MaxSize(void);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1UDP_State_Compact_ForVersion(OodleNetwork1UDP_StateCompacted* to, const OodleNetwork1UDP_State* from, OO_S32 for_oodle_major_version);
IDOC OOFUNC1 OO_SINTa OOFUNC2 OodleNetwork1UDP_State_Compact(OodleNetwork1UDP_StateCompacted* to, const OodleNetwork1UDP_State* from);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1UDP_State_Uncompact_ForVersion(OodleNetwork1UDP_State* to, const OodleNetwork1UDP_StateCompacted* from, OO_S32 for_oodle_major_version);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1UDP_State_Uncompact(OodleNetwork1UDP_State* to, const OodleNetwork1UDP_StateCompacted* from);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1_SelectDictionarySupported(void);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1_SelectDictionaryFromPackets(void* dictionary_to_fill, OO_S32 dictionary_size, OO_S32 htbits, const void** dictionary_packet_pointers, const OO_S32* dictionary_packet_sizes, OO_S32 num_dictionary_packets, const void** test_packet_pointers, const OO_S32* test_packet_sizes, OO_S32 num_test_packets);
IDOC OOFUNC1 OO_BOOL OOFUNC2 OodleNetwork1_SelectDictionaryFromPackets_Trials(void* dictionary_to_fill, OO_S32 dictionary_size, OO_S32 htbits, const void** dictionary_packet_pointers, const OO_S32* dictionary_packet_sizes, OO_S32 num_dictionary_packets, const void** test_packet_pointers, const OO_S32* test_packet_sizes, OO_S32 num_test_packets, OO_S32 num_trials, double randomness_percent, OO_S32 num_generations);



//lua-c ByteArray (userdatum)

//typedef unsigned char uint8;
//typedef struct ByteArray { 
//	int size;
//	uint8 values[1]; /* variable part */
//}ByteArray; 
////ByteArray 1st arg
//static ByteArray* check_ByteArray(lua_State* L) {
//    void* ud = luaL_checkudata(L, 1, "LuaOddle.ByteArray");
//    luaL_argcheck(L, ud != NULL, 1, "`array' expected");
//    return (ByteArray*)ud;
//}
//static int len_ByteArray(lua_State* L) {
//    //Lua: oo2net9.len(ByteArray a) -> int
//    //     ByteArray:len()-> int
//    ByteArray* ba = check_ByteArray(L);
//    lua_pushnumber(L, ba->size);
//    return 1;
//}
//
////ByteArray 2nd arg
//static uint8* index_ByteArray(lua_State* L) {
//    ByteArray* ba = check_ByteArray(L);
//    int index = luaL_checkint(L, 2);
//    luaL_argcheck(L, 1 <= index && index <= ba->size, 2,"index out of range");
//    /* return element address */
//    return &ba->values[index-1];
//}
//static int new_ByteArray (lua_State *L) {
//	//Lua: oo2net9.new(int size) -> ByteArray
//	int n = luaL_checkint(L, 1);
//	size_t nbytes = sizeof(ByteArray) + n*sizeof(char); 
//	ByteArray *ba = (ByteArray *)lua_newuserdata(L, nbytes);//pushstack 1:userdatum
//    luaL_getmetatable(L, "LuaOddle.ByteArray");
//    lua_setmetatable(L, -2);
//	ba->size = n;
//	return 1;
//} 
//static int set_ByteArray (lua_State *L) { 
//	//Lua: oo2net9.set(ByteArray a, int index, uint8 value) -> void
//	//     ByteArray:set(int index, uint8 value)->void
//    uint8 value = luaL_checknumber(L, 3);
//    *index_ByteArray(L) = value;
//    return 0;
//}
//static int get_ByteArray (lua_State *L) { 
//	//Lua: oo2net9.get(ByteArray a, int index) -> uint8
//	//     ByteArray:get(int index)-> uint8
//    lua_pushnumber(L, *index_ByteArray(L));
//	return 1; 
//}
//


//lua moudule install
int HashtableBits = 19;
const int WindowSize = 0x16000;
void* _window;
OodleNetwork1UDP_State* _state;
OodleNetwork1_Shared* _shared;
bool lua_oo2net9_inited = false;


//Lua: Initialize() -> userdatum[3]{_state _shared _window}
static int lua_OodleInitialize(lua_State* L)
{
    bool inited = false;
    int stateSize = OodleNetwork1UDP_State_Size();
    int sharedSize = OodleNetwork1_Shared_Size(HashtableBits);
    _state = (OodleNetwork1UDP_State*)lua_newuserdata(L, stateSize);
    _shared = (OodleNetwork1_Shared *)lua_newuserdata(L, sharedSize);
    _window = (void*)lua_newuserdata(L, WindowSize);
    OodleNetwork1_Shared_SetWindow(_shared, HashtableBits, _window, WindowSize);
    OodleNetwork1UDP_Train(_state, _shared, 0, 0, 0);//
    lua_oo2net9_inited = true;
    return 3;
}

//InLua: Decompress(string payload,int start,int complen, rawlen) -> string
static int lua_OodleUDPDecompress(lua_State* L) {
    const char* payload = lua_tostring(L, 1);
    int compLen = luaL_checkint(L, 2);
    int rawLen = luaL_checkint(L, 3);
    void* raw = (void*)new char[rawLen];
    OodleNetwork1UDP_Decode(_state, _shared, payload, compLen, raw, rawLen);
    lua_pushlstring(L, (const char*)raw, rawLen);
    return 1;
}

static int lua_OodleVersion(lua_State* L) {
    lua_pushlstring(L, "OodleNetwork: oo2net_9_win64", 27);
    return 1;
}

//export func table
static const struct luaL_Reg lua_Oodle[] =
{
    {"Version",                        lua_OodleVersion},
    {"Initialize",                     lua_OodleInitialize},
    {"Decompress",                     lua_OodleUDPDecompress},
    {NULL, NULL}
};

extern "C"
{
    int __declspec(dllexport) luaopen_oo2net9(lua_State * L)
    {
        luaL_newlib(L, lua_Oodle);
        return 1;
    }
}