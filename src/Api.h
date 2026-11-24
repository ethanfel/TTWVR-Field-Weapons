// SPDX-License-Identifier: GPL-3.0-only
#pragma once
struct NVSE { unsigned version,runtime,editor,isEditor; void *add,*opcode;
    void* (__cdecl* query)(unsigned); unsigned (__cdecl* handle)(); };
struct Info { unsigned version; const char* name; unsigned build; };
struct Message { const char* sender; unsigned type,length; void* data; };
struct Messages { unsigned version; bool (__cdecl* listen)(unsigned,const char*,void (__cdecl*)(Message*)); };
struct Value { union { double number{}; unsigned char* object; }; unsigned char type{}; };
static_assert(sizeof(Value)==16);
struct Scripts { bool (__cdecl* call)(void*,void*,void*,Value*,unsigned char,...);
    void *params,*extract,*format,*callAlt; void* (__cdecl* compile)(const char*); };
struct Arrays {
    void* before[6];
    unsigned (__cdecl* size)(void*);
    void* lookup;
    bool (__cdecl* get)(void*,const Value&,Value&);
};
struct Data { unsigned version; void* singleton; void* (__cdecl* get)(unsigned); };
